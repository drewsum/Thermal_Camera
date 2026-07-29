/*******************************************************************************
  USB Device Peripheral Driver (Hi-Speed USB / MUSB core)

  File Name:
    usb.h

  Summary:
    Register-level device-mode driver for the PIC32MZ DA's Hi-Speed USB
    module. Owns the bus (attach/detach/reset/suspend), endpoint 0 and
    every standard request, and the raw bulk endpoint-1 primitives. The
    mass storage protocol itself (Bulk-Only Transport + SCSI) lives one
    tier up in usb/device_driver/usb_msd.c -- the same peripheral/device
    split as sdhc.c + sd_card.c.

  Description:
    Layering: usb.c (this, MUSB registers) -> usb_msd.c (BOT/SCSI, 2 LUNs)
    -> sd_card.h / w25q128jv_disk.h (block backends). usb.c calls up
    into usb_msd.c only through the USB_MSD_*Hook()/USB_MSD_Tasks()
    seams declared in usb_msd.h; usb_msd.c never touches a USB register,
    only the USB_Bulk*() primitives below.

    Clocking: there is no runtime USB PLL setup on this device -- the HS
    PHY's 480MHz PLL runs from the DEVCFG2.UPLLFSEL fuse (FREQ_24MHZ,
    matching the 24MHz POSC; pic32mzda_configuration.h). The empirical
    check that this works is USBCSR0.HSMODE reading 1 after a host
    reset. PMD note: PMDInitialize() (power_saving.c) must leave
    PMD5.USBMD clear; PMD is one-shot, so USB_Initialize() can only
    check it, not fix it.

    VBUS: the MCU's VBUS pin wiring is unverified on rev A, so
    USB_FORCE_SESSION (below) is enabled -- the MUSB session is forced
    on and the USBCRCON VBUS comparator monitors stay off, meaning the
    core acts as if VBUS were always valid and attach/detach are
    tracked via bus reset/suspend/disconnect events instead. Once VBUS
    sensing is verified on hardware, flip it to 0 to get true
    plug-detection via VBUSMONEN/session valid.

    Interrupt model ("ISR latches, superloop executes" -- same shape as
    sdhc.c/usb_uart.c, but load-bearing here): the MUSB interrupt-flag
    fields in USBCSR0/1/2 are CLEAR-ON-READ, so they must be read
    exactly once, in one place. usbISR() reads all three, ORs the flag
    bits into the usb_pending_* accumulators below, and sets
    usb_event_pending; USB_Tasks() (main superloop) drains the
    accumulators and runs all protocol logic. Hardware NAKs the host
    until firmware responds, so superloop-latency is protocol-safe.
*******************************************************************************/

#ifndef USB_H
#define USB_H

#include <stdint.h>
#include <stdbool.h>
#include <xc.h>
#include <sys/attribs.h>

#ifdef __cplusplus
extern "C" {
#endif

// Force the MUSB session active instead of monitoring VBUS -- see the
// file header. Bring-up default: 1.
#define USB_FORCE_SESSION   1

// Device state per USB 2.0 chapter 9 (POWERED is skipped -- with a
// forced session, DEFAULT is entered at the first bus reset)
typedef enum
{
    USB_STATE_DETACHED = 0,     // SOFTCONN off (or never initialized)
    USB_STATE_ATTACHED,         // pull-up on, no bus reset seen yet
    USB_STATE_DEFAULT,          // bus reset seen, address 0
    USB_STATE_ADDRESSED,        // SET_ADDRESS complete
    USB_STATE_CONFIGURED        // SET_CONFIGURATION(1) complete
} usb_device_state_t;

// SETUP packet, USB 2.0 table 9-2
typedef struct
{
    uint8_t bmRequestType;
    uint8_t bRequest;
    uint16_t wValue;
    uint16_t wIndex;
    uint16_t wLength;
} usb_setup_packet_t;

// Diagnostic counters for USB_PrintStatus() -- cleared only by reset
typedef struct
{
    uint32_t bus_resets;
    uint32_t suspends;
    uint32_t resumes;
    uint32_t disconnects;
    uint32_t vbus_errors;
    uint32_t setup_packets;
    uint32_t ep0_stalls;
} usb_counters_t;

// ISR -> USB_Tasks() event accumulators (see file header: the CSR
// interrupt-flag fields are clear-on-read, the ISR is the one reader).
// usb_pending_csr0 holds USBCSR0<23:16> (EP0IF/EPnTXIF), usb_pending_csr1
// holds USBCSR1<7:0> (EPnRXIF), usb_pending_csr2 holds USBCSR2<23:16>
// (bus events).
volatile uint32_t usb_pending_csr0 = 0;
volatile uint32_t usb_pending_csr1 = 0;
volatile uint32_t usb_pending_csr2 = 0;
volatile uint8_t usb_event_pending = 0;

volatile usb_device_state_t usb_device_state = USB_STATE_DETACHED;
usb_counters_t usb_counters = {0};

// Brings up the USB module as a device and attaches to the bus: checks
// PMD5.USBMD was left clear, forces B-device role via USBCRCON ID
// override (the port is hardwired to a hub downstream port -- no ID
// pin), configures the endpoint FIFO map (EP0 64B + bulk EP1 512B TX +
// 512B RX), enables the module interrupt, requests Hi-Speed, and sets
// SOFTCONN. Returns false if the module is PMD-disabled.
bool USB_Initialize(void);

// Event pump -- call every superloop pass (cheap no-op when
// usb_event_pending is clear). Drains the ISR accumulators and runs bus
// events, the EP0 control state machine, and USB_MSD_Tasks().
void USB_Tasks(void);

// Soft connect/disconnect (D+ pull-up). Detach is the local "take my
// drives back" control: the host sees a clean unplug, and the detach
// hook remounts the local filesystems. Attach re-enumerates.
void USB_Attach(void);
void USB_Detach(void);

// True once the host has selected configuration 1 (bulk endpoints live).
bool USB_IsConfigured(void);

// True if the current connection negotiated Hi-Speed (USBCSR0.HSMODE).
bool USB_IsHighSpeed(void);

// ---- Bulk endpoint-1 primitives for usb_msd.c (no other module should
// call these; usb.c keeps register access to itself) ----

// Current bulk max packet size: 512 (HS) or 64 (FS).
uint16_t USB_GetBulkMaxPacket(void);

// True while the previous bulk IN packet is still in the FIFO
// (TXPKTRDY) -- the FIFO must not be reloaded until this clears.
bool USB_BulkInBusy(void);

// Loads `length` (<= USB_GetBulkMaxPacket()) bytes into the bulk IN FIFO
// and arms TXPKTRDY. Caller must check !USB_BulkInBusy() first.
void USB_BulkInWrite(const uint8_t *data, uint16_t length);

// True when a received bulk OUT packet is waiting in the FIFO (RXPKTRDY).
bool USB_BulkOutAvailable(void);

// Unloads the waiting bulk OUT packet (up to maxLength bytes -- a packet
// longer than maxLength is truncated, which the caller should treat as a
// protocol error) and releases the FIFO. Returns the packet's byte
// count, or 0 if no packet was waiting.
uint16_t USB_BulkOutRead(uint8_t *data, uint16_t maxLength);

// Sets/clears endpoint halt on the bulk endpoints. `inEndpoint` selects
// bulk IN (true) or bulk OUT (false). Clearing also resets the data
// toggle, per USB 2.0 9.4.5.
void USB_BulkStall(bool inEndpoint);
void USB_BulkClearStall(bool inEndpoint);
bool USB_BulkInStalled(void);
bool USB_BulkOutStalled(void);

// Flushes both bulk FIFOs and clears stalls/toggles -- the transport
// reset primitive backing the MSC Bulk-Only Mass Storage Reset request.
void USB_BulkReset(void);

// Prints module/bus/endpoint register state and the event counters to
// the terminal (Peripheral Status? USB).
void USB_PrintStatus(void);

// USB general-event interrupt service routine. Declared here, defined in
// usb.c. Minimal body: single clear-on-read pass of USBCSR0/1/2 into the
// usb_pending_* accumulators, then clearInterruptFlag() -- no protocol
// logic runs here (see file header).
void __ISR(_USB_VECTOR, IPL2SRS) usbISR(void);

#ifdef __cplusplus
}
#endif

#endif /* USB_H */
