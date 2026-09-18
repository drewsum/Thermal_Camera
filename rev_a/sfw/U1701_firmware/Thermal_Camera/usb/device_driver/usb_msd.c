/*******************************************************************************
  USB Mass Storage Device Class Driver (Bulk-Only Transport + SCSI)

  File Name:
    usb_msd.c

  Summary:
    MSC Bulk-Only Transport state machine and SCSI transparent command
    set over LUN 0 (microSD card) and LUN 1 (SPI flash disk layer). See
    usb_msd.h for the yield-to-host and durability design notes.
*******************************************************************************/

#include <xc.h>
#include <stdio.h>
#include <string.h>

#include "usb/device_driver/usb_msd.h"
#include "usb/usb.h"
#include "sdhc/device_driver/sd_card.h"
#include "sdhc/sd_fileio.h"
#include "spi/device_driver/w25q128jv_disk.h"
#include "spi/device_driver/w25q128jv.h"
#include "spi/flash_fileio.h"
#include "usb_uart/terminal_control.h"

// ---- Bulk-Only Transport wire formats (MSC BOT spec 5.1/5.2) ----
#define MSD_CBW_LENGTH          31u
#define MSD_CSW_LENGTH          13u
#define MSD_CBW_SIGNATURE       0x43425355ul
#define MSD_CSW_SIGNATURE       0x53425355ul
#define MSD_CBW_FLAG_DATA_IN    0x80u

#define MSD_CSW_STATUS_PASS         0u
#define MSD_CSW_STATUS_FAIL         1u
#define MSD_CSW_STATUS_PHASE_ERROR  2u

// ---- SCSI opcodes ----
#define SCSI_TEST_UNIT_READY        0x00u
#define SCSI_REQUEST_SENSE          0x03u
#define SCSI_INQUIRY                0x12u
#define SCSI_MODE_SENSE_6           0x1Au
#define SCSI_START_STOP_UNIT        0x1Bu
#define SCSI_PREVENT_ALLOW_REMOVAL  0x1Eu
#define SCSI_READ_FORMAT_CAPACITIES 0x23u
#define SCSI_READ_CAPACITY_10       0x25u
#define SCSI_READ_10                0x28u
#define SCSI_WRITE_10               0x2Au
#define SCSI_VERIFY_10              0x2Fu
#define SCSI_SYNCHRONIZE_CACHE_10   0x35u
#define SCSI_MODE_SENSE_10          0x5Au

// ---- SCSI sense keys / additional sense codes ----
#define SENSE_KEY_NO_SENSE          0x0u
#define SENSE_KEY_NOT_READY         0x2u
#define SENSE_KEY_MEDIUM_ERROR      0x3u
#define SENSE_KEY_ILLEGAL_REQUEST   0x5u
#define SENSE_KEY_UNIT_ATTENTION    0x6u

#define SENSE_KEY_DATA_PROTECT      0x7u

#define ASC_INVALID_COMMAND         0x20u   // ascq 0x00
#define ASC_LBA_OUT_OF_RANGE        0x21u
#define ASC_INVALID_FIELD_IN_CDB    0x24u
#define ASC_LUN_NOT_SUPPORTED       0x25u
#define ASC_WRITE_PROTECTED         0x27u
#define ASC_MEDIA_CHANGED           0x28u
#define ASC_MEDIUM_NOT_PRESENT      0x3Au
#define ASC_WRITE_ERROR             0x0Cu
#define ASC_UNRECOVERED_READ_ERROR  0x11u
#define ASC_MEDIUM_REMOVAL_PREVENTED 0x53u  // ascq 0x02

#define MSD_BLOCK_SIZE              512u

// Write-idle staging flush threshold (usb_msd.h) -- CP0 Count runs at
// SYSCLK/2 = 100MHz
#define MSD_FLUSH_IDLE_TICKS        (250ul * 100000ul)  // 250ms

// Bounded wait for the single-buffered bulk IN FIFO to drain so one
// Tasks() pass can move several packets -- 512B on a 480Mbps wire is
// ~10us + protocol overhead, so 50us covers it with margin, and a busy
// bus (NAK-heavy host) just falls back to one-packet-per-event pacing
#define MSD_TX_DRAIN_WAIT_TICKS     (50ul * 100ul)      // 50us

// ---- Per-LUN backend ops ----
typedef struct
{
    bool removable;             // internal semantics: hotplug detect + eject
    bool inquiry_removable;     // RMB bit reported to the host. Windows only
                                // auto-mounts a partition-table-less
                                // (superfloppy/FM_SFD) volume when RMB=1, so
                                // the flash LUN must claim removable even
                                // though it is soldered down
    const char *inquiry_product;    // exactly 16 chars
    bool (*isPresent)(void);
    bool (*isWriteProtected)(void); // NULL = never write-protected
    uint32_t (*sectorCount)(void);
    bool (*readSectors)(uint32_t lba, uint8_t *buffer, uint16_t count);
    bool (*writeSectors)(uint32_t lba, const uint8_t *buffer, uint16_t count);
    bool (*sync)(void);
} msd_lun_ops_t;

// ---- Per-LUN runtime state ----
typedef struct
{
    uint8_t sense_key;
    uint8_t asc;
    uint8_t ascq;
    bool unit_attention;    // MEDIA CHANGED pending report
    bool ejected;           // host issued START STOP UNIT eject
    bool prevent_removal;
    bool present_last;      // presence edge tracking (removable media)
} msd_lun_state_t;

// ---- LUN 0: microSD card backend wrappers ----

static bool msdSdIsPresent(void)
{
    return SD_Card_IsPresent() && (SD_Card_GetInfo() != NULL);
}

static uint32_t msdSdSectorCount(void)
{
    const sd_card_info_t *info = SD_Card_GetInfo();
    return (info != NULL) ? info->capacity_blocks : 0u;
}

static bool msdSdReadSectors(uint32_t lba, uint8_t *buffer, uint16_t count)
{
    return SD_Card_ReadBlocks(lba, buffer, count);
}

static bool msdSdWriteSectors(uint32_t lba, const uint8_t *buffer, uint16_t count)
{
    return SD_Card_WriteBlocks(lba, buffer, count);
}

static bool msdSdSync(void)
{
    // SD_Card_WriteBlocks() is write-through (diskio.c's CTRL_SYNC note)
    return true;
}

// ---- LUN 1: SPI flash disk layer wrappers ----

static bool msdFlashIsPresent(void)
{
    return W25Q128JV_Disk_IsInitialized();
}

static bool msdFlashIsWriteProtected(void)
{
    return W25Q128JV_WriteProtectIsEnabled();
}

static const msd_lun_ops_t msd_luns[USB_MSD_NUM_LUNS] = {
    {
        .removable = true,
        .inquiry_removable = true,
        .inquiry_product = "Thermal Cam SD  ",
        .isPresent = msdSdIsPresent,
        .sectorCount = msdSdSectorCount,
        .readSectors = msdSdReadSectors,
        .writeSectors = msdSdWriteSectors,
        .sync = msdSdSync
    },
    {
        .removable = false,
        .inquiry_removable = true,
        .inquiry_product = "Thermal Cam SPI ",
        .isPresent = msdFlashIsPresent,
        .isWriteProtected = msdFlashIsWriteProtected,
        .sectorCount = W25Q128JV_Disk_GetSectorCount,
        .readSectors = W25Q128JV_Disk_ReadSectors,
        .writeSectors = W25Q128JV_Disk_WriteSectors,
        .sync = W25Q128JV_Disk_Sync
    }
};

static msd_lun_state_t msd_lun_state[USB_MSD_NUM_LUNS];

// True when writes to this LUN must be refused (and reported as such in
// MODE SENSE)
static bool msdLunWriteProtected(uint8_t lun)
{
    return (lun < USB_MSD_NUM_LUNS)
            && (msd_luns[lun].isWriteProtected != NULL)
            && msd_luns[lun].isWriteProtected();
}

// ---- Transport state ----
static usb_msd_bot_state_t bot_state = MSD_STATE_WAIT_CBW;

// Current CBW, parsed
static uint32_t cbw_tag;
static uint32_t cbw_dtl;            // dCBWDataTransferLength
static uint8_t cbw_flags;
static uint8_t cbw_lun;
static uint8_t cbw_cb[16];

static uint8_t csw_status;
static uint32_t data_residue;

// Data-IN sources: a small buffer for command responses, or streamed
// media blocks
static uint8_t response_buf[64];
static uint16_t response_len;
static uint16_t response_off;

static uint8_t block_buf[MSD_BLOCK_SIZE];
static uint16_t block_buf_len;      // valid bytes staged for IN / target for OUT
static uint16_t block_buf_off;

static bool xfer_from_media;        // IN data comes from readSectors
static uint32_t xfer_lba;
static uint32_t xfer_blocks_remaining;
static uint32_t data_bytes_remaining;   // bytes still to move on the bus
static bool stall_in_after_data;        // device data < host expectation

// One-byte EP0 response for Get Max LUN
static uint8_t max_lun_response;

static void msdPump(void);
static void msdReceiveCbw(void);
static void msdDispatchScsi(void);
static void msdPumpDataIn(void);
static void msdPumpDataOut(void);
static void msdTrySendCsw(void);
static void msdYieldMediaToHost(void);
static void msdHandMediaBack(void);

static void msdSetSense(uint8_t lun, uint8_t key, uint8_t asc, uint8_t ascq)
{
    if (lun < USB_MSD_NUM_LUNS)
    {
        msd_lun_state[lun].sense_key = key;
        msd_lun_state[lun].asc = asc;
        msd_lun_state[lun].ascq = ascq;
    }
}

void USB_MSD_Initialize(void)
{
    bot_state = MSD_STATE_WAIT_CBW;
    memset(msd_lun_state, 0, sizeof(msd_lun_state));

    for (uint8_t lun = 0; lun < USB_MSD_NUM_LUNS; lun++)
    {
        msd_lun_state[lun].present_last = msd_luns[lun].isPresent();
    }

    usb_msd_media_owned_by_host = 0;
}

// Presence edges for removable media (LUN 0): a card inserted while the
// host is attached is initialized here and announced via UNIT ATTENTION
// (MEDIA CHANGED) so the host rescans; removal latches the same
// attention for whatever comes next. Returns whether the medium is
// ready for I/O right now.
static bool msdLunMediaReady(uint8_t lun)
{
    if (lun >= USB_MSD_NUM_LUNS)
    {
        return false;
    }

    const msd_lun_ops_t *ops = &msd_luns[lun];
    msd_lun_state_t *state = &msd_lun_state[lun];

    if (!ops->removable)
    {
        return ops->isPresent();
    }

    bool detected = SD_Card_IsPresent();

    if (detected && !state->present_last)
    {
        // Fresh insertion -- bring the card up now (identification takes
        // ~100ms; acceptable as a one-off in task context)
        if (SD_Card_Initialize())
        {
            state->unit_attention = true;
            state->ejected = false;
        }
        else
        {
            detected = false;
        }
    }
    else if (!detected && state->present_last)
    {
        // Removal: report MEDIA CHANGED when something shows up again
        state->unit_attention = true;
    }

    state->present_last = detected;

    return detected && !state->ejected && ops->isPresent();
}

int16_t USB_MSD_HandleClassRequest(const usb_setup_packet_t *setup,
        const uint8_t **outData)
{
    // Get Max LUN: D2H | class | interface, one data byte = highest LUN
    if ((setup->bRequest == 0xFEu) && (setup->bmRequestType == 0xA1u))
    {
        max_lun_response = USB_MSD_NUM_LUNS - 1u;
        *outData = &max_lun_response;
        return 1;
    }

    // Bulk-Only Mass Storage Reset: H2D | class | interface, no data.
    // Readies the device for the next CBW; per BOT 3.1 the bulk stalls
    // and data toggles are deliberately preserved (the host clears them
    // itself during reset recovery).
    if ((setup->bRequest == 0xFFu) && (setup->bmRequestType == 0x21u)
            && (setup->wLength == 0u))
    {
        usb_msd_counters.bot_resets++;
        USB_BulkReset();
        W25Q128JV_Disk_Sync();
        bot_state = MSD_STATE_WAIT_CBW;
        return 0;
    }

    return -1;
}

void USB_MSD_Tasks(bool bulkInEvent, bool bulkOutEvent)
{
    (void)bulkInEvent;
    (void)bulkOutEvent;

    // Events only tell us "state advanced" -- the pump re-derives
    // everything from FIFO readiness, so it is also safe to call from
    // the halt-cleared hook
    msdPump();
}

static void msdPump(void)
{
    if (!USB_IsConfigured())
    {
        return;
    }

    switch (bot_state)
    {
        case MSD_STATE_WAIT_CBW:
            msdReceiveCbw();
            break;

        case MSD_STATE_DATA_IN:
            msdPumpDataIn();
            break;

        case MSD_STATE_DATA_OUT:
            msdPumpDataOut();
            break;

        case MSD_STATE_SEND_CSW:
            msdTrySendCsw();
            break;

        case MSD_STATE_STALLED:
            // Dead until a Bulk-Only Mass Storage Reset (BOT 6.6.1)
            break;
    }
}

static void msdReceiveCbw(void)
{
    if (!USB_BulkOutAvailable())
    {
        return;
    }

    uint8_t raw[MSD_CBW_LENGTH];
    uint16_t received = USB_BulkOutRead(raw, sizeof(raw));

    uint32_t signature = (uint32_t)raw[0] | ((uint32_t)raw[1] << 8)
            | ((uint32_t)raw[2] << 16) | ((uint32_t)raw[3] << 24);

    uint8_t cbLength = (received == MSD_CBW_LENGTH) ? (raw[14] & 0x1Fu) : 0u;

    if ((received != MSD_CBW_LENGTH) || (signature != MSD_CBW_SIGNATURE)
            || (cbLength < 1u) || (cbLength > 16u))
    {
        // Not a valid/meaningful CBW: per BOT 6.6.1 stall both pipes and
        // stay dead until a Bulk-Only Mass Storage Reset
        usb_msd_counters.invalid_cbws++;
        USB_BulkStall(true);
        USB_BulkStall(false);
        bot_state = MSD_STATE_STALLED;
        return;
    }

    usb_msd_counters.cbws_received++;

    cbw_tag = (uint32_t)raw[4] | ((uint32_t)raw[5] << 8)
            | ((uint32_t)raw[6] << 16) | ((uint32_t)raw[7] << 24);
    cbw_dtl = (uint32_t)raw[8] | ((uint32_t)raw[9] << 8)
            | ((uint32_t)raw[10] << 16) | ((uint32_t)raw[11] << 24);
    cbw_flags = raw[12];
    cbw_lun = raw[13] & 0x0Fu;
    memcpy(cbw_cb, &raw[15], sizeof(cbw_cb));

    csw_status = MSD_CSW_STATUS_PASS;
    data_residue = cbw_dtl;
    response_len = 0;
    response_off = 0;
    block_buf_len = 0;
    block_buf_off = 0;
    xfer_from_media = false;
    xfer_blocks_remaining = 0;
    data_bytes_remaining = 0;
    stall_in_after_data = false;

    msdDispatchScsi();
}

// Queues a small (<= sizeof(response_buf)) IN response prepared in
// response_buf, clipped to what the host asked for
static void msdQueueResponse(uint16_t length)
{
    if (length > cbw_dtl)
    {
        length = (uint16_t)cbw_dtl;
    }
    response_len = length;
    response_off = 0;
    xfer_from_media = false;
    data_bytes_remaining = length;
}

// Ends the command with CHECK CONDITION and the given sense
static void msdFailCommand(uint8_t key, uint8_t asc, uint8_t ascq)
{
    msdSetSense(cbw_lun, key, asc, ascq);
    csw_status = MSD_CSW_STATUS_FAIL;
    data_bytes_remaining = 0;
    response_len = 0;
    xfer_from_media = false;
    xfer_blocks_remaining = 0;
}

// After the SCSI handler has decided what data moves, route to the right
// phase, honoring the BOT case-13 rules for mismatches between what the
// host said (cbw_dtl/direction) and what the command produces
static void msdEnterDataPhase(bool commandWantsIn, bool commandWantsOut)
{
    if (cbw_dtl == 0u)
    {
        if (commandWantsIn || commandWantsOut)
        {
            // Case 2/3: host expected no data but the command moves some
            csw_status = MSD_CSW_STATUS_PHASE_ERROR;
        }
        bot_state = MSD_STATE_SEND_CSW;
        msdTrySendCsw();
        return;
    }

    bool hostExpectsIn = (cbw_flags & MSD_CBW_FLAG_DATA_IN) != 0;

    if (!commandWantsIn && !commandWantsOut)
    {
        // No-data command (or one that already failed before its data
        // phase) against a nonzero host expectation -- cases 4/9: stall
        // the pipe the host is using, keep the command's own status,
        // residue reports the whole unmoved expectation
        USB_BulkStall(hostExpectsIn);
        bot_state = MSD_STATE_SEND_CSW;
        msdTrySendCsw();
        return;
    }

    if (commandWantsOut)
    {
        if (hostExpectsIn)
        {
            // Case 10: direction disagreement -- stall the IN pipe the
            // host is listening on, phase error
            csw_status = MSD_CSW_STATUS_PHASE_ERROR;
            USB_BulkStall(true);
            bot_state = MSD_STATE_SEND_CSW;
            return;
        }
        bot_state = MSD_STATE_DATA_OUT;
        msdPumpDataOut();
        return;
    }

    if (!hostExpectsIn)
    {
        // Case 8: command produces IN data but host set direction OUT
        csw_status = MSD_CSW_STATUS_PHASE_ERROR;
        USB_BulkStall(false);
        bot_state = MSD_STATE_SEND_CSW;
        msdTrySendCsw();
        return;
    }

    // IN data phase (possibly zero device bytes against a nonzero
    // cbw_dtl -- cases 4/5: send what exists, stall IN, report residue)
    if (data_bytes_remaining < cbw_dtl)
    {
        stall_in_after_data = true;
    }

    bot_state = MSD_STATE_DATA_IN;
    msdPumpDataIn();
}

static void msdDispatchScsi(void)
{
    uint8_t opcode = cbw_cb[0];
    msd_lun_state_t *state = (cbw_lun < USB_MSD_NUM_LUNS)
            ? &msd_lun_state[cbw_lun] : &msd_lun_state[0];

    // LUN sanity -- INQUIRY to an invalid LUN must still answer (with
    // peripheral qualifier "not connected"), everything else fails
    if (cbw_lun >= USB_MSD_NUM_LUNS)
    {
        if (opcode == SCSI_INQUIRY)
        {
            memset(response_buf, 0, 36);
            response_buf[0] = 0x7Fu;    // not connected, unknown type
            response_buf[4] = 31;
            msdQueueResponse(36);
            msdEnterDataPhase(true, false);
            return;
        }
        msdFailCommand(SENSE_KEY_ILLEGAL_REQUEST, ASC_LUN_NOT_SUPPORTED, 0);
        msdEnterDataPhase(false, false);
        return;
    }

    // Pending UNIT ATTENTION preempts everything except the commands
    // that are defined to bypass it (SPC: INQUIRY, REQUEST SENSE)
    if (state->unit_attention && (opcode != SCSI_INQUIRY)
            && (opcode != SCSI_REQUEST_SENSE))
    {
        state->unit_attention = false;
        msdLunMediaReady(cbw_lun);      // refresh presence bookkeeping
        msdFailCommand(SENSE_KEY_UNIT_ATTENTION, ASC_MEDIA_CHANGED, 0);
        msdEnterDataPhase(false, false);
        return;
    }

    switch (opcode)
    {
        case SCSI_TEST_UNIT_READY:
            if (!msdLunMediaReady(cbw_lun))
            {
                msdFailCommand(SENSE_KEY_NOT_READY, ASC_MEDIUM_NOT_PRESENT, 0);
            }
            msdEnterDataPhase(false, false);
            break;

        case SCSI_REQUEST_SENSE:
            memset(response_buf, 0, 18);
            response_buf[0] = 0x70;     // current, fixed format
            response_buf[2] = state->sense_key;
            response_buf[7] = 10;       // additional sense length
            response_buf[12] = state->asc;
            response_buf[13] = state->ascq;
            msdSetSense(cbw_lun, SENSE_KEY_NO_SENSE, 0, 0);     // consumed
            msdQueueResponse(18);
            msdEnterDataPhase(true, false);
            break;

        case SCSI_INQUIRY:
            if (cbw_cb[1] & 0x03u)
            {
                // EVPD/CMDDT: no vital product data pages implemented
                msdFailCommand(SENSE_KEY_ILLEGAL_REQUEST,
                        ASC_INVALID_FIELD_IN_CDB, 0);
                msdEnterDataPhase(false, false);
                break;
            }
            memset(response_buf, 0, 36);
            response_buf[0] = 0x00;     // direct-access block device
            response_buf[1] = msd_luns[cbw_lun].inquiry_removable ? 0x80u : 0x00u;
            response_buf[2] = 0x02;     // ANSI SCSI-2
            response_buf[3] = 0x02;     // response data format
            response_buf[4] = 31;       // additional length (36 - 5)
            memcpy(&response_buf[8], "DrewM   ", 8);
            memcpy(&response_buf[16], msd_luns[cbw_lun].inquiry_product, 16);
            memcpy(&response_buf[32], "1.00", 4);
            msdQueueResponse(36);
            msdEnterDataPhase(true, false);
            break;

        case SCSI_MODE_SENSE_6:
            // Minimal: header only, no pages
            response_buf[0] = 3;        // mode data length (after this byte)
            response_buf[1] = 0;        // medium type
            response_buf[2] = msdLunWriteProtected(cbw_lun) ? 0x80u : 0x00u;
            response_buf[3] = 0;        // block descriptor length
            msdQueueResponse(4);
            msdEnterDataPhase(true, false);
            break;

        case SCSI_MODE_SENSE_10:
            memset(response_buf, 0, 8);
            response_buf[1] = 6;        // mode data length
            response_buf[3] = msdLunWriteProtected(cbw_lun) ? 0x80u : 0x00u;
            msdQueueResponse(8);
            msdEnterDataPhase(true, false);
            break;

        case SCSI_READ_FORMAT_CAPACITIES:
        {
            bool ready = msdLunMediaReady(cbw_lun);
            uint32_t count = ready ? msd_luns[cbw_lun].sectorCount() : 0u;
            memset(response_buf, 0, 12);
            response_buf[3] = 8;        // capacity list length
            response_buf[4] = (uint8_t)(count >> 24);
            response_buf[5] = (uint8_t)(count >> 16);
            response_buf[6] = (uint8_t)(count >> 8);
            response_buf[7] = (uint8_t)count;
            response_buf[8] = ready ? 0x02u : 0x03u;    // formatted / no media
            response_buf[10] = (uint8_t)(MSD_BLOCK_SIZE >> 8);
            response_buf[11] = (uint8_t)(MSD_BLOCK_SIZE & 0xFFu);
            msdQueueResponse(12);
            msdEnterDataPhase(true, false);
            break;
        }

        case SCSI_READ_CAPACITY_10:
        {
            if (!msdLunMediaReady(cbw_lun))
            {
                msdFailCommand(SENSE_KEY_NOT_READY, ASC_MEDIUM_NOT_PRESENT, 0);
                msdEnterDataPhase(false, false);
                break;
            }
            uint32_t lastLba = msd_luns[cbw_lun].sectorCount() - 1u;
            response_buf[0] = (uint8_t)(lastLba >> 24);
            response_buf[1] = (uint8_t)(lastLba >> 16);
            response_buf[2] = (uint8_t)(lastLba >> 8);
            response_buf[3] = (uint8_t)lastLba;
            response_buf[4] = (uint8_t)(MSD_BLOCK_SIZE >> 24);
            response_buf[5] = (uint8_t)(MSD_BLOCK_SIZE >> 16);
            response_buf[6] = (uint8_t)(MSD_BLOCK_SIZE >> 8);
            response_buf[7] = (uint8_t)(MSD_BLOCK_SIZE & 0xFFu);
            msdQueueResponse(8);
            msdEnterDataPhase(true, false);
            break;
        }

        case SCSI_READ_10:
        case SCSI_WRITE_10:
        {
            bool isWrite = (opcode == SCSI_WRITE_10);

            if (!msdLunMediaReady(cbw_lun))
            {
                msdFailCommand(SENSE_KEY_NOT_READY, ASC_MEDIUM_NOT_PRESENT, 0);
                msdEnterDataPhase(false, false);
                break;
            }

            if (isWrite && msdLunWriteProtected(cbw_lun))
            {
                msdFailCommand(SENSE_KEY_DATA_PROTECT, ASC_WRITE_PROTECTED, 0);
                msdEnterDataPhase(false, false);
                break;
            }

            uint32_t lba = ((uint32_t)cbw_cb[2] << 24) | ((uint32_t)cbw_cb[3] << 16)
                    | ((uint32_t)cbw_cb[4] << 8) | (uint32_t)cbw_cb[5];
            uint32_t blocks = ((uint32_t)cbw_cb[7] << 8) | (uint32_t)cbw_cb[8];

            if ((lba + blocks) > msd_luns[cbw_lun].sectorCount())
            {
                msdFailCommand(SENSE_KEY_ILLEGAL_REQUEST, ASC_LBA_OUT_OF_RANGE, 0);
                msdEnterDataPhase(false, false);
                break;
            }

            if (blocks == 0u)
            {
                msdEnterDataPhase(false, false);    // nothing to move: pass
                break;
            }

            xfer_lba = lba;
            xfer_blocks_remaining = blocks;
            data_bytes_remaining = blocks * MSD_BLOCK_SIZE;
            if (data_bytes_remaining > cbw_dtl)
            {
                // Host budgeted fewer bytes than the command moves (case
                // 7/13) -- honor the smaller bus budget, fail the command
                data_bytes_remaining = cbw_dtl;
                csw_status = MSD_CSW_STATUS_FAIL;
                msdSetSense(cbw_lun, SENSE_KEY_ILLEGAL_REQUEST,
                        ASC_INVALID_FIELD_IN_CDB, 0);
            }
            xfer_from_media = true;
            block_buf_len = 0;
            block_buf_off = 0;
            msdEnterDataPhase(!isWrite, isWrite);
            break;
        }

        case SCSI_START_STOP_UNIT:
        {
            bool loej = (cbw_cb[4] & 0x02u) != 0;
            bool start = (cbw_cb[4] & 0x01u) != 0;

            if (loej && !start)
            {
                // Eject ("Safely Remove" path): flush, then park the LUN
                if (msd_lun_state[cbw_lun].prevent_removal)
                {
                    msdFailCommand(SENSE_KEY_ILLEGAL_REQUEST,
                            ASC_MEDIUM_REMOVAL_PREVENTED, 0x02);
                }
                else
                {
                    msd_luns[cbw_lun].sync();
                    usb_msd_counters.staging_syncs++;
                    if (msd_luns[cbw_lun].removable)
                    {
                        msd_lun_state[cbw_lun].ejected = true;
                    }
                }
            }
            else if (loej && start)
            {
                msd_lun_state[cbw_lun].ejected = false;     // load
            }
            msdEnterDataPhase(false, false);
            break;
        }

        case SCSI_PREVENT_ALLOW_REMOVAL:
            msd_lun_state[cbw_lun].prevent_removal = (cbw_cb[4] & 0x01u) != 0;
            msdEnterDataPhase(false, false);
            break;

        case SCSI_VERIFY_10:
            // No BYTCHK support; media already verifies on read -- accept
            msdEnterDataPhase(false, false);
            break;

        case SCSI_SYNCHRONIZE_CACHE_10:
            if (!msd_luns[cbw_lun].sync())
            {
                msdFailCommand(SENSE_KEY_MEDIUM_ERROR, ASC_WRITE_ERROR, 0);
            }
            else
            {
                usb_msd_counters.staging_syncs++;
            }
            msdEnterDataPhase(false, false);
            break;

        default:
            usb_msd_counters.unsupported_scsi++;
            msdFailCommand(SENSE_KEY_ILLEGAL_REQUEST, ASC_INVALID_COMMAND, 0);
            msdEnterDataPhase(false, false);
            break;
    }
}

// Bounded wait for the (single-buffered) IN FIFO to drain, so one pump
// pass can move up to the block budget instead of one packet per event
static bool msdWaitBulkInReady(void)
{
    if (!USB_BulkInBusy())
    {
        return true;
    }

    uint32_t start = _CP0_GET_COUNT();
    while ((uint32_t)(_CP0_GET_COUNT() - start) < MSD_TX_DRAIN_WAIT_TICKS)
    {
        if (!USB_BulkInBusy())
        {
            return true;
        }
    }

    return false;
}

static void msdPumpDataIn(void)
{
    uint16_t maxPacket = USB_GetBulkMaxPacket();
    uint32_t budgetBytes = (uint32_t)USB_MSD_BLOCKS_PER_PASS * MSD_BLOCK_SIZE;

    while (data_bytes_remaining > 0u)
    {
        if (budgetBytes == 0u)
        {
            return;     // superloop breather; EP1TX event resumes us
        }

        if (!msdWaitBulkInReady())
        {
            return;
        }

        // Refill the staging buffer from the medium when it runs dry
        if (xfer_from_media && (block_buf_off >= block_buf_len))
        {
            if (xfer_blocks_remaining == 0u)
            {
                break;
            }
            if (!msd_luns[cbw_lun].readSectors(xfer_lba, block_buf, 1))
            {
                msdSetSense(cbw_lun, SENSE_KEY_MEDIUM_ERROR,
                        ASC_UNRECOVERED_READ_ERROR, 0);
                csw_status = MSD_CSW_STATUS_FAIL;
                stall_in_after_data = true;
                break;
            }
            usb_msd_counters.blocks_read++;
            xfer_lba++;
            xfer_blocks_remaining--;
            block_buf_len = MSD_BLOCK_SIZE;
            block_buf_off = 0;
        }

        const uint8_t *src;
        uint32_t available;
        if (xfer_from_media)
        {
            src = &block_buf[block_buf_off];
            available = (uint32_t)(block_buf_len - block_buf_off);
        }
        else
        {
            src = &response_buf[response_off];
            available = (uint32_t)(response_len - response_off);
        }

        uint16_t chunk = (uint16_t)((available < maxPacket) ? available : maxPacket);
        if ((uint32_t)chunk > data_bytes_remaining)
        {
            chunk = (uint16_t)data_bytes_remaining;
        }

        USB_BulkInWrite(src, chunk);

        if (xfer_from_media)
        {
            block_buf_off += chunk;
        }
        else
        {
            response_off += chunk;
        }
        data_bytes_remaining -= chunk;
        data_residue -= chunk;
        budgetBytes = (budgetBytes > chunk) ? (budgetBytes - chunk) : 0u;
    }

    // Data phase over (all sent, or truncated by a media error)
    data_bytes_remaining = 0;
    if (stall_in_after_data && (data_residue > 0u))
    {
        USB_BulkStall(true);
    }
    bot_state = MSD_STATE_SEND_CSW;
    msdTrySendCsw();
}

static void msdPumpDataOut(void)
{
    uint32_t budgetBytes = (uint32_t)USB_MSD_BLOCKS_PER_PASS * MSD_BLOCK_SIZE;

    while ((data_bytes_remaining > 0u) && (budgetBytes > 0u))
    {
        if (!USB_BulkOutAvailable())
        {
            return;     // next EP1RX event resumes us
        }

        uint16_t space = (uint16_t)(MSD_BLOCK_SIZE - block_buf_off);
        uint16_t received = USB_BulkOutRead(&block_buf[block_buf_off], space);

        if (received > space)
        {
            // Host packet overflows the block framing -- phase error
            csw_status = MSD_CSW_STATUS_PHASE_ERROR;
            USB_BulkStall(false);
            data_bytes_remaining = 0;
            bot_state = MSD_STATE_SEND_CSW;
            msdTrySendCsw();
            return;
        }

        block_buf_off += received;
        if ((uint32_t)received > data_bytes_remaining)
        {
            data_bytes_remaining = 0;
        }
        else
        {
            data_bytes_remaining -= received;
        }
        data_residue -= received;
        budgetBytes = (budgetBytes > received) ? (budgetBytes - received) : 0u;

        if (block_buf_off >= MSD_BLOCK_SIZE)
        {
            if (csw_status == MSD_CSW_STATUS_PASS)
            {
                if (!msd_luns[cbw_lun].writeSectors(xfer_lba, block_buf, 1))
                {
                    // Keep draining the host's remaining data (it won't
                    // stop mid-burst), but the command has failed
                    msdSetSense(cbw_lun, SENSE_KEY_MEDIUM_ERROR,
                            ASC_WRITE_ERROR, 0);
                    csw_status = MSD_CSW_STATUS_FAIL;
                }
                else
                {
                    usb_msd_counters.blocks_written++;
                }
            }
            xfer_lba++;
            block_buf_off = 0;
        }
    }

    if (data_bytes_remaining > 0u)
    {
        return;
    }

    // All expected data received. If the host budgeted more bytes than
    // the command consumes (case 11/13), refuse the excess.
    if (data_residue > 0u)
    {
        USB_BulkStall(false);
        if (csw_status == MSD_CSW_STATUS_PASS)
        {
            csw_status = MSD_CSW_STATUS_FAIL;
            msdSetSense(cbw_lun, SENSE_KEY_ILLEGAL_REQUEST,
                    ASC_INVALID_FIELD_IN_CDB, 0);
        }
    }

    bot_state = MSD_STATE_SEND_CSW;
    msdTrySendCsw();
}

static void msdTrySendCsw(void)
{
    // The CSW rides the bulk IN pipe: wait out an in-flight packet, and
    // if we stalled IN, wait for the host's CLEAR_FEATURE
    // (USB_MSD_EndpointHaltCleared re-pumps us)
    if (USB_BulkInStalled() || USB_BulkInBusy())
    {
        return;
    }

    uint8_t csw[MSD_CSW_LENGTH];
    csw[0] = (uint8_t)(MSD_CSW_SIGNATURE & 0xFFu);
    csw[1] = (uint8_t)((MSD_CSW_SIGNATURE >> 8) & 0xFFu);
    csw[2] = (uint8_t)((MSD_CSW_SIGNATURE >> 16) & 0xFFu);
    csw[3] = (uint8_t)((MSD_CSW_SIGNATURE >> 24) & 0xFFu);
    csw[4] = (uint8_t)(cbw_tag & 0xFFu);
    csw[5] = (uint8_t)((cbw_tag >> 8) & 0xFFu);
    csw[6] = (uint8_t)((cbw_tag >> 16) & 0xFFu);
    csw[7] = (uint8_t)((cbw_tag >> 24) & 0xFFu);
    csw[8] = (uint8_t)(data_residue & 0xFFu);
    csw[9] = (uint8_t)((data_residue >> 8) & 0xFFu);
    csw[10] = (uint8_t)((data_residue >> 16) & 0xFFu);
    csw[11] = (uint8_t)((data_residue >> 24) & 0xFFu);
    csw[12] = csw_status;

    USB_BulkInWrite(csw, MSD_CSW_LENGTH);

    if (csw_status != MSD_CSW_STATUS_PASS)
    {
        usb_msd_counters.csw_failures++;
    }

    bot_state = MSD_STATE_WAIT_CBW;

    // A CBW may already be waiting behind the data we just finished
    msdReceiveCbw();
}

void USB_MSD_TimedTasks(void)
{
    if (!W25Q128JV_Disk_IsDirty())
    {
        return;
    }

    if (W25Q128JV_Disk_TicksSinceLastWrite() >= MSD_FLUSH_IDLE_TICKS)
    {
        if (W25Q128JV_Disk_Sync())
        {
            usb_msd_counters.staging_syncs++;
        }
        // Failed sync retries on the next pass -- the dirty flag is
        // still set and the tick keeps receding
    }
}

// ---- Media ownership (yield-to-host policy, usb_msd.h) ----

static void msdYieldMediaToHost(void)
{
    if (usb_msd_media_owned_by_host)
    {
        return;
    }

    SDFileIO_UnmountKeepPower();
    FlashFileIO_Unmount();
    usb_msd_media_owned_by_host = 1;

    terminalTextAttributes(MAGENTA_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("USB host attached: SD and FLASH volumes yielded to host\r\n\r\n");
    terminalTextAttributesReset();
}

static void msdHandMediaBack(void)
{
    if (!usb_msd_media_owned_by_host)
    {
        return;
    }

    // Nothing the host wrote may be left RAM-only
    if (W25Q128JV_Disk_Sync())
    {
        usb_msd_counters.staging_syncs++;
    }

    usb_msd_media_owned_by_host = 0;

    bool sdRemounted = false;
    if (SD_Card_IsPresent())
    {
        // A card swapped in while the host owned the bus was initialized
        // by msdLunMediaReady(); a card that was never absent is still
        // initialized -- either way only the FatFs view needs rebuilding
        sdRemounted = (SD_Card_GetInfo() != NULL) && SDFileIO_Mount();
    }

    bool flashRemounted = FlashFileIO_MountAndFormatIfNeeded();

    terminalTextAttributes(MAGENTA_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("USB host detached: local volumes remounted (SD: %s, FLASH: %s)\r\n",
            sdRemounted ? "mounted" : "not mounted",
            flashRemounted ? "mounted" : "FAILED");
    terminalTextAttributesReset();
}

void USB_MSD_ConfiguredHook(bool configured)
{
    bot_state = MSD_STATE_WAIT_CBW;

    if (configured)
    {
        msdYieldMediaToHost();
    }
    else
    {
        msdHandMediaBack();
    }
}

void USB_MSD_BusResetHook(void)
{
    // A bus reset deconfigures the device (host is about to re-enumerate
    // or gave up) -- transport dead until re-configured, media comes home
    bot_state = MSD_STATE_WAIT_CBW;
    msdHandMediaBack();
}

void USB_MSD_DetachHook(void)
{
    bot_state = MSD_STATE_WAIT_CBW;
    msdHandMediaBack();
}

void USB_MSD_ResumeHook(void)
{
    // Host resumed a still-configured device (no SET_CONFIGURATION coming)
    // -- take the media back off the local mounts before traffic restarts
    msdYieldMediaToHost();
}

void USB_MSD_NotifyWriteProtectChanged(void)
{
    for (uint8_t lun = 0; lun < USB_MSD_NUM_LUNS; lun++)
    {
        if (msd_luns[lun].isWriteProtected != NULL)
        {
            msd_lun_state[lun].unit_attention = true;
        }
    }
}

void USB_MSD_EndpointHaltCleared(bool inEndpoint)
{
    (void)inEndpoint;

    // If a CSW was gated on our own case-13/error stall, it can go now
    msdPump();
}

void USB_MSD_PrintStatus(void)
{
    static const char *bot_state_names[] = {
        "WAIT CBW", "DATA IN", "DATA OUT", "SEND CSW", "STALLED (await reset)"
    };

    terminalTextAttributesReset();
    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);

    printf("    BOT State:                                %s\n\r",
            bot_state_names[bot_state]);

    if (usb_msd_media_owned_by_host)
        terminalTextAttributes(YELLOW_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("    Media Owned By USB Host:                  %s\n\r",
            usb_msd_media_owned_by_host ? "T" : "F");
    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);

    for (uint8_t lun = 0; lun < USB_MSD_NUM_LUNS; lun++)
    {
        bool present = msd_luns[lun].isPresent();
        printf("    LUN %u (%s):\n\r", (unsigned)lun,
                (lun == 0u) ? "microSD" : "SPI flash");
        printf("        Media Present:                        %s\n\r",
                present ? "T" : "F");
        if (present)
        {
            printf("        Capacity:                             %lu blocks (%lu KB)\n\r",
                    (unsigned long)msd_luns[lun].sectorCount(),
                    (unsigned long)(msd_luns[lun].sectorCount() / 2u));
        }
        printf("        Ejected / Removal Prevented:          %s / %s\n\r",
                msd_lun_state[lun].ejected ? "T" : "F",
                msd_lun_state[lun].prevent_removal ? "T" : "F");
        printf("        Sense (key/ASC/ASCQ):                 %02X/%02X/%02X\n\r",
                msd_lun_state[lun].sense_key, msd_lun_state[lun].asc,
                msd_lun_state[lun].ascq);
    }

    if (W25Q128JV_Disk_IsDirty()) terminalTextAttributes(YELLOW_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("    Flash Staging Dirty:                      %s\n\r",
            W25Q128JV_Disk_IsDirty() ? "T" : "F");
    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);

    printf("    CBWs Received:                            %lu\n\r",
            (unsigned long)usb_msd_counters.cbws_received);
    printf("    Blocks Read / Written:                    %lu / %lu\n\r",
            (unsigned long)usb_msd_counters.blocks_read,
            (unsigned long)usb_msd_counters.blocks_written);
    printf("    Staging Syncs:                            %lu\n\r",
            (unsigned long)usb_msd_counters.staging_syncs);

    if (usb_msd_counters.invalid_cbws || usb_msd_counters.csw_failures
            || usb_msd_counters.unsupported_scsi)
        terminalTextAttributes(YELLOW_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("    Invalid CBWs:                             %lu\n\r",
            (unsigned long)usb_msd_counters.invalid_cbws);
    printf("    Failed CSWs:                              %lu\n\r",
            (unsigned long)usb_msd_counters.csw_failures);
    printf("    Unsupported SCSI Commands:                %lu\n\r",
            (unsigned long)usb_msd_counters.unsupported_scsi);

    printf("    BOT Resets:                               %lu\n\r",
            (unsigned long)usb_msd_counters.bot_resets);

    terminalTextAttributesReset();
}
