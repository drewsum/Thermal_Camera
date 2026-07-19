/*******************************************************************************
  USB Mass Storage Device Class Driver (Bulk-Only Transport + SCSI)

  File Name:
    usb_msd.h

  Summary:
    The device tier above usb.c: implements the MSC Bulk-Only Transport
    (CBW/CSW) state machine and the SCSI transparent command set over two
    logical units -- LUN 0 = microSD card (removable, may be absent),
    LUN 1 = SST25VF080B SPI flash (via sst25vf080b_disk.h, always
    present). Never touches a USB register; all bus access goes through
    usb.h's USB_Bulk*() primitives.

  Description:
    Yield-to-host policy: a USB host and the local firmware cannot both
    have a FAT volume mounted read/write without corrupting it, so when
    the host configures this device (SET_CONFIGURATION 1), the
    ConfiguredHook unmounts the local FatFs volumes (SD kept powered --
    the host is about to use it) and sets usb_msd_media_owned_by_host;
    sd_fileio.c/flash_fileio.c entry points refuse while it's set. The
    media is handed back (flash staging synced, both volumes remounted)
    on disconnect, bus reset, SET_CONFIGURATION(0), USB_Detach(), or
    suspend. Suspend is in that list because with USB_FORCE_SESSION
    (usb.h) a cable unplug is only observable as a suspend -- and since
    a host can also resume from a suspend without re-configuring, the
    ResumeHook re-yields if the device is still configured, closing the
    both-sides-mounted window a host-sleep cycle would otherwise open.

    Durability: LUN 1 writes land in sst25vf080b_disk.c's 4KB staging
    buffer. It is flushed on SCSI SYNCHRONIZE CACHE / START STOP UNIT
    (eject) / BOT reset / every hand-back above, plus a write-idle
    timeout in USB_MSD_TimedTasks() -- so a cable yank mid-write can
    lose at most the staged 4KB (normal removable-media behavior).

    SD hot-swap while attached: LUN 0 reports NOT READY (MEDIUM NOT
    PRESENT) without a card; when one is inserted, it is initialized on
    the next SCSI command and reported via UNIT ATTENTION (MEDIA
    CHANGED), which makes the host rescan the volume.
*******************************************************************************/

#ifndef USB_MSD_H
#define USB_MSD_H

#include <stdint.h>
#include <stdbool.h>

#include "usb/usb.h"

#ifdef __cplusplus
extern "C" {
#endif

#define USB_MSD_NUM_LUNS    2u

// Max 512B blocks moved per USB_MSD_Tasks() call during a READ/WRITE
// data phase, so a multi-MB transfer can't starve the superloop
// (watchdog kick, console, telemetry) between passes
#define USB_MSD_BLOCKS_PER_PASS 8u

// Bulk-Only Transport state
typedef enum
{
    MSD_STATE_WAIT_CBW = 0,     // idle, bulk OUT armed for a 31-byte CBW
    MSD_STATE_DATA_IN,          // streaming command data to the host
    MSD_STATE_DATA_OUT,         // receiving WRITE(10) data from the host
    MSD_STATE_SEND_CSW,         // data phase done, CSW pending FIFO space
    MSD_STATE_STALLED           // EP(s) halted (BOT 6.6.1 / case-13);
                                // CSW sends after the host clears the halt
} usb_msd_bot_state_t;

// Diagnostic counters for USB_MSD_PrintStatus()
typedef struct
{
    uint32_t cbws_received;
    uint32_t invalid_cbws;
    uint32_t csw_failures;      // CSWs sent with bCSWStatus != 0
    uint32_t unsupported_scsi;  // commands answered ILLEGAL REQUEST
    uint32_t blocks_read;
    uint32_t blocks_written;
    uint32_t bot_resets;
    uint32_t staging_syncs;
} usb_msd_counters_t;

// Set while a USB host owns the media (see file header). Checked by
// sd_fileio.c and flash_fileio.c entry guards.
volatile uint8_t usb_msd_media_owned_by_host = 0;

usb_msd_counters_t usb_msd_counters = {0};

// Resets all transport/LUN state to power-on defaults. Called by
// USB_Initialize(); no hardware of its own to bring up.
void USB_MSD_Initialize(void);

// Transport pump, called from USB_Tasks() after the bus/EP0 work with
// this pass's latched endpoint events. Runs the BOT state machine,
// moving at most USB_MSD_BLOCKS_PER_PASS blocks before returning.
void USB_MSD_Tasks(bool bulkInEvent, bool bulkOutEvent);

// Write-idle staging flush (see file header), called every superloop
// pass -- cheap CP0-count compare when nothing is dirty.
void USB_MSD_TimedTasks(void);

// ---- Hooks called by usb.c (task context, never from the ISR) ----

// MSC class requests on EP0. Returns the IN-data length to send (0 for a
// no-data ack) with *outData pointed at the response, or -1 to have
// usb.c stall EP0 (unsupported request). Handles Get Max LUN (0xFE) and
// Bulk-Only Mass Storage Reset (0xFF).
int16_t USB_MSD_HandleClassRequest(const usb_setup_packet_t *setup,
        const uint8_t **outData);

// SET_CONFIGURATION edge: configured=true yields the media to the host,
// false hands it back (see file header).
void USB_MSD_ConfiguredHook(bool configured);

// Bus reset: BOT state machine to WAIT_CBW and, since a reset
// deconfigures the device, hands media back like ConfiguredHook(false).
void USB_MSD_BusResetHook(void);

// Suspend/disconnect/local detach: sync staging, hand media back.
void USB_MSD_DetachHook(void);

// Resume: if the host resumed a still-configured device, re-yield the
// media (see file header's suspend/resume note).
void USB_MSD_ResumeHook(void);

// CLEAR_FEATURE(ENDPOINT_HALT) landed on a bulk endpoint -- lets the
// BOT machine finish its CSW after a case-13/error stall.
void USB_MSD_EndpointHaltCleared(bool inEndpoint);

// The flash write-protect state changed (terminal command) -- raises
// UNIT ATTENTION (MEDIA CHANGED) on the write-protectable LUNs so an
// attached host re-mounts them and re-reads MODE SENSE's WP bit, which
// it otherwise caches from mount time.
void USB_MSD_NotifyWriteProtectChanged(void);

// Prints transport state, per-LUN media state, and counters (backing
// "USB Status?" alongside usb.c's USB_PrintStatus()).
void USB_MSD_PrintStatus(void);

#ifdef __cplusplus
}
#endif

#endif /* USB_MSD_H */
