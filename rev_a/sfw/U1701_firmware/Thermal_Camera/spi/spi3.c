/*******************************************************************************
  SPI3 Master Driver

  File Name:
    spi3.c

  Summary:
    Blocking driver for the SPI3 peripheral in master mode. See spi3.h for
    why this has no queue (unlike i2c_master.h) and no chip-select handling.
*******************************************************************************/

#include "spi/spi3.h"
#include "core/device_control.h"
#include "core/32mzda_interrupt_control.h"
#include "usb_uart/terminal_control.h"
#include <xc.h>
#include <sys/attribs.h>
#include <sys/kmem.h>

#include <stdio.h>

// SPI3 is clocked from PBCLK2, which PBCLK2Initialize() (device_control.c)
// divides down from SYSCLK by 3 (66.67 MHz on this board) -- same source as
// I2C1 (see I2C_PBCLK_HZ in i2c_master.c).
#define SPI3_PBCLK_HZ                (SYSCLK_INT / 3u)

// sst25vf080b.c reads with Fast Read (0Bh), not plain Read (03h), so every
// instruction this driver issues tolerates the SST25VF080B's full clock
// range (66/80 MHz per its datasheet Table 7-1) -- this bound reflects the
// PART's rating, not what's been proven reliable on THIS board.
#define SPI3_MAX_CLK_HZ              66000000UL

// REGRESSION 2026-07-20: raising this to the PBCLK2/2 (~33.3MHz) ceiling
// broke JEDEC ID readback on real hardware (SST25VF080B_Verify() failed,
// "SPI Flash FAILED to initialize" at boot) -- that path is plain
// SPI3_TransferByte(), untouched by the Fast Read change, so the clock
// increase itself is the cause, most likely board-level signal integrity
// (trace length/no termination) rather than anything the part's own
// datasheet rating would predict. Reverted to the original, long-proven
// 10MHz default. If you want to try raising this again, step it up
// incrementally on the bench with a scope on SCK/SDI rather than jumping
// straight to the calculated ceiling.
#define SPI3_DEFAULT_CLK_HZ          10000000UL

// Gates SPI3_TransferBlock()'s DMA fast path (DCH2/DCH3, see the "DMA-
// accelerated bulk transfer" section below) and its interrupt setup.
// Confirmed working on real hardware 2026-07-20 with SPI3_DEFAULT_CLK_HZ
// at the 10MHz above -- see [[spi3-flash-speed-and-dma]] memory. Flip to
// 0 to fall back to the plain SPI3_TransferByte() loop if this is ever
// suspected again.
#define SPI3_DMA_ENABLED   1

#if SPI3_DMA_ENABLED
// Defined in the "DMA-accelerated bulk transfer" section below; forward
// declared here so SPI3_Initialize() can call it once at bring-up.
static void SPI3_DMAInterruptSetup(void);
#endif

bool SPI3_TransferSetup(SPI3_TRANSFER_SETUP *setup, uint32_t srcClkFreq)
{
    uint32_t baudValue;
    uint32_t spiClkSpeed;

    if (setup == NULL)
    {
        return false;
    }

    spiClkSpeed = setup->clkSpeed;

    if ((spiClkSpeed == 0) || (spiClkSpeed > SPI3_MAX_CLK_HZ))
    {
        return false;
    }

    if (srcClkFreq == 0)
    {
        srcClkFreq = SPI3_PBCLK_HZ;
    }

    // Fsck = PBCLK / (2 * (BRG + 1))  =>  BRG = PBCLK / (2 * Fsck) - 1
    baudValue = (srcClkFreq / (2u * spiClkSpeed)) - 1u;

    if (baudValue > 0x1FFu)
    {
        return false;
    }

    SPI3BRG = baudValue;

    return true;
}

bool SPI3_Initialize(void)
{
    SPI3_TRANSFER_SETUP setup = { SPI3_DEFAULT_CLK_HZ };

    /* Disable the module while it's (re)configured */
    SPI3CONbits.ON = 0;

    /* Clear the receive buffer / overflow flag left over from any prior state */
    SPI3STATbits.SPIROV = 0;
    (void)SPI3BUF;

    if (!SPI3_TransferSetup(&setup, 0))
    {
        return false;
    }

    SPI3CONbits.SIDL = 0;      // continue running in CPU Idle mode
    SPI3CONbits.MSTEN = 1;     // master mode

    // SPI Mode 0 (CPOL=0, CPHA=0), which the SST25VF080B supports (along
    // with Mode 3). PIC32's CKE is inverted relative to standard CPHA:
    // CKP=0/CKE=1 here is Mode 0, not Mode 1.
    SPI3CONbits.CKP = 0;
    SPI3CONbits.CKE = 1;
    SPI3CONbits.SMP = 0;       // sample input at the middle of the data output time

    SPI3CONbits.MODE32 = 0;    // 8-bit word size (both MODE32 and MODE16 clear)
    SPI3CONbits.MODE16 = 0;

    SPI3CONbits.SSEN = 0;      // chip select is a plain GPIO owned by the device
    SPI3CONbits.MSSEN = 0;     // driver (nFLASH_SPI_CS_PIN), not the SPI module

    SPI3CONbits.ENHBUF = 0;    // legacy (non-FIFO) buffer mode -- polled via SPIRBF/SPITBE
    SPI3CONbits.DISSDO = 0;
    SPI3CONbits.DISSDI = 0;
    SPI3CONbits.MCLKSEL = 0;   // clock source is PBCLK (not REFCLK)
    SPI3CONbits.FRMEN = 0;     // framed sync mode disabled

    SPI3CONbits.ON = 1;

#if SPI3_DMA_ENABLED
    SPI3_DMAInterruptSetup();
#endif

    return (SPI3CONbits.ON == 1);
}

uint8_t SPI3_TransferByte(uint8_t data)
{
    /* Wait for the transmit buffer to be free before loading the next byte */
    while (!SPI3STATbits.SPITBE);

    SPI3BUF = data;

    /* Wait for the byte shifted in during that same transfer to arrive */
    while (!SPI3STATbits.SPIRBF);

    return (uint8_t)SPI3BUF;
}

// --- DMA-accelerated bulk transfer -----------------------------------
//
// DCH2 (TX) and DCH3 (RX) -- DCH0/DCH1 are already claimed by
// usb_uart.c's console DMA (see its file header). Interrupt-driven,
// mirroring usb_uart.c's DMA ISR pattern: SPI3_DMAInterruptSetup() (called
// once from SPI3_Initialize()) registers spi3TxDmaISR()/spi3RxDmaISR() at
// IPL1/IPL2 (same levels usb_uart.c uses for its TX/RX DMA, respectively)
// with CHBCIE/CHERIE enabled; the ISRs just latch CHBCIF/CHERIF into the
// software flags below and clear the hardware, same minimal-ISR shape as
// sdhcISR() (sdhc.c). SPI3_TransferBlockDMA() waits on those flags instead
// of polling DCH3INTbits.CHBCIF directly -- the actual byte-shuttling
// between SPI3BUF and RAM still happens entirely in the DMA engine either
// way; this just lets the CPU be interrupted rather than spin-poll a
// register while it waits.
#define SPI3_TX_DMA_CON_BITFIELD    DCH2CONbits
#define SPI3_TX_DMA_ECON_BITFIELD   DCH2ECONbits
#define SPI3_TX_DMA_INT_BITFIELD    DCH2INTbits
#define SPI3_TX_DMA_INTCLR_REG      DCH2INTCLR
#define SPI3_TX_DMA_SSA_REG         DCH2SSA
#define SPI3_TX_DMA_DSA_REG         DCH2DSA
#define SPI3_TX_DMA_SSIZ_REG        DCH2SSIZ
#define SPI3_TX_DMA_DSIZ_REG        DCH2DSIZ
#define SPI3_TX_DMA_CSIZ_REG        DCH2CSIZ
#define SPI3_TX_DMA_INT_SOURCE      dma_channel_2
#define SPI3_TX_DMA_INT_VECTOR      _DMA2_VECTOR

#define SPI3_RX_DMA_CON_BITFIELD    DCH3CONbits
#define SPI3_RX_DMA_ECON_BITFIELD   DCH3ECONbits
#define SPI3_RX_DMA_INT_BITFIELD    DCH3INTbits
#define SPI3_RX_DMA_INTCLR_REG      DCH3INTCLR
#define SPI3_RX_DMA_SSA_REG         DCH3SSA
#define SPI3_RX_DMA_DSA_REG         DCH3DSA
#define SPI3_RX_DMA_SSIZ_REG        DCH3SSIZ
#define SPI3_RX_DMA_DSIZ_REG        DCH3DSIZ
#define SPI3_RX_DMA_CSIZ_REG        DCH3CSIZ
#define SPI3_RX_DMA_INT_SOURCE      dma_channel_3
#define SPI3_RX_DMA_INT_VECTOR      _DMA3_VECTOR

// Below this length, DMA channel setup/teardown overhead exceeds
// whatever it would save over the plain byte loop -- SPI3_TransferBlock()
// only takes the DMA path at or above this many bytes.
#define SPI3_DMA_MIN_LENGTH   32u

// Largest single transfer this driver's only bulk caller ever issues:
// SST25VF080B_Read() reading one 4KB NOR flash sector at a time
// (flash_fileio.c's page-at-a-time disk I/O). Sized to that specific,
// known ceiling, not a generic maximum -- see spi3_dma_tx_dummy[] below.
#define SPI3_DMA_MAX_LENGTH   4096u

// All-zero filler the TX channel feeds into SPI3BUF while the RX channel
// captures real data into the caller's buffer. SPI is full-duplex, so
// *something* has to go out on SDO3 to generate the clock edges that
// shift bytes in on SDI3 -- this driver's only DMA use case (a flash
// read) doesn't care what that content is, only the SST25VF080B's
// response on SDI3 matters. __attribute__((coherent)) matches this
// project's other DMA source-buffer convention (usb_uart.c's TX/RX
// buffers, sdhc.c's ADMA2 descriptor table); this content never changes
// after zero-init, so coherency here is about not having to think about
// cache state rather than an active hazard.
#if SPI3_DMA_ENABLED
static __attribute__((coherent)) uint8_t spi3_dma_tx_dummy[SPI3_DMA_MAX_LENGTH];

#define SPI3_DMA_TIMEOUT_TICKS   ((uint32_t)(((uint64_t)SYSCLK_INT / 2u) * 50000u / 1000000u))  // 50ms

// Set by spi3RxDmaISR()/spi3TxDmaISR(), consumed by the completion poll in
// SPI3_TransferIsBusy() -- the interrupt-driven equivalent of sdhc.c's
// sdhc_isr_events accumulator. Reset right before arming the channels for
// a new transfer (nothing is in flight at that point, so no event can be
// lost).
static volatile bool spi3_dma_rx_done = false;
static volatile bool spi3_dma_error = false;

// In-flight transfer bookkeeping, shared by the async API and the
// blocking wrapper built on it. rxBuffer/rxLength are retained from arm
// time so the D-cache invalidate can run at COMPLETION time (the only
// point at which the DMA engine is done writing the buffer). startTick
// bounds a wedged transfer; lastResult survives finalization so
// SPI3_TransferGetResult() can report it after the fact.
static bool      spi3_dma_in_flight = false;
static uint8_t  *spi3_dma_rx_buffer = NULL;
static size_t    spi3_dma_rx_length = 0;
static uint32_t  spi3_dma_start_tick = 0;
static bool      spi3_dma_last_result = false;

// Minimal ISRs (see the "DMA-accelerated bulk transfer" section header
// comment): latch the channel's own status into the software flags above,
// then clear the channel interrupt-status register and the CPU interrupt
// flag. No SPI/flash protocol logic runs here.
void __ISR(SPI3_TX_DMA_INT_VECTOR, IPL1SRS) spi3TxDmaISR(void)
{
    if (SPI3_TX_DMA_INT_BITFIELD.CHERIF)
    {
        spi3_dma_error = true;
    }

    SPI3_TX_DMA_INTCLR_REG = 0x000000FFu;
    clearInterruptFlag(SPI3_TX_DMA_INT_SOURCE);
}

void __ISR(SPI3_RX_DMA_INT_VECTOR, IPL2SRS) spi3RxDmaISR(void)
{
    if (SPI3_RX_DMA_INT_BITFIELD.CHBCIF)
    {
        spi3_dma_rx_done = true;
    }
    else if (SPI3_RX_DMA_INT_BITFIELD.CHERIF)
    {
        spi3_dma_error = true;
    }

    SPI3_RX_DMA_INTCLR_REG = 0x000000FFu;
    clearInterruptFlag(SPI3_RX_DMA_INT_SOURCE);
}

// One-time bring-up, called from SPI3_Initialize(): enables the Block-
// Complete and Error interrupts (CHBCIE/CHERIE) at the channel level and
// registers spi3TxDmaISR()/spi3RxDmaISR() with the CPU interrupt
// controller. These channel-level enables and the CPU-level priority/
// enable both persist across the CHEN=0/CHEN=1 toggling
// SPI3_TransferBlockDMA() does on every call, so this only needs to run
// once -- matching usb_uart.c's usbUartTrasmitDmaInitialize()/
// usbUartReceiveDmaInitialize(), which set CHBCIE/CHERIE once at their own
// one-time init rather than per-transfer.
static void SPI3_DMAInterruptSetup(void)
{
    disableInterrupt(SPI3_TX_DMA_INT_SOURCE);
    disableInterrupt(SPI3_RX_DMA_INT_SOURCE);

    SPI3_TX_DMA_INTCLR_REG = 0x000000FFu;
    SPI3_TX_DMA_INT_BITFIELD.CHBCIE = 1;
    SPI3_TX_DMA_INT_BITFIELD.CHERIE = 1;

    SPI3_RX_DMA_INTCLR_REG = 0x000000FFu;
    SPI3_RX_DMA_INT_BITFIELD.CHBCIE = 1;
    SPI3_RX_DMA_INT_BITFIELD.CHERIE = 1;

    // Same IPL/subpriority levels usb_uart.c uses for its own TX/RX DMA
    // (IPL1/sub3 for TX, IPL2/sub3 for RX) -- sharing an existing,
    // already-proven IPL rather than inventing a new one.
    setInterruptPriority(SPI3_TX_DMA_INT_SOURCE, 1);
    setInterruptSubpriority(SPI3_TX_DMA_INT_SOURCE, 3);
    clearInterruptFlag(SPI3_TX_DMA_INT_SOURCE);
    enableInterrupt(SPI3_TX_DMA_INT_SOURCE);

    setInterruptPriority(SPI3_RX_DMA_INT_SOURCE, 2);
    setInterruptSubpriority(SPI3_RX_DMA_INT_SOURCE, 3);
    clearInterruptFlag(SPI3_RX_DMA_INT_SOURCE);
    enableInterrupt(SPI3_RX_DMA_INT_SOURCE);

    DMACONbits.ON = 1;
}

// D-cache line size (microAptiv/PIC32MZ-DA) and MIPS32 CACHE op-field
// encodings -- same values and same rationale as sdhc.c's
// SDHC_DCacheInvalidate() (see that file for the full explanation). rxData
// here (e.g. sst25vf080b.c's static readBuffer[]) is ordinary cached
// KSEG0 memory, not __attribute__((coherent)) like spi3_dma_tx_dummy[] --
// the RX DMA channel writes it via physical memory directly, bypassing
// the CPU entirely, so without this the caller's very next load of
// rxData could return stale cached bytes instead of what DCH3 just wrote.
#define SPI3_DCACHE_LINE_SIZE            SPI3_DMA_BUFFER_ALIGNMENT  // 16 bytes, spi3.h
#define SPI3_CACHE_OP_HIT_INVALIDATE_D   0x11u

static void SPI3_DCacheInvalidate(const void *addr, size_t length)
{
    uint32_t line = (uint32_t)addr & ~(SPI3_DCACHE_LINE_SIZE - 1u);
    uint32_t end = (uint32_t)addr + length;

    for (; line < end; line += SPI3_DCACHE_LINE_SIZE)
    {
        __asm__ __volatile__ ("cache %0, 0(%1)"
            : : "i" (SPI3_CACHE_OP_HIT_INVALIDATE_D), "r" (line) : "memory");
    }
    __asm__ __volatile__ ("sync" ::: "memory");
}

// Shared DMA-eligibility test for both the blocking and async entry
// points. See SPI3_TransferBlock()'s comment for why the alignment and
// length-granularity rules are a correctness requirement (cache-line
// rounding), not a performance preference.
static bool SPI3_DMAEligible(const uint8_t *txData, const uint8_t *rxData, size_t length)
{
    return (txData == NULL) && (rxData != NULL)
            && (length >= SPI3_DMA_MIN_LENGTH) && (length <= SPI3_DMA_MAX_LENGTH)
            && (((uintptr_t)rxData % SPI3_DCACHE_LINE_SIZE) == 0)
            && ((length % SPI3_DCACHE_LINE_SIZE) == 0);
}

// Arms DCH2/DCH3 for a `length`-byte capture from SPI3BUF into `rxData`,
// transmitting spi3_dma_tx_dummy as filler, and kicks the transfer off.
// Returns with the transfer RUNNING -- completion is observed separately
// (SPI3_TransferIsBusy()), which is what makes the async API possible.
// Caller must have already checked SPI3_DMAEligible() and that no other
// transfer is in flight.
static void SPI3_DMAArm(uint8_t *rxData, size_t length)
{
    DMACONbits.ON = 1;

    // SPI3_TransferByte() (used for the command/address/dummy bytes sent
    // just before every call here) only polls SPIRBF/SPITBE -- the
    // peripheral's own status bits -- and never touches the CPU-level
    // spi3_receive_done/spi3_transfer_done IFS flag bits, which are a
    // separate thing entirely and stay set until software clears them.
    // Since SPI3 has never used interrupts before this DMA path existed,
    // those flags carry a backlog of "set" from every byte this driver
    // has ever shifted. If left set, arming CHSIRQ/SIRQEN against an
    // already-set flag can fire the DMA channel immediately on enable --
    // before the real first byte ever shifts -- desyncing the TX/RX
    // channels from actual SPI3 activity and capturing garbage. Clear
    // both right before arming either channel.
    clearInterruptFlag(spi3_receive_done);
    clearInterruptFlag(spi3_transfer_done);

    // Nothing is in flight yet at this point, so no event can be lost by
    // clearing these now (see the comment on the flags' declaration).
    spi3_dma_rx_done = false;
    spi3_dma_error = false;

    // Drop any DIRTY cache lines covering the destination BEFORE the
    // engine starts writing it. Without this, a dirty line left over from
    // the CPU's own earlier stores to this buffer can be evicted (written
    // back) at an arbitrary later moment -- including partway through the
    // transfer -- landing stale bytes on top of what DMA already wrote.
    // Discarding rather than writing back is correct here: the buffer is
    // about to be overwritten wholesale, and the enforced alignment
    // guarantees every line lies entirely inside it.
    //
    // This hazard is latent on the blocking path (the CPU just spins, so
    // it touches almost no memory and rarely evicts anything) but becomes
    // much more likely on the async path, where the whole main loop runs
    // against the cache while the transfer is in flight.
    SPI3_DCacheInvalidate(rxData, length);

    // RX: SPI3BUF (fixed, 1 byte) -> rxData (grows to `length` bytes)
    SPI3_RX_DMA_CON_BITFIELD.CHEN = 0;
    SPI3_RX_DMA_CON_BITFIELD.CHPRI = 2;
    SPI3_RX_DMA_CON_BITFIELD.CHCHN = 0;
    SPI3_RX_DMA_ECON_BITFIELD.CHSIRQ = spi3_receive_done;
    SPI3_RX_DMA_ECON_BITFIELD.SIRQEN = 1;
    SPI3_RX_DMA_ECON_BITFIELD.PATEN = 0;
    SPI3_RX_DMA_SSA_REG = (uint32_t)KVA_TO_PA((void *)&SPI3BUF);
    SPI3_RX_DMA_DSA_REG = (uint32_t)KVA_TO_PA((void *)rxData);
    SPI3_RX_DMA_SSIZ_REG = 1;
    SPI3_RX_DMA_DSIZ_REG = (uint32_t)length;
    SPI3_RX_DMA_CSIZ_REG = 1;
    SPI3_RX_DMA_INTCLR_REG = 0x000000FFu;
    SPI3_RX_DMA_INT_BITFIELD.CHBCIF = 0;
    SPI3_RX_DMA_CON_BITFIELD.CHEN = 1;

    // TX: spi3_dma_tx_dummy (grows to `length` bytes) -> SPI3BUF (fixed)
    SPI3_TX_DMA_CON_BITFIELD.CHEN = 0;
    SPI3_TX_DMA_CON_BITFIELD.CHPRI = 2;
    SPI3_TX_DMA_CON_BITFIELD.CHCHN = 0;
    SPI3_TX_DMA_ECON_BITFIELD.CHSIRQ = spi3_transfer_done;
    SPI3_TX_DMA_ECON_BITFIELD.SIRQEN = 1;
    SPI3_TX_DMA_ECON_BITFIELD.PATEN = 0;
    SPI3_TX_DMA_SSA_REG = (uint32_t)KVA_TO_PA((void *)&spi3_dma_tx_dummy[0]);
    SPI3_TX_DMA_DSA_REG = (uint32_t)KVA_TO_PA((void *)&SPI3BUF);
    SPI3_TX_DMA_SSIZ_REG = (uint32_t)length;
    SPI3_TX_DMA_DSIZ_REG = 1;
    SPI3_TX_DMA_CSIZ_REG = 1;
    SPI3_TX_DMA_INTCLR_REG = 0x000000FFu;
    SPI3_TX_DMA_INT_BITFIELD.CHBCIF = 0;
    SPI3_TX_DMA_CON_BITFIELD.CHEN = 1;

    // SPITBE already reads set at idle (nothing queued yet), so the TX
    // channel's own trigger interrupt won't naturally edge for the very
    // first byte -- force it, same as usb_uart.c's TX DMA kick. Every
    // byte after that is paced by genuine SPITBE/SPIRBF edges as the
    // hardware shifts each byte in and out.
    SPI3_TX_DMA_ECON_BITFIELD.CFORCE = 1;

    spi3_dma_rx_buffer = rxData;
    spi3_dma_rx_length = length;
    spi3_dma_start_tick = _CP0_GET_COUNT();
    spi3_dma_in_flight = true;
}

// Tears the channels down, applies the receive buffer's D-cache
// invalidate, and latches the pass/fail result. Idempotent: safe to call
// again once the transfer has already been finalized (just re-reports the
// stored result), which is what lets SPI3_TransferIsBusy() and
// SPI3_TransferGetResult() both be called freely by a polling caller.
static bool SPI3_DMAFinalize(void)
{
    if (!spi3_dma_in_flight)
    {
        return spi3_dma_last_result;
    }

    SPI3_TX_DMA_CON_BITFIELD.CHEN = 0;
    SPI3_RX_DMA_CON_BITFIELD.CHEN = 0;

    bool success = spi3_dma_rx_done && !spi3_dma_error;

    // Only meaningful once the engine has stopped writing the buffer --
    // that's precisely why this is deferred to completion instead of
    // being done at arm time.
    if (success)
    {
        SPI3_DCacheInvalidate(spi3_dma_rx_buffer, spi3_dma_rx_length);
    }

    spi3_dma_in_flight = false;
    spi3_dma_rx_buffer = NULL;
    spi3_dma_rx_length = 0;
    spi3_dma_last_result = success;

    return success;
}

bool SPI3_TransferBlockAsync(uint8_t *rxData, size_t length)
{
    if (spi3_dma_in_flight || !SPI3_DMAEligible(NULL, rxData, length))
    {
        return false;
    }

    SPI3_DMAArm(rxData, length);
    return true;
}

bool SPI3_TransferIsBusy(void)
{
    if (!spi3_dma_in_flight)
    {
        return false;
    }

    // Consume the ISR-latched flags, NOT the DCH3INT SFR directly -- the
    // ISR W1C-clears its own interrupt-status bits as soon as it fires
    // (see spi3RxDmaISR()), so racing a second reader against that clear
    // is exactly the hazard sdhc.c's sdhc_isr_events comment documents.
    if (spi3_dma_rx_done || spi3_dma_error)
    {
        SPI3_DMAFinalize();
        return false;
    }

    // Bound a wedged transfer. Finalizing here reports failure (rx_done
    // was never set), so a stuck DMA surfaces as a failed transfer rather
    // than an async caller polling busy forever.
    if ((uint32_t)(_CP0_GET_COUNT() - spi3_dma_start_tick) >= SPI3_DMA_TIMEOUT_TICKS)
    {
        SPI3_DMAFinalize();
        return false;
    }

    return true;
}

bool SPI3_TransferGetResult(void)
{
    return spi3_dma_in_flight ? false : spi3_dma_last_result;
}

// Blocking convenience wrapper over the async API above -- both paths
// therefore share one arm/finalize implementation rather than having two
// copies of the channel setup to keep in sync.
//
// The wait here is a plain spin. An earlier revision offered a MIPS WAIT
// (CPU Idle) alternative to avoid burning cycles, but it was removed:
// idling only saves power, it cannot let the CPU run other work (the call
// is still synchronous), and it depended on unverified wake-from-Idle
// behavior with no rescue interrupt available (Timer1/Timer2 both set
// SIDL = 1 in core/heartbeat_timer.c, so they stop in Idle). Callers that
// actually want the CPU doing other work during a transfer use the async
// API above -- see spi/flash_async.h for a main-loop-driven consumer.
static bool SPI3_TransferBlockDMA(uint8_t *rxData, size_t length)
{
    if (!SPI3_TransferBlockAsync(rxData, length))
    {
        return false;
    }

    while (SPI3_TransferIsBusy())
    {
        // Interrupts stay enabled through this spin, so the DMA ISR (and
        // every other ISR) is serviced normally while it runs.
    }

    return SPI3_TransferGetResult();
}
#endif /* SPI3_DMA_ENABLED */

void SPI3_TransferBlock(const uint8_t *txData, uint8_t *rxData, size_t length)
{
    // DMA fast path: this driver's only bulk-transfer caller always
    // passes txData=NULL (it only wants the RX data; see
    // spi3_dma_tx_dummy[] for why that's what makes the DMA path
    // tractable without a real TX payload buffer). Falls back to the
    // byte loop for anything else -- arbitrary txData, no rxData, a
    // length outside the DMA path's sized/worthwhile range, a
    // misaligned/oddly-sized rxData (see below), or a DMA timeout.
    // The alignment/granularity half of SPI3_DMAEligible() is a
    // CORRECTNESS rule, not a performance one: SPI3_DCacheInvalidate()
    // operates on whole cache lines, so a buffer that doesn't start AND
    // end on a line boundary makes it round into memory OUTSIDE
    // [rxData, rxData+length) and discard (without writeback) whatever
    // dirty cache line shares that boundary with unrelated data. This is
    // exactly what corrupted sst25vf080b.c's SST25VF080B_SelfTest()
    // writeBuffer[] (plain `static uint8_t[]`, no alignment guarantee,
    // sitting adjacent to readBuffer[]) on 2026-07-20 -- readBuffer[]'s
    // DMA capture invalidated a line that also covered part of
    // writeBuffer[], silently reverting bytes the CPU had just written
    // there. Callers wanting the DMA speedup declare their buffer
    // __attribute__((aligned(SPI3_DMA_BUFFER_ALIGNMENT))), as
    // sst25vf080b.c's writeBuffer/readBuffer and sst25vf080b_disk.c's
    // staging[] now do.
#if SPI3_DMA_ENABLED
    if (SPI3_DMAEligible(txData, rxData, length) && SPI3_TransferBlockDMA(rxData, length))
    {
        return;
    }
#endif

    size_t i;

    for (i = 0; i < length; i++)
    {
        uint8_t rx = SPI3_TransferByte((txData != NULL) ? txData[i] : 0x00u);

        if (rxData != NULL)
        {
            rxData[i] = rx;
        }
    }
}

uint32_t SPI3_GetBusSpeed(void)
{
    return SPI3_PBCLK_HZ / (2u * ((uint32_t)SPI3BRG + 1u));
}

void SPI3_PrintStatus(void)
{
    uint32_t busSpeed = SPI3_GetBusSpeed();

    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, BOLD_FONT);
    printf("    --- SPI3 Controller ---\n\r");

    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("    PBCLK2 (SPI3 peripheral clock): %lu Hz\n\r", (unsigned long)SPI3_PBCLK_HZ);
    printf("    SPI3BRG: 0x%04X (%u)\n\r", (unsigned int)SPI3BRG, (unsigned int)SPI3BRG);
    printf("    Calculated bus speed: %lu Hz\n\r", (unsigned long)busSpeed);

    if (SPI3CONbits.ON) terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    else terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("    SPI3 Module is %s\n\r", SPI3CONbits.ON ? "enabled" : "disabled");

    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("    Master mode: %s\n\r", SPI3CONbits.MSTEN ? "enabled" : "disabled");
    printf("    Clock Polarity (CKP): idle %s\n\r", SPI3CONbits.CKP ? "high" : "low");
    printf("    Clock Edge (CKE): output changes on %s edge\n\r",
           SPI3CONbits.CKE ? "active-to-idle" : "idle-to-active");
    printf("    Sample Phase (SMP): input sampled at %s of output time\n\r",
           SPI3CONbits.SMP ? "end" : "middle");

    if (SPI3STATbits.SPIROV) terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
    else terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("    Receive overflow has %s\n\r", SPI3STATbits.SPIROV ? "occurred" : "not occurred");

    if (SPI3STATbits.SPIBUSY) terminalTextAttributes(YELLOW_COLOR, BLACK_COLOR, NORMAL_FONT);
    else terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("    Bus is currently: %s\n\r", SPI3STATbits.SPIBUSY ? "busy" : "idle");

    terminalTextAttributesReset();
}
