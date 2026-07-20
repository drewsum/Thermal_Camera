/*******************************************************************************
  USB Device Peripheral Driver (Hi-Speed USB / MUSB core)

  File Name:
    usb.c

  Summary:
    Register-level device-mode driver for the PIC32MZ DA's Hi-Speed USB
    module. See usb.h for the architecture (layering, clocking, forced
    session, ISR-latches/superloop-executes model).

  Description:
    Register access notes that shape this file:

    - The endpoint CSRs mix read-only, write-1-to-act, and
      write-0-to-clear bits in one register, and the DFP header's
      device-mode bitfield aliases for USBE1CSR1 disagree with the MUSB
      databook by one bit position -- so every endpoint-CSR access here
      uses explicit shifted masks (below) verified against the MUSB
      bit layout, never the header bitfields. Plain config fields
      (FUNC, ENDPOINT, FIFOSZ, RXCNT...) still use the bitfield names.

    - The EP1 CSR low halfword holds TXMAXP/RXMAXP, so flag writes to
      those registers always rebuild the register as
      (maxp | intended flags) rather than read-modify-write -- an RMW
      would re-write a latched write-0-to-clear flag (SENTSTALL,
      UNDERRUN) back to 1 and keep it set.

    - FIFO access is asymmetric at BYTE width (matches Microchip's
      Harmony USBHS driver, usbhs_EndpointFIFO_Default): byte WRITES all
      go to the low byte lane of USBFIFOn, but byte READS must never hit
      the same lane twice in a row -- the bridge serves a latched 32-bit
      word per lane, so byte unloads must rotate the lane with (i & 3).
      Fixed-lane byte reads return garbage (found the hard way: every
      SETUP parsed as junk and was stalled).

      USBFIFOn is however a 32-bit port, and WORD accesses sidestep that
      asymmetry entirely -- one access moves a whole word in either
      direction, so no lane bookkeeping applies. The bulk paths
      (USB_BulkInWrite/USB_BulkOutRead) therefore use 32-bit accesses
      whenever the buffer is 4-byte aligned and the length is a whole
      number of words, falling back to the byte loops otherwise. A single
      packet never mixes the two widths, which is what keeps the
      word path clear of the lane rules. EP0 is deliberately left on the
      byte path: control transfers are small and infrequent, and
      enumeration is not worth destabilizing for throughput that does not
      matter.
*******************************************************************************/

#include <xc.h>
#include <sys/attribs.h>
#include <stdio.h>
#include <string.h>

#include "usb/usb.h"
#include "usb/device_driver/usb_msd.h"
#include "usb/device_driver/usb_descriptors.h"
#include "core/32mzda_interrupt_control.h"
#include "usb_uart/terminal_control.h"

// ---- USBE0CSR0 flag masks (EP0 CSR0, device mode; bits 23:16 of the
// register = MUSB CSR0L). Low halfword is unused for EP0, so flag writes
// are direct assignments. ----
#define USB_E0_RXRDY    (1ul << 16)     // RxPktRdy (RO)
#define USB_E0_TXRDY    (1ul << 17)     // TxPktRdy (set to arm IN packet)
#define USB_E0_STALLED  (1ul << 18)     // SentStall (write 0 to clear)
#define USB_E0_DATAEND  (1ul << 19)     // DataEnd (set with last packet)
#define USB_E0_SETEND   (1ul << 20)     // SetupEnd (RO; host aborted early)
#define USB_E0_STALL    (1ul << 21)     // SendStall (protocol stall)
#define USB_E0_RXRDYC   (1ul << 22)     // ServicedRxPktRdy
#define USB_E0_SETENDC  (1ul << 23)     // ServicedSetupEnd
#define USB_E0_FLUSH    (1ul << 24)     // FlushFIFO

// ---- USBE1CSR0 flag masks (bulk IN / TX CSR, device mode; bits 23:16 =
// MUSB TXCSRL, bit 29 = TXCSRH.Mode) ----
#define USB_TX_TXPKTRDY (1ul << 16)     // set to arm the loaded packet
#define USB_TX_FIFONE   (1ul << 17)     // FIFO not empty (RO)
#define USB_TX_UNDERRUN (1ul << 18)     // write 0 to clear
#define USB_TX_FLUSH    (1ul << 19)     // only while TXPKTRDY is set
#define USB_TX_SENDSTALL (1ul << 20)
#define USB_TX_SENTSTALL (1ul << 21)    // write 0 to clear
#define USB_TX_CLRDT    (1ul << 22)     // clear data toggle
#define USB_TX_MODE     (1ul << 29)     // 1 = endpoint FIFO is TX

// ---- USBE1CSR1 flag masks (bulk OUT / RX CSR, device mode; bits 23:16 =
// MUSB RXCSRL -- these positions follow the MUSB databook, NOT the DFP
// header's device-mode union, which is off by one; see file header) ----
#define USB_RX_RXPKTRDY (1ul << 16)     // write 0 to release the packet
#define USB_RX_FIFOFULL (1ul << 17)     // RO
#define USB_RX_OVERRUN  (1ul << 18)     // write 0 to clear
#define USB_RX_DATAERR  (1ul << 19)     // RO
#define USB_RX_FLUSH    (1ul << 20)     // only while RXPKTRDY is set
#define USB_RX_SENDSTALL (1ul << 21)
#define USB_RX_SENTSTALL (1ul << 22)    // write 0 to clear
#define USB_RX_CLRDT    (1ul << 23)     // clear data toggle

// Interrupt-flag masks within the ISR's clear-on-read snapshots
#define USB_CSR0_EP0IF      (1ul << 16)
#define USB_CSR0_EP1TXIF    (1ul << 17)
#define USB_CSR1_EP1RXIF    (1ul << 1)
#define USB_CSR2_SUSPIF     (1ul << 16)
#define USB_CSR2_RESUMEIF   (1ul << 17)
#define USB_CSR2_RESETIF    (1ul << 18)
#define USB_CSR2_DISCONIF   (1ul << 21)
#define USB_CSR2_VBUSERRIF  (1ul << 23)

// ---- Endpoint FIFO RAM map ----
// USBOTG.TXFIFOSZ/RXFIFOSZ encoding: 0x6 = 512-byte packets. FIFOAD is in
// 8-byte units. Both bulk endpoints are double-buffered, so each reserves
// 2 x 512 = 1024 bytes:
//
//     0    ..   63   EP0 (fixed by hardware, 64B)
//     64   .. 1087   bulk IN  (2 x 512)
//     1088 .. 2111   bulk OUT (2 x 512)
//
// Total 2112 bytes. The core's FIFO RAM is 8 * 2^USBINFO.RAMBITS bytes
// (4KB when RAMBITS = 9), so this fits with room to spare -- the actual
// RAMBITS value is reported by USB_PrintStatus() to make that verifiable
// on the bench rather than assumed.
#define USB_FIFO_SZ_512         0x6u
#define USB_FIFO_BULK_TX_ADDR   64u
#define USB_FIFO_BULK_RX_ADDR   (USB_FIFO_BULK_TX_ADDR + 2u * 512u)
#define USB_FIFO_TOTAL_USED     (USB_FIFO_BULK_RX_ADDR + 2u * 512u)

// FIFO access windows. Byte-lane 0 is for WRITES only; reads must use
// 32-bit words or rotate lanes (see file header)
#define USB_FIFO0_BYTE  (*(volatile uint8_t *)&USBFIFO0)
#define USB_FIFO1_BYTE  (*(volatile uint8_t *)&USBFIFO1)
#define USB_FIFO1_LANES ((volatile uint8_t *)&USBFIFO1)

// Standard request codes (USB 2.0 table 9-4)
#define USB_REQ_GET_STATUS          0u
#define USB_REQ_CLEAR_FEATURE       1u
#define USB_REQ_SET_FEATURE         3u
#define USB_REQ_SET_ADDRESS         5u
#define USB_REQ_GET_DESCRIPTOR      6u
#define USB_REQ_GET_CONFIGURATION   8u
#define USB_REQ_SET_CONFIGURATION   9u
#define USB_REQ_GET_INTERFACE       10u
#define USB_REQ_SET_INTERFACE       11u
#define USB_FEATURE_ENDPOINT_HALT   0u

// EP0 control transfer state
typedef enum
{
    USB_EP0_IDLE = 0,   // awaiting SETUP
    USB_EP0_TX,         // IN data stage in progress
    USB_EP0_STATUS      // final packet armed; next event = status done
} usb_ep0_state_t;

static usb_ep0_state_t ep0_state = USB_EP0_IDLE;
static const uint8_t *ep0_tx_ptr = NULL;
static uint16_t ep0_tx_remaining = 0;
static bool ep0_tx_need_zlp = false;
static int16_t pending_address = -1;    // SET_ADDRESS deferred until status stage
static uint8_t current_configuration = 0;

// Scratch for descriptors that need patching before transmission
// (OTHER_SPEED_CONFIGURATION is the CONFIGURATION table with byte 1
// rewritten, per USB 2.0 9.6.4)
static uint8_t ep0_desc_scratch[USB_CONFIG_DESC_TOTAL_LENGTH];

// Bulk max packet for the negotiated speed; the low halfword base every
// EP1 CSR flag write is rebuilt from (see file header)
static uint16_t usb_bulk_maxp = USB_BULK_MAX_PACKET_HS;

#define USB_TX_CSR_BASE()   ((uint32_t)usb_bulk_maxp | USB_TX_MODE)
#define USB_RX_CSR_BASE()   ((uint32_t)usb_bulk_maxp)

// True once enableInterrupt(usb_general_event) has run -- gates the
// mask/unmask in the latching accessors below so init-time use of them
// can't enable the interrupt prematurely
static bool usb_isr_armed = false;

static void usbConfigureEndpoints(void);
static void usbServiceEp0(void);
static void usbHandleSetupPacket(void);
static void usbHandleStandardRequest(const usb_setup_packet_t *setup);
static void usbEp0StartTx(const uint8_t *data, uint16_t length, uint16_t wLength);
static void usbEp0ContinueTx(void);
static void usbEp0AckNoData(void);
static void usbEp0Stall(void);

// ---- Latching accessors for USBCSR0 ----
// The EP0IF/EPnTXIF field (bits 23:16) is CLEAR-ON-READ, and every access
// to any USBCSR0 field -- including a bitfield write like
// USBCSR0bits.FUNC = x, which compiles to a 32-bit read-modify-write --
// reads the whole register. A task-context access would therefore
// silently eat endpoint events the ISR hasn't latched yet. These helpers
// are the only permitted task-context access to USBCSR0: they latch any
// flags they swallow into usb_pending_csr0 exactly as usbISR() would.
// (USBCSR1/2 have the same property but are only written before the
// interrupt is armed, where a clear-on-read is harmless.)

static uint32_t usbReadCsr0(void)
{
    if (usb_isr_armed)
    {
        disableInterrupt(usb_general_event);
    }

    uint32_t value = USBCSR0;
    if (value & 0x00FF0000ul)
    {
        usb_pending_csr0 |= (value & 0x00FF0000ul);
        usb_event_pending = 1;
    }

    if (usb_isr_armed)
    {
        enableInterrupt(usb_general_event);
    }

    return value;
}

static void usbModifyCsr0(uint32_t clearMask, uint32_t setMask)
{
    if (usb_isr_armed)
    {
        disableInterrupt(usb_general_event);
    }

    uint32_t value = USBCSR0;
    if (value & 0x00FF0000ul)
    {
        usb_pending_csr0 |= (value & 0x00FF0000ul);
        usb_event_pending = 1;
    }

    // IF bits are read-only (writes ignored) but write them as 0 anyway
    USBCSR0 = ((value & ~clearMask) | setMask) & ~0x00FF0000ul;

    if (usb_isr_armed)
    {
        enableInterrupt(usb_general_event);
    }
}

// USBCSR0 non-IF field masks for the accessors above
#define USB_CSR0_FUNC_MASK      0x0000007Ful
#define USB_CSR0_SUSPMODE       (1ul << 9)
#define USB_CSR0_HSMODE         (1ul << 12)
#define USB_CSR0_HSEN           (1ul << 13)
#define USB_CSR0_SOFTCONN       (1ul << 14)

bool USB_Initialize(void)
{
    // PMD is one-shot (power_saving.c) -- if the module was disabled
    // there, nothing this function writes will stick. Fail loudly.
    if (PMD5bits.USBMD)
    {
        return false;
    }

    USB_MSD_Initialize();

    // Detached while configuring
    usbModifyCsr0(USB_CSR0_SOFTCONN, 0);

    // Soft-reset the MUSB core (NRST | NRSTX, hardware self-clearing) and
    // wait for completion, exactly as Microchip's Harmony USBHS driver
    // does before any module configuration on this family. Bounded wait:
    // ~10ms at the CP0 rate (SYSCLK/2 = 100MHz), then give up. RMW
    // preserves the HS/FS/LS end-of-frame fields in the low bytes.
    USBEOFRST = (USBEOFRST & 0x00FFFFFFul) | (1ul << 24) | (1ul << 25);
    {
        uint32_t start = _CP0_GET_COUNT();
        while ((USBEOFRST & 0xFF000000ul) != 0u)
        {
            if ((uint32_t)(_CP0_GET_COUNT() - start) > 1000000ul)
            {
                return false;   // core never came out of soft reset
            }
        }
    }

    // Device (B-device) role via ID override -- the port is hardwired to
    // a hub downstream port, so the ID value is forced rather than
    // sensed. PHYIDEN=1 is required even with the override: it is what
    // routes the ID/role indication into the PHY, and without it the PHY
    // never engages (no D+ pull-up appears on the bus even with SOFTCONN
    // set). This matches Harmony's PIC32MZ DA device-mode sequence
    // (USBIDOVEN=1, PHYIDEN=1, USBIDVAL=1), found the hard way during
    // rev A bring-up.
    USBCRCONbits.USBIDOVEN = 1;
    USBCRCONbits.PHYIDEN = 1;
    USBCRCONbits.USBIDVAL = 1;

#if USB_FORCE_SESSION
    // VBUS pin wiring unverified on rev A (usb.h) -- leave every VBUS
    // comparator monitor off and force the session below, so the core
    // treats the bus as always powered.
    USBCRCONbits.VBUSMONEN = 0;
    USBCRCONbits.ASVALMONEN = 0;
    USBCRCONbits.BSVALMONEN = 0;
    USBCRCONbits.SENDMONEN = 0;
#else
    USBCRCONbits.VBUSMONEN = 1;
    USBCRCONbits.ASVALMONEN = 1;
    USBCRCONbits.BSVALMONEN = 1;
    USBCRCONbits.SENDMONEN = 1;
#endif

    // Module-level interrupt gate to the interrupt controller
    USBCRCONbits.USBIE = 1;

    // Endpoint and bus-event interrupt enables. SOF/CONN/SESSRQ stay off:
    // SOF fires every 125us in HS and would waste the superloop, CONN and
    // SESSRQ are host-mode events.
    USBCSR1bits.EP0IE = 1;
    USBCSR1bits.EP1TXIE = 1;
    USBCSR2bits.EP1RXIE = 1;
    USBCSR2bits.RESETIE = 1;
    USBCSR2bits.SUSPIE = 1;
    USBCSR2bits.RESUMEIE = 1;
    USBCSR2bits.DISCONIE = 1;
    USBCSR2bits.VBUSERRIE = 1;
    USBCSR2bits.SOFIE = 0;
    USBCSR2bits.CONNIE = 0;
    USBCSR2bits.SESSRQIE = 0;

    usbConfigureEndpoints();

    setInterruptPriority(usb_general_event, 2);
    setInterruptSubpriority(usb_general_event, 0);
    clearInterruptFlag(usb_general_event);
    enableInterrupt(usb_general_event);
    usb_isr_armed = true;

#if USB_FORCE_SESSION
    USBOTGbits.SESSION = 1;
#endif

    // Request Hi-Speed (chirp during the next bus reset; falls back to
    // Full-Speed automatically -- USBCSR0.HSMODE reports the outcome),
    // then present to the host
    usbModifyCsr0(0, USB_CSR0_HSEN | USB_CSR0_SOFTCONN);
    usb_device_state = USB_STATE_ATTACHED;

    return true;
}

// (Re)builds the endpoint FIFO map and bulk endpoint CSRs. Called at init
// and on every bus reset -- by reset time USBCSR0.HSMODE is valid, so this
// is also where the bulk max packet size locks to 512 (HS) or 64 (FS).
// The FIFOs stay 512B/packet even in FS mode; only MAXP shrinks.
//
// Both bulk endpoints are DOUBLE-buffered (TXDPB/RXDPB = 1). FIFOSZ
// specifies the PACKET size, not the allocation -- with DPB set the core
// reserves 2x that, so each bulk endpoint costs 1024 bytes of FIFO RAM
// while still declaring a 512-byte packet. This is what lets the host
// stream back-to-back packets: hardware clears TXPKTRDY as soon as there
// is room for another packet rather than waiting for the in-flight one to
// be ACKed, and a second bulk OUT packet can land while firmware is still
// unloading the first. Single-buffered, throughput was gated on superloop
// latency instead of on the wire.
static void usbConfigureEndpoints(void)
{
    usb_bulk_maxp = (usbReadCsr0() & USB_CSR0_HSMODE) ? USB_BULK_MAX_PACKET_HS
                                                      : USB_BULK_MAX_PACKET_FS;

    // Dynamic FIFO sizing registers are indexed through USBCSR3.ENDPOINT
    USBCSR3bits.ENDPOINT = USB_BULK_EP_NUM;
    USBOTGbits.TXFIFOSZ = USB_FIFO_SZ_512;
    USBOTGbits.TXDPB = 1;                       // double-buffered
    USBFIFOAbits.TXFIFOAD = USB_FIFO_BULK_TX_ADDR / 8u;
    USBOTGbits.RXFIFOSZ = USB_FIFO_SZ_512;
    USBOTGbits.RXDPB = 1;
    USBFIFOAbits.RXFIFOAD = USB_FIFO_BULK_RX_ADDR / 8u;
    USBCSR3bits.ENDPOINT = 0;

    // Fresh CSRs: direction, max packet, data toggles reset, no stalls
    USBE1CSR0 = USB_TX_CSR_BASE() | USB_TX_CLRDT;
    USBE1CSR1 = USB_RX_CSR_BASE() | USB_RX_CLRDT;
}

void __ISR(_USB_VECTOR, IPL2SRS) usbISR(void)
{
    // The one and only read of the clear-on-read interrupt-flag fields
    // (usb.h file header) -- latch and get out
    uint32_t csr0 = USBCSR0;
    uint32_t csr1 = USBCSR1;
    uint32_t csr2 = USBCSR2;

    usb_pending_csr0 |= (csr0 & 0x00FF0000ul);  // EP0IF/EPnTXIF
    usb_pending_csr1 |= (csr1 & 0x000000FEul);  // EPnRXIF
    usb_pending_csr2 |= (csr2 & 0x00FF0000ul);  // bus events
    usb_event_pending = 1;

    clearInterruptFlag(usb_general_event);
}

void USB_Tasks(void)
{
    if (!usb_event_pending)
    {
        return;
    }

    // Atomically take this pass's events (the ISR ORs into these)
    disableInterrupt(usb_general_event);
    uint32_t p0 = usb_pending_csr0;
    uint32_t p1 = usb_pending_csr1;
    uint32_t p2 = usb_pending_csr2;
    usb_pending_csr0 = 0;
    usb_pending_csr1 = 0;
    usb_pending_csr2 = 0;
    usb_event_pending = 0;
    enableInterrupt(usb_general_event);

    // Bus events first, so a reset/resume reshapes state before any
    // endpoint traffic from the same pass is interpreted
    if (p2 & USB_CSR2_RESETIF)
    {
        usb_counters.bus_resets++;
        usbModifyCsr0(USB_CSR0_FUNC_MASK, 0);
        pending_address = -1;
        current_configuration = 0;
        ep0_state = USB_EP0_IDLE;
        usb_device_state = USB_STATE_DEFAULT;
        usbConfigureEndpoints();
        USB_MSD_BusResetHook();
    }

    if (p2 & USB_CSR2_DISCONIF)
    {
        usb_counters.disconnects++;
        current_configuration = 0;
        ep0_state = USB_EP0_IDLE;
        usb_device_state = USB_STATE_ATTACHED;
        USB_MSD_DetachHook();
    }

    if (p2 & USB_CSR2_SUSPIF)
    {
        usb_counters.suspends++;
        // With a forced session a cable unplug is only visible as a
        // suspend (usb.h), so treat it as a detach for media ownership;
        // a genuinely suspended-then-resumed host gets the media
        // re-yielded by the resume hook below
        USB_MSD_DetachHook();
    }

    if (p2 & USB_CSR2_RESUMEIF)
    {
        usb_counters.resumes++;
        if (usb_device_state == USB_STATE_CONFIGURED)
        {
            USB_MSD_ResumeHook();
        }
    }

    if (p2 & USB_CSR2_VBUSERRIF)
    {
        usb_counters.vbus_errors++;
    }

    if (p0 & USB_CSR0_EP0IF)
    {
        usbServiceEp0();
    }

    USB_MSD_Tasks((p0 & USB_CSR0_EP1TXIF) != 0, (p1 & USB_CSR1_EP1RXIF) != 0);
}

// One EP0 event: order matters -- abort conditions, then status-stage
// completion bookkeeping, then a fresh SETUP, then IN-data continuation.
// A single event can legitimately carry more than one of these (e.g. the
// status-complete of the previous transfer coalesced with the next SETUP's
// RXRDY).
static void usbServiceEp0(void)
{
    uint32_t csr = USBE0CSR0;

    if (csr & USB_E0_SETEND)
    {
        // Host moved on before the transfer finished -- acknowledge and
        // abandon whatever was in flight
        USBE0CSR0 = USB_E0_SETENDC;
        ep0_state = USB_EP0_IDLE;
        pending_address = -1;
    }

    if (csr & USB_E0_STALLED)
    {
        // Stall handshake was delivered; clear the latch (write-0) and
        // return to idle
        USBE0CSR0 = 0;
        ep0_state = USB_EP0_IDLE;
    }

    if ((ep0_state == USB_EP0_STATUS) && !(csr & USB_E0_TXRDY))
    {
        // Status stage of the previous transfer completed. This is the
        // one legal moment to load a SET_ADDRESS address (MUSB rule:
        // FUNC must not change until the status stage is done).
        if (pending_address >= 0)
        {
            usbModifyCsr0(USB_CSR0_FUNC_MASK, (uint32_t)pending_address & 0x7Ful);
            usb_device_state = (pending_address > 0) ? USB_STATE_ADDRESSED
                                                     : USB_STATE_DEFAULT;
            pending_address = -1;
        }
        ep0_state = USB_EP0_IDLE;
    }

    if ((csr & USB_E0_RXRDY) && (ep0_state == USB_EP0_IDLE))
    {
        usbHandleSetupPacket();
    }
    else if ((ep0_state == USB_EP0_TX) && !(csr & USB_E0_TXRDY))
    {
        usbEp0ContinueTx();
    }
}

static void usbHandleSetupPacket(void)
{
    usb_setup_packet_t setup;

    usb_counters.setup_packets++;

    // Unload the 8-byte SETUP packet as two 32-bit word reads (file
    // header: FIFO reads must not repeat a byte lane)
    uint32_t w0 = USBFIFO0;
    uint32_t w1 = USBFIFO0;

    setup.bmRequestType = (uint8_t)(w0 & 0xFFu);
    setup.bRequest = (uint8_t)((w0 >> 8) & 0xFFu);
    setup.wValue = (uint16_t)(w0 >> 16);
    setup.wIndex = (uint16_t)(w1 & 0xFFFFu);
    setup.wLength = (uint16_t)(w1 >> 16);

    uint8_t type = (setup.bmRequestType >> 5) & 0x3u;

    if (type == 0u)
    {
        usbHandleStandardRequest(&setup);
    }
    else if ((type == 1u) && ((setup.bmRequestType & 0x1Fu) == 0x01u))
    {
        // Class request to the interface -- MSC (Get Max LUN / BOT Reset)
        const uint8_t *data = NULL;
        int16_t length = USB_MSD_HandleClassRequest(&setup, &data);

        if (length < 0)
        {
            usbEp0Stall();
        }
        else if ((length == 0) || (setup.wLength == 0))
        {
            usbEp0AckNoData();
        }
        else
        {
            usbEp0StartTx(data, (uint16_t)length, setup.wLength);
        }
    }
    else
    {
        usbEp0Stall();
    }
}

static void usbHandleStandardRequest(const usb_setup_packet_t *setup)
{
    uint8_t recipient = setup->bmRequestType & 0x1Fu;

    switch (setup->bRequest)
    {
        case USB_REQ_SET_ADDRESS:
            // Deferred: FUNC is written when the status stage completes
            // (usbServiceEp0)
            pending_address = (int16_t)(setup->wValue & 0x7Fu);
            usbEp0AckNoData();
            break;

        case USB_REQ_GET_DESCRIPTOR:
        {
            uint8_t descType = (uint8_t)(setup->wValue >> 8);
            uint8_t descIndex = (uint8_t)(setup->wValue & 0xFFu);
            const uint8_t *desc = NULL;
            uint16_t length = 0;
            bool highSpeed = (usbReadCsr0() & USB_CSR0_HSMODE) != 0;

            switch (descType)
            {
                case USB_DESC_TYPE_DEVICE:
                    desc = usb_device_descriptor;
                    length = sizeof(usb_device_descriptor);
                    break;

                case USB_DESC_TYPE_CONFIGURATION:
                    desc = highSpeed ? usb_config_descriptor_hs
                                     : usb_config_descriptor_fs;
                    length = USB_CONFIG_DESC_TOTAL_LENGTH;
                    break;

                case USB_DESC_TYPE_OTHER_SPEED_CFG:
                    // The config table for the speed we are NOT running,
                    // with bDescriptorType patched per USB 2.0 9.6.4
                    memcpy(ep0_desc_scratch,
                            highSpeed ? usb_config_descriptor_fs
                                      : usb_config_descriptor_hs,
                            USB_CONFIG_DESC_TOTAL_LENGTH);
                    ep0_desc_scratch[1] = USB_DESC_TYPE_OTHER_SPEED_CFG;
                    desc = ep0_desc_scratch;
                    length = USB_CONFIG_DESC_TOTAL_LENGTH;
                    break;

                case USB_DESC_TYPE_DEVICE_QUALIFIER:
                    desc = usb_device_qualifier;
                    length = sizeof(usb_device_qualifier);
                    break;

                case USB_DESC_TYPE_STRING:
                    desc = USB_GetStringDescriptor(descIndex, &length);
                    break;

                default:
                    break;
            }

            if (desc != NULL)
            {
                usbEp0StartTx(desc, length, setup->wLength);
            }
            else
            {
                usbEp0Stall();
            }
            break;
        }

        case USB_REQ_SET_CONFIGURATION:
            if ((setup->wValue & 0xFFu) == 1u)
            {
                current_configuration = 1;
                usb_device_state = USB_STATE_CONFIGURED;
                usbConfigureEndpoints();    // fresh toggles per 9.1.1.5
                USB_MSD_ConfiguredHook(true);
                usbEp0AckNoData();
            }
            else if ((setup->wValue & 0xFFu) == 0u)
            {
                current_configuration = 0;
                usb_device_state = USB_STATE_ADDRESSED;
                USB_MSD_ConfiguredHook(false);
                usbEp0AckNoData();
            }
            else
            {
                usbEp0Stall();
            }
            break;

        case USB_REQ_GET_CONFIGURATION:
        {
            static uint8_t config;
            config = current_configuration;
            usbEp0StartTx(&config, 1, setup->wLength);
            break;
        }

        case USB_REQ_GET_STATUS:
        {
            static uint8_t status[2];
            status[0] = 0;
            status[1] = 0;

            if (recipient == 0u)
            {
                status[0] = 0x01;   // self-powered
            }
            else if (recipient == 2u)
            {
                uint8_t ep = (uint8_t)(setup->wIndex & 0xFFu);
                if ((ep == (0x80u | USB_BULK_EP_NUM) && USB_BulkInStalled())
                        || ((ep == USB_BULK_EP_NUM) && USB_BulkOutStalled()))
                {
                    status[0] = 0x01;   // halted
                }
            }
            usbEp0StartTx(status, 2, setup->wLength);
            break;
        }

        case USB_REQ_CLEAR_FEATURE:
        case USB_REQ_SET_FEATURE:
        {
            bool set = (setup->bRequest == USB_REQ_SET_FEATURE);
            uint8_t ep = (uint8_t)(setup->wIndex & 0xFFu);

            if ((recipient == 2u) && (setup->wValue == USB_FEATURE_ENDPOINT_HALT)
                    && ((ep & 0x7Fu) == USB_BULK_EP_NUM))
            {
                bool inEndpoint = (ep & 0x80u) != 0;
                if (set)
                {
                    USB_BulkStall(inEndpoint);
                }
                else
                {
                    USB_BulkClearStall(inEndpoint);
                    USB_MSD_EndpointHaltCleared(inEndpoint);
                }
                usbEp0AckNoData();
            }
            else if ((recipient == 2u) && (setup->wValue == USB_FEATURE_ENDPOINT_HALT)
                    && ((ep & 0x7Fu) == 0u))
            {
                usbEp0AckNoData();  // EP0 halt is a no-op ack
            }
            else
            {
                usbEp0Stall();      // TEST_MODE/remote-wakeup unsupported
            }
            break;
        }

        case USB_REQ_GET_INTERFACE:
        {
            static const uint8_t altSetting = 0;
            usbEp0StartTx(&altSetting, 1, setup->wLength);
            break;
        }

        case USB_REQ_SET_INTERFACE:
            if ((setup->wValue == 0u) && (setup->wIndex == 0u))
            {
                usbEp0AckNoData();  // only alt setting 0 exists
            }
            else
            {
                usbEp0Stall();
            }
            break;

        default:
            usbEp0Stall();
            break;
    }
}

// Starts (and possibly finishes) an EP0 IN data stage: sends
// min(length, wLength) bytes in 64B packets. A terminating ZLP is queued
// when the reply is shorter than the host asked for AND ends on a packet
// boundary, so the host sees end-of-data (USB 2.0 8.5.3.2).
static void usbEp0StartTx(const uint8_t *data, uint16_t length, uint16_t wLength)
{
    if (length > wLength)
    {
        length = wLength;
    }

    ep0_tx_ptr = data;
    ep0_tx_remaining = length;
    ep0_tx_need_zlp = (length < wLength) && ((length % USB_EP0_MAX_PACKET) == 0u);

    // Service the SETUP that started this, then load the first packet
    USBE0CSR0 = USB_E0_RXRDYC;
    usbEp0ContinueTx();
}

static void usbEp0ContinueTx(void)
{
    uint16_t chunk = ep0_tx_remaining;
    if (chunk > USB_EP0_MAX_PACKET)
    {
        chunk = USB_EP0_MAX_PACKET;
    }

    for (uint16_t i = 0; i < chunk; i++)
    {
        USB_FIFO0_BYTE = ep0_tx_ptr[i];
    }

    ep0_tx_ptr += chunk;
    ep0_tx_remaining -= chunk;

    bool moreToSend = (ep0_tx_remaining > 0u)
            || (ep0_tx_need_zlp && (chunk == USB_EP0_MAX_PACKET));

    if (moreToSend)
    {
        USBE0CSR0 = USB_E0_TXRDY;
        ep0_state = USB_EP0_TX;
        if ((chunk == USB_EP0_MAX_PACKET) && (ep0_tx_remaining == 0u))
        {
            ep0_tx_need_zlp = false;    // the pending ZLP is now the tail
        }
    }
    else
    {
        USBE0CSR0 = USB_E0_TXRDY | USB_E0_DATAEND;
        ep0_state = USB_EP0_STATUS;
    }
}

static void usbEp0AckNoData(void)
{
    USBE0CSR0 = USB_E0_RXRDYC | USB_E0_DATAEND;
    ep0_state = USB_EP0_STATUS;
}

static void usbEp0Stall(void)
{
    usb_counters.ep0_stalls++;
    USBE0CSR0 = USB_E0_RXRDYC | USB_E0_STALL;
    ep0_state = USB_EP0_IDLE;
}

void USB_Attach(void)
{
    ep0_state = USB_EP0_IDLE;
    pending_address = -1;
    current_configuration = 0;
#if USB_FORCE_SESSION
    USBOTGbits.SESSION = 1;
#endif
    usbModifyCsr0(0, USB_CSR0_HSEN | USB_CSR0_SOFTCONN);
    usb_device_state = USB_STATE_ATTACHED;
}

void USB_Detach(void)
{
    usbModifyCsr0(USB_CSR0_SOFTCONN, 0);
    current_configuration = 0;
    ep0_state = USB_EP0_IDLE;
    usb_device_state = USB_STATE_DETACHED;
    USB_MSD_DetachHook();
}

bool USB_IsConfigured(void)
{
    return (usb_device_state == USB_STATE_CONFIGURED);
}

bool USB_IsHighSpeed(void)
{
    return (usbReadCsr0() & USB_CSR0_HSMODE) != 0;
}

uint16_t USB_GetBulkMaxPacket(void)
{
    return usb_bulk_maxp;
}

bool USB_BulkInBusy(void)
{
    return (USBE1CSR0 & USB_TX_TXPKTRDY) != 0;
}

void USB_BulkInWrite(const uint8_t *data, uint16_t length)
{
    // Word-wide fast path: one 32-bit store per 4 bytes instead of four
    // separate byte stores, each of which stalls on the peripheral bus.
    // USBFIFO1 is a 32-bit port (one register per endpoint), so a word
    // store is its natural access width.
    //
    // Taken only when the source is 4-byte aligned AND the length is a
    // whole number of words, so a single packet NEVER mixes access
    // widths -- that keeps this away from the byte-lane packing rules in
    // the file header entirely. The bulk data path always qualifies
    // (512-byte chunks out of usb_msd.c's aligned block_buf); short
    // odd-sized transfers (the 13-byte CSW, 36-byte INQUIRY) quietly take
    // the byte loop, the same fall-back-silently idiom spi3.c uses when a
    // buffer doesn't qualify for DMA.
    if ((((uintptr_t)data & 3u) == 0u) && ((length & 3u) == 0u))
    {
        const uint32_t *words = (const uint32_t *)(const void *)data;
        uint16_t wordCount = length / 4u;

        for (uint16_t i = 0; i < wordCount; i++)
        {
            USBFIFO1 = words[i];
        }
    }
    else
    {
        for (uint16_t i = 0; i < length; i++)
        {
            USB_FIFO1_BYTE = data[i];
        }
    }

    USBE1CSR0 = USB_TX_CSR_BASE() | USB_TX_TXPKTRDY;
}

bool USB_BulkOutAvailable(void)
{
    return (USBE1CSR1 & USB_RX_RXPKTRDY) != 0;
}

uint16_t USB_BulkOutRead(uint8_t *data, uint16_t maxLength)
{
    if (!(USBE1CSR1 & USB_RX_RXPKTRDY))
    {
        return 0;
    }

    uint16_t count = (uint16_t)USBE1CSR2bits.RXCNT;
    uint16_t stored = (count < maxLength) ? count : maxLength;

    // Word-wide fast path, mirroring USB_BulkInWrite. 32-bit reads are
    // already the proven access mode on this bridge -- usbHandleSetupPacket()
    // unloads SETUP packets exactly this way -- and they satisfy the
    // never-repeat-a-byte-lane rule trivially by consuming all four lanes
    // in one access. Requires the whole packet to fit the caller's buffer
    // (so there is nothing to drain afterwards, keeping the two access
    // widths from mixing within one packet), a 4-byte-aligned
    // destination, and a whole number of words. A 512-byte bulk OUT into
    // block_buf qualifies; the 31-byte CBW does not and takes the lane
    // loop below.
    if ((stored == count) && (((uintptr_t)data & 3u) == 0u)
            && ((count & 3u) == 0u))
    {
        uint32_t *words = (uint32_t *)(void *)data;
        uint16_t wordCount = count / 4u;

        for (uint16_t i = 0; i < wordCount; i++)
        {
            words[i] = USBFIFO1;
        }
    }
    else
    {
        // Rotate the byte lane on every read (file header: FIFO reads must
        // not repeat a byte lane)
        for (uint16_t i = 0; i < stored; i++)
        {
            data[i] = USB_FIFO1_LANES[i & 3u];
        }

        // Drain anything the caller's buffer couldn't hold so the FIFO is
        // clean before release (caller sees the oversize via return > max)
        for (uint16_t i = stored; i < count; i++)
        {
            (void)USB_FIFO1_LANES[i & 3u];
        }
    }

    USBE1CSR1 = USB_RX_CSR_BASE();  // RXPKTRDY <- 0: release the FIFO

    return count;
}

void USB_BulkStall(bool inEndpoint)
{
    if (inEndpoint)
    {
        uint32_t keep = USBE1CSR0 & USB_TX_TXPKTRDY;
        USBE1CSR0 = USB_TX_CSR_BASE() | USB_TX_SENDSTALL | keep;
    }
    else
    {
        uint32_t keep = USBE1CSR1 & USB_RX_RXPKTRDY;
        USBE1CSR1 = USB_RX_CSR_BASE() | USB_RX_SENDSTALL | keep;
    }
}

void USB_BulkClearStall(bool inEndpoint)
{
    // Writing the base value clears SENDSTALL/SENTSTALL (write-0), CLRDT
    // resets the data toggle per USB 2.0 9.4.5
    if (inEndpoint)
    {
        USBE1CSR0 = USB_TX_CSR_BASE() | USB_TX_CLRDT;
    }
    else
    {
        uint32_t keep = USBE1CSR1 & USB_RX_RXPKTRDY;
        USBE1CSR1 = USB_RX_CSR_BASE() | USB_RX_CLRDT | keep;
    }
}

bool USB_BulkInStalled(void)
{
    return (USBE1CSR0 & (USB_TX_SENDSTALL | USB_TX_SENTSTALL)) != 0;
}

bool USB_BulkOutStalled(void)
{
    return (USBE1CSR1 & (USB_RX_SENDSTALL | USB_RX_SENTSTALL)) != 0;
}

void USB_BulkReset(void)
{
    // MSC BOT reset semantics: discard any in-flight FIFO contents but
    // PRESERVE stalls and data toggles (BOT spec 3.1) -- the host clears
    // those itself with CLEAR_FEATURE as part of reset recovery.
    //
    // FLUSH discards ONE packet per write and is only valid while the
    // corresponding PKTRDY is set, so a double-buffered endpoint
    // (usbConfigureEndpoints) needs up to two passes to empty both
    // halves. Bounded at 2 -- if PKTRDY is still set after that, the core
    // has re-armed from the bus rather than kept stale data.
    for (uint8_t pass = 0; pass < 2u; pass++)
    {
        uint32_t tx = USBE1CSR0;
        if (!(tx & USB_TX_TXPKTRDY))
        {
            break;
        }
        USBE1CSR0 = USB_TX_CSR_BASE() | USB_TX_FLUSH
                | (tx & (USB_TX_SENDSTALL | USB_TX_SENTSTALL));
    }

    for (uint8_t pass = 0; pass < 2u; pass++)
    {
        uint32_t rx = USBE1CSR1;
        if (!(rx & USB_RX_RXPKTRDY))
        {
            break;
        }
        USBE1CSR1 = USB_RX_CSR_BASE() | USB_RX_FLUSH
                | (rx & (USB_RX_SENDSTALL | USB_RX_SENTSTALL));
    }
}

void USB_PrintStatus(void)
{
    static const char *state_names[] = {
        "DETACHED", "ATTACHED", "DEFAULT", "ADDRESSED", "CONFIGURED"
    };

    terminalTextAttributesReset();

    // One latching snapshot -- USBCSR0's IF field is clear-on-read, so
    // even a status print must go through the accessor
    uint32_t csr0 = usbReadCsr0();

    if (PMD5bits.USBMD) terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
    else terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("    USB Module Enabled (PMD):                 %s\n\r", PMD5bits.USBMD ? "F" : "T");

    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("    Device State:                             %s\n\r",
            state_names[usb_device_state]);
    printf("    Soft Connect:                             %s\n\r",
            (csr0 & USB_CSR0_SOFTCONN) ? "T" : "F");
    printf("    Negotiated Speed:                         %s\n\r",
            (csr0 & USB_CSR0_HSMODE) ? "High Speed (480 Mbps)" : "Full Speed (12 Mbps)");
    printf("    Function Address:                         %u\n\r",
            (unsigned)(csr0 & USB_CSR0_FUNC_MASK));
    printf("    Bulk Max Packet:                          %u bytes\n\r",
            (unsigned)usb_bulk_maxp);

    // Both bulk endpoints are double-buffered; flag it loudly if the map
    // in usbConfigureEndpoints() ever outgrows the core's actual FIFO RAM
    {
        uint32_t ramBytes = 8ul << USBINFObits.RAMBITS;
        if (USB_FIFO_TOTAL_USED > ramBytes)
            terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("    Endpoint FIFO RAM Used:                   %u / %lu bytes\n\r",
                (unsigned)USB_FIFO_TOTAL_USED, (unsigned long)ramBytes);
        terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);

        // USBOTG's FIFO fields are a window indexed by USBCSR3.ENDPOINT,
        // which sits at 0 outside usbConfigureEndpoints() -- select the
        // bulk endpoint to read back ITS config, then put it back
        USBCSR3bits.ENDPOINT = USB_BULK_EP_NUM;
        bool txDouble = USBOTGbits.TXDPB;
        bool rxDouble = USBOTGbits.RXDPB;
        USBCSR3bits.ENDPOINT = 0;

        printf("    Bulk FIFO Buffering:                      %s / %s (IN / OUT)\n\r",
                txDouble ? "double" : "single", rxDouble ? "double" : "single");
    }
    printf("    Session Active:                           %s%s\n\r",
            USBOTGbits.SESSION ? "T" : "F",
            USB_FORCE_SESSION ? " (forced)" : "");
    printf("    Suspend Mode:                             %s\n\r",
            (csr0 & USB_CSR0_SUSPMODE) ? "T" : "F");

    printf("    USBCRCON:                                 0x%08lX\n\r",
            (unsigned long)USBCRCON);
    printf("    USBCSR0 (low half):                       0x%08lX\n\r",
            (unsigned long)(csr0 & 0x0000FFFFul));
    printf("    USBE1CSR0 (bulk IN):                      0x%08lX\n\r",
            (unsigned long)USBE1CSR0);
    printf("    USBE1CSR1 (bulk OUT):                     0x%08lX\n\r",
            (unsigned long)USBE1CSR1);

    if (USB_BulkInStalled() || USB_BulkOutStalled())
        terminalTextAttributes(YELLOW_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("    Bulk IN / OUT Stalled:                    %s / %s\n\r",
            USB_BulkInStalled() ? "T" : "F", USB_BulkOutStalled() ? "T" : "F");

    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("    Bus Resets:                               %lu\n\r",
            (unsigned long)usb_counters.bus_resets);
    printf("    Suspends / Resumes:                       %lu / %lu\n\r",
            (unsigned long)usb_counters.suspends,
            (unsigned long)usb_counters.resumes);
    printf("    Disconnects:                              %lu\n\r",
            (unsigned long)usb_counters.disconnects);
    printf("    SETUP Packets:                            %lu\n\r",
            (unsigned long)usb_counters.setup_packets);
    printf("    EP0 Stalls:                               %lu\n\r",
            (unsigned long)usb_counters.ep0_stalls);

    if (usb_counters.vbus_errors > 0)
        terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("    VBUS Errors:                              %lu\n\r",
            (unsigned long)usb_counters.vbus_errors);

    terminalTextAttributesReset();
}
