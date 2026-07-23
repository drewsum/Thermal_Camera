/*******************************************************************************
  FLIR Lepton VoSPI (Video over SPI) Capture Driver

  File Name:
    flir_vospi.c

  Summary:
    SPI4 + DMA capture and frame assembly for the Lepton 3.5. See flir_vospi.h
    for the VoSPI framing and the two-channel DMA capture design.
*******************************************************************************/

#include "application/flir/flir_vospi.h"
#include "core/device_control.h"
#include "core/32mzda_interrupt_control.h"
#include "gpio/pin_macros.h"
#include "usb_uart/terminal_control.h"

#include <xc.h>
#include <sys/kmem.h>
#include <sys/attribs.h>
#include <stdio.h>
#include <string.h>

// SPI4 is clocked from PBCLK2 (SYSCLK/3 = 66.67 MHz), same as SPI3 (spi3.c).
#define FLIR_VOSPI_PBCLK_HZ         (SYSCLK_INT / 3u)

// VoSPI clock. The Lepton 3.5 tolerates up to ~20 MHz; BRG=1 gives
// PBCLK2 / (2*(1+1)) = 16.67 MHz, a safe margin. (Fsck = PBCLK/(2*(BRG+1)).)
#define FLIR_VOSPI_BRG              1u

// DMA channels: DCH3 generates the clock (dummy TX), DCH2 captures RX. DMA
// channels 0/1 are the USB UART (usb_uart.h); 2/3 are otherwise free.
// Interrupt priorities: DMA at IPL5, VSYNC/INT1 at IPL4 -- both have their own
// shadow register set (PRISS, interruptControllerInitialize()) and sit clear
// of I2C (IPL7) and the USB UART (IPL1/2).
#define FLIR_VOSPI_DMA_IPL          5
#define FLIR_VOSPI_VSYNC_IPL        4

// A discard/idle packet has 0xF in the ID word's top nibble.
#define FLIR_VOSPI_IS_DISCARD(hdr0) (((hdr0) & 0x0Fu) == 0x0Fu)

// --- Capture buffers -------------------------------------------------------

// One packet lands here per DMA block; the ISR copies its payload out before
// re-arming. Coherent (uncached) so the CPU sees exactly what DMA wrote, and
// word-aligned for the DMA engine.
static uint8_t vospiPacket[FLIR_VOSPI_PACKET_BYTES] __attribute__((coherent, aligned(4)));

// 164 zero bytes clocked out to drive SCK during a packet read. Not const:
// the DMA engine reads it from RAM, and 'coherent' (uncached DMA memory) is
// incompatible with the .rodata section 'const' would place it in. Never
// written, so it stays all-zero (static zero-initialization).
static uint8_t vospiTxDummy[FLIR_VOSPI_PACKET_BYTES] __attribute__((coherent, aligned(4)));

// One segment's raw payloads accumulate here (CPU-only, no DMA) until the
// segment completes and is placed into the frame buffer.
static uint8_t segScratch[FLIR_VOSPI_PACKETS_PER_SEGMENT * FLIR_VOSPI_PACKET_PAYLOAD_BYTES];

// Two DDR2 frame buffers (14-bit values). captureFrame is being filled;
// readyFrame is the last completed one handed to the app.
static volatile uint16_t *captureFrame;
static volatile uint16_t *readyFrame;
static volatile bool frameReadyFlag;

// --- Capture state machine (all touched only in the DMA ISR) ---------------
static bool     haveSync;         // locked onto the packet-0..59 sequence
static uint8_t  expectedPacket;   // next packet number we expect (0..59)
static uint8_t  currentSegment;   // segment id from packet 20 (1..4)
static uint8_t  segmentMask;      // bitmask of segments placed for this frame
static FLIR_VOSPI_STATS stats;

// Reset the frame-assembly state (called on start and on any desync).
static void FLIR_VOSPI_ResetSync(void)
{
    haveSync = false;
    expectedPacket = 0;
    currentSegment = 0;
    segmentMask = 0;
}

void FLIR_VOSPI_Initialize(void)
{
    // --- SPI4 (mirrors spi3.c, but VoSPI is SPI mode 3 and read-mostly) ---
    SPI4CONbits.ON = 0;
    SPI4STATbits.SPIROV = 0;
    (void)SPI4BUF;

    SPI4BRG = FLIR_VOSPI_BRG;

    SPI4CONbits.MSTEN = 1;      // master: we generate the clock for the slave
    SPI4CONbits.SIDL = 0;

    // SPI mode 3 for VoSPI: clock idles high, data sampled on the rising
    // (idle-to-active) edge. PIC32's CKE is inverted vs CPHA, so mode 3 is
    // CKP=1 / CKE=0.
    SPI4CONbits.CKP = 1;
    SPI4CONbits.CKE = 0;
    SPI4CONbits.SMP = 1;        // sample input at the end of the output time

    SPI4CONbits.MODE32 = 0;     // 8-bit words
    SPI4CONbits.MODE16 = 0;

    SPI4CONbits.SSEN = 0;       // CS is a plain GPIO (nFLIR_VOSPI_CS), not SS
    SPI4CONbits.MSSEN = 0;
    SPI4CONbits.MCLKSEL = 0;    // PBCLK, not REFCLK
    SPI4CONbits.FRMEN = 0;

    // Enhanced (FIFO) buffer mode with interrupts that pace the DMA: TX event
    // when the buffer can accept a byte, RX event when a byte has arrived.
    SPI4CONbits.ENHBUF = 1;
    SPI4CONbits.STXISEL = 0b01; // TX event: buffer not full
    SPI4CONbits.SRXISEL = 0b01; // RX event: buffer not empty

    SPI4CONbits.ON = 1;

    // --- DMA controller ---
    DMACONbits.ON = 1;          // idempotent; also enabled by the USB UART

    // RX channel (DCH2): SPI4BUF -> vospiPacket, one byte per SPI4-RX event.
    DCH2CON = 0;
    DCH2CONbits.CHPRI = 0b10;   // priority 2 (above the UART, below nothing critical)
    DCH2ECON = 0;
    DCH2ECONbits.CHSIRQ = spi4_receive_done;   // start cell on each RX event
    DCH2ECONbits.SIRQEN = 1;
    DCH2SSA = KVA_TO_PA((uint32_t)&SPI4BUF);
    DCH2SSIZ = 1;               // source is the 1-byte SPI FIFO (wraps)
    DCH2DSA = KVA_TO_PA((uint32_t)vospiPacket);
    DCH2DSIZ = FLIR_VOSPI_PACKET_BYTES;        // block = one whole packet
    DCH2CSIZ = 1;               // one byte per event
    DCH2INT = 0;
    DCH2INTbits.CHBCIE = 1;     // interrupt on block (packet) complete

    // TX channel (DCH3): vospiTxDummy -> SPI4BUF, one byte per SPI4-TX event.
    DCH3CON = 0;
    DCH3CONbits.CHPRI = 0b10;
    DCH3ECON = 0;
    DCH3ECONbits.CHSIRQ = spi4_transfer_done;
    DCH3ECONbits.SIRQEN = 1;
    DCH3SSA = KVA_TO_PA((uint32_t)vospiTxDummy);
    DCH3SSIZ = FLIR_VOSPI_PACKET_BYTES;        // 164 dummy bytes per block
    DCH3DSA = KVA_TO_PA((uint32_t)&SPI4BUF);
    DCH3DSIZ = 1;               // dest is the 1-byte SPI FIFO (wraps)
    DCH3CSIZ = 1;
    DCH3INT = 0;

    // DMA block-complete interrupt (RX channel only).
    setInterruptPriority(dma_channel_2, FLIR_VOSPI_DMA_IPL);
    setInterruptSubpriority(dma_channel_2, 0);
    clearInterruptFlag(dma_channel_2);

    // --- VSYNC on INT1 (RD0 already PPS-mapped to INT1 in gpio setup) ---
    INTCONbits.INT1EP = 1;      // rising-edge triggered
    setInterruptPriority(external_interrupt_1, FLIR_VOSPI_VSYNC_IPL);
    setInterruptSubpriority(external_interrupt_1, 0);
    clearInterruptFlag(external_interrupt_1);

    // Frame buffers.
    captureFrame = (volatile uint16_t *)FLIR_VOSPI_FRAME_A_ADDRESS;
    readyFrame   = (volatile uint16_t *)FLIR_VOSPI_FRAME_B_ADDRESS;
    frameReadyFlag = false;

    memset((void *)FLIR_VOSPI_FRAME_A_ADDRESS, 0, FLIR_VOSPI_FRAME_SIZE_BYTES);
    memset((void *)FLIR_VOSPI_FRAME_B_ADDRESS, 0, FLIR_VOSPI_FRAME_SIZE_BYTES);

    memset(&stats, 0, sizeof(stats));
    FLIR_VOSPI_ResetSync();

    // Deassert CS; leave everything disarmed.
    nFLIR_VOSPI_CS_PIN = HIGH;
}

// Re-arms both DMA channels for the next packet. RX first so it captures from
// the first clocked byte; then TX, which starts the clock. Pointers reset to
// the programmed SSA/DSA automatically when CHEN is set.
static inline void FLIR_VOSPI_ArmPacket(void)
{
    DCH2INTCLR = _DCH2INT_CHBCIF_MASK;
    clearInterruptFlag(dma_channel_2);
    DCH2CONSET = _DCH2CON_CHEN_MASK;   // arm RX
    DCH3CONSET = _DCH3CON_CHEN_MASK;   // start TX (generates the clock)
}

void FLIR_VOSPI_Start(void)
{
    // Flush any stale RX state.
    SPI4STATbits.SPIROV = 0;
    while (SPI4STATbits.SPIRBE == 0)
    {
        (void)SPI4BUF;
    }

    FLIR_VOSPI_ResetSync();

    nFLIR_VOSPI_CS_PIN = LOW;

    setInterruptEnable(dma_channel_2, 1);
    setInterruptEnable(external_interrupt_1, 1);

    FLIR_VOSPI_ArmPacket();
}

void FLIR_VOSPI_Stop(void)
{
    // Disarm DMA and mask the interrupts.
    DCH2CONCLR = _DCH2CON_CHEN_MASK;
    DCH3CONCLR = _DCH3CON_CHEN_MASK;
    setInterruptEnable(dma_channel_2, 0);
    setInterruptEnable(external_interrupt_1, 0);

    nFLIR_VOSPI_CS_PIN = HIGH;

    FLIR_VOSPI_ResetSync();
}

bool FLIR_VOSPI_FrameReady(void)
{
    return frameReadyFlag;
}

const uint16_t *FLIR_VOSPI_TakeFrame(void)
{
    if (!frameReadyFlag)
    {
        return NULL;
    }

    frameReadyFlag = false;
    return (const uint16_t *)readyFrame;
}

void FLIR_VOSPI_GetStats(FLIR_VOSPI_STATS *out)
{
    if (out != NULL)
    {
        *out = stats;
    }
}

// Places one completed segment's 60 payloads into the capture frame buffer,
// byte-swapping and masking each pixel to 14 bits. seg is 1..4.
static void FLIR_VOSPI_PlaceSegment(uint8_t seg)
{
    uint32_t k;

    for (k = 0; k < FLIR_VOSPI_PACKETS_PER_SEGMENT; k++)
    {
        const uint8_t *src = &segScratch[k * FLIR_VOSPI_PACKET_PAYLOAD_BYTES];
        // Two packets per 160-pixel row; even packet = left 80px, odd = right.
        uint32_t row = ((uint32_t)(seg - 1) * 30u) + (k / 2u);
        uint32_t colStart = (k & 1u) * 80u;
        volatile uint16_t *dst = &captureFrame[(row * FLIR_VOSPI_WIDTH_PX) + colStart];
        uint32_t i;

        for (i = 0; i < 80u; i++)
        {
            dst[i] = (uint16_t)((((uint16_t)src[i * 2u] << 8) | src[(i * 2u) + 1u]) & 0x3FFFu);
        }
    }
}

// Flips the freshly filled capture buffer to be the ready buffer, and points
// capture at the other one.
static void FLIR_VOSPI_CompleteFrame(void)
{
    volatile uint16_t *justFilled = captureFrame;

    readyFrame = justFilled;
    captureFrame = (justFilled == (volatile uint16_t *)FLIR_VOSPI_FRAME_A_ADDRESS)
                 ? (volatile uint16_t *)FLIR_VOSPI_FRAME_B_ADDRESS
                 : (volatile uint16_t *)FLIR_VOSPI_FRAME_A_ADDRESS;
    frameReadyFlag = true;
    stats.framesCaptured++;
}

// DMA channel 2 block-complete: one VoSPI packet has been captured into
// vospiPacket. Parse, accumulate, and re-arm for the next packet. Short and
// integer-only, per the ISR conventions (IPL5 shadow register set).
void __ISR(_DMA2_VECTOR, IPL5SRS) flirVospiDmaISR(void)
{
    uint8_t hdr0 = vospiPacket[0];
    uint8_t pnum = vospiPacket[1];

    // Discard/idle packets carry no image data.
    if (FLIR_VOSPI_IS_DISCARD(hdr0))
    {
        FLIR_VOSPI_ArmPacket();
        return;
    }

    // Seek the start of a segment before trusting anything.
    if (!haveSync)
    {
        if (pnum != 0)
        {
            FLIR_VOSPI_ArmPacket();
            return;
        }
        haveSync = true;
        expectedPacket = 0;
    }

    // A break in the packet-number sequence means we lost byte alignment.
    if (pnum != expectedPacket)
    {
        stats.desyncCount++;
        FLIR_VOSPI_ResetSync();
        FLIR_VOSPI_ArmPacket();
        return;
    }

    // Copy the payload out of the DMA landing buffer before re-arming.
    memcpy(&segScratch[pnum * FLIR_VOSPI_PACKET_PAYLOAD_BYTES],
           &vospiPacket[FLIR_VOSPI_PACKET_HEADER_BYTES],
           FLIR_VOSPI_PACKET_PAYLOAD_BYTES);

    // The segment number is only meaningful in packet 20.
    if (pnum == 20u)
    {
        uint8_t seg = (uint8_t)((hdr0 >> 4) & 0x07u);
        if ((seg < 1u) || (seg > FLIR_VOSPI_SEGMENTS_PER_FRAME))
        {
            // Segment not yet valid (camera still settling): drop it and wait
            // for a fresh segment start.
            stats.segmentErrors++;
            FLIR_VOSPI_ResetSync();
            FLIR_VOSPI_ArmPacket();
            return;
        }
        currentSegment = seg;
    }

    if (pnum == (FLIR_VOSPI_PACKETS_PER_SEGMENT - 1u))
    {
        // Segment complete. Only assemble frames from segments seen in order
        // starting at segment 1, so a mid-frame lock-on doesn't stitch a torn
        // frame together.
        if (currentSegment == 1u)
        {
            segmentMask = 0x02u;   // bit for segment 1
            FLIR_VOSPI_PlaceSegment(1u);
        }
        else if (segmentMask & (1u << (currentSegment - 1u)))
        {
            // Previous segment in the run was placed -> this one continues it.
            segmentMask |= (1u << currentSegment);
            FLIR_VOSPI_PlaceSegment(currentSegment);

            if ((currentSegment == FLIR_VOSPI_SEGMENTS_PER_FRAME) &&
                (segmentMask == 0x1Eu))   // segments 1,2,3,4 all placed
            {
                FLIR_VOSPI_CompleteFrame();
                segmentMask = 0;
            }
        }
        else
        {
            // Out-of-order segment: wait for the next segment 1.
            segmentMask = 0;
        }

        expectedPacket = 0;
        currentSegment = 0;
    }
    else
    {
        expectedPacket++;
    }

    FLIR_VOSPI_ArmPacket();
}

// VSYNC rising edge from the Lepton (GPIO3 -> RD0 -> INT1). Frame-timing tick;
// the capture itself is DMA-driven, so this only keeps a count for now.
void __ISR(_EXTERNAL_1_VECTOR, IPL4SRS) flirVsyncISR(void)
{
    stats.vsyncCount++;
    clearInterruptFlag(external_interrupt_1);
}

static uint32_t FLIR_VOSPI_GetBusSpeed(void)
{
    return FLIR_VOSPI_PBCLK_HZ / (2u * ((uint32_t)SPI4BRG + 1u));
}

void FLIR_VOSPI_PrintStatus(void)
{
    // MCU-peripheral view only (the SPI4/DMA/INT1 register settings behind the
    // VoSPI capture). Lepton module state and capture statistics live in
    // FLIR_PrintStatus() ("FLIR Status?").
    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, BOLD_FONT);
    printf("    --- FLIR VoSPI MCU Peripherals (SPI4 / DMA / INT1) ---\n\r");

    // SPI4 controller.
    if (SPI4CONbits.ON) terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    else terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("    SPI4 Module: %s\n\r", SPI4CONbits.ON ? "enabled" : "disabled");

    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("    SPI4 Mode: master=%u, mode 3 (CKP=%u CKE=%u SMP=%u), %u-bit\n\r",
           (unsigned)SPI4CONbits.MSTEN, (unsigned)SPI4CONbits.CKP,
           (unsigned)SPI4CONbits.CKE, (unsigned)SPI4CONbits.SMP,
           SPI4CONbits.MODE32 ? 32u : (SPI4CONbits.MODE16 ? 16u : 8u));
    printf("    SPI4BRG: %u -> %lu Hz (PBCLK2 %lu Hz)\n\r",
           (unsigned)SPI4BRG, (unsigned long)FLIR_VOSPI_GetBusSpeed(),
           (unsigned long)FLIR_VOSPI_PBCLK_HZ);
    printf("    Enhanced buffer: %u  STXISEL=%u  SRXISEL=%u\n\r",
           (unsigned)SPI4CONbits.ENHBUF, (unsigned)SPI4CONbits.STXISEL,
           (unsigned)SPI4CONbits.SRXISEL);

    if (SPI4STATbits.SPIROV) terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
    else terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("    SPI4 receive overflow: %s\n\r", SPI4STATbits.SPIROV ? "occurred" : "none");

    // DMA channels.
    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("    DMA RX (ch2): %s  CHSIRQ=%u(SPI4RX)  cell=%uB  block=%uB\n\r",
           DCH2CONbits.CHEN ? "enabled" : "disabled", (unsigned)DCH2ECONbits.CHSIRQ,
           (unsigned)DCH2CSIZ, (unsigned)DCH2DSIZ);
    printf("    DMA TX (ch3): %s  CHSIRQ=%u(SPI4TX)  cell=%uB  block=%uB\n\r",
           DCH3CONbits.CHEN ? "enabled" : "disabled", (unsigned)DCH3ECONbits.CHSIRQ,
           (unsigned)DCH3CSIZ, (unsigned)DCH3SSIZ);

    // Chip select GPIO and the VSYNC external interrupt.
    printf("    Chip Select (RD1, GPIO): %s\n\r", nFLIR_VOSPI_CS_PIN ? "deasserted" : "asserted");
    printf("    INT1 (VSYNC/RD0): %s, %s edge, priority %u\n\r",
           getInterruptEnable(external_interrupt_1) ? "enabled" : "disabled",
           INTCONbits.INT1EP ? "rising" : "falling",
           (unsigned)getInterruptPriority(external_interrupt_1));

    terminalTextAttributesReset();
}
