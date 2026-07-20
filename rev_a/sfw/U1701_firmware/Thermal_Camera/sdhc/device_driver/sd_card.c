/*******************************************************************************
  SD Memory Card Protocol Driver

  File Name:
    sd_card.c

  Summary:
    SD command/protocol state machine built on sdhc.h. See sd_card.h for
    the layering rationale and the two hardware-bring-up-verify items
    (card-detect polarity, CID/CSD response-word mapping).
*******************************************************************************/

#include <xc.h>
#include <stdio.h>
#include <string.h>

#include "sdhc/device_driver/sd_card.h"
#include "sdhc/sdhc.h"
#include "core/device_control.h"
#include "gpio/pin_macros.h"
#include "usb_uart/terminal_control.h"

// SD command indices (physical layer spec) -- this is the one file in
// the stack that's allowed to know these
#define SD_CMD_GO_IDLE_STATE            0u
#define SD_CMD_ALL_SEND_CID             2u
#define SD_CMD_SEND_RELATIVE_ADDR       3u
#define SD_CMD_SELECT_CARD              7u
#define SD_CMD_SEND_IF_COND             8u
#define SD_CMD_SEND_CSD                 9u
#define SD_CMD_STOP_TRANSMISSION        12u
#define SD_CMD_SET_BLOCKLEN             16u
#define SD_CMD_READ_SINGLE_BLOCK        17u
#define SD_CMD_READ_MULTIPLE_BLOCK      18u
#define SD_CMD_WRITE_BLOCK              24u
#define SD_CMD_WRITE_MULTIPLE_BLOCK     25u
#define SD_CMD_APP_CMD                  55u
#define SD_ACMD_SET_BUS_WIDTH           6u
#define SD_ACMD_SD_SEND_OP_COND         41u

#define SD_CARD_TIMEOUT_TICKS(us)       ((uint32_t)(((uint64_t)SYSCLK_INT / 2u) * (us) / 1000000u))
#define SD_CARD_ACMD41_TIMEOUT_TICKS    SD_CARD_TIMEOUT_TICKS(1000000u)   // SD spec allows up to 1s
#define SD_CARD_POWERDOWN_TIMEOUT_TICKS SD_CARD_TIMEOUT_TICKS(500000u)    // 500ms for any in-flight transfer to finish

// Default Speed (non-High-Speed) operating clock -- High-Speed (50MHz)
// negotiation is a phase-2 item, see sd_card.h
#define SD_CARD_OPERATING_CLOCK_HZ      25000000UL


#define SD_CARD_MAX_SDSC_CAPACITY_BLOCKS  (2UL * 1024u * 1024u * 1024u / 512u)   // 2GB
#define SD_CARD_MAX_SDHC_CAPACITY_BLOCKS  (32UL * 1024u * 1024u * 1024u / 512u)  // 32GB

static sd_card_info_t sd_card_info;
static bool sd_card_info_valid = false;
static uint16_t sd_card_rca = 0;

// See sd_card.h -- set by the Port A change-notice ISR on any card-detect
// edge, consumed by SDFileIO_HotSwapTasks() in the main loop
volatile uint8_t sd_card_hotswap_event = 0;

// Calibrated microsecond delay via CP0 Count (increments at SYSCLK/2,
// see SD_CARD_TIMEOUT_TICKS above) -- unlike softwareDelay()
// (core/device_control.c, a raw NOP-counting loop with no fixed
// relationship to real time), this holds regardless of compiler
// optimization level or core clock changes. Card power-up settling is
// timing-critical (load-switch rise time, SD spec supply-ramp
// requirement) so it can't tolerate softwareDelay()'s uncalibrated
// duration -- see sd_card.h bring-up notes.
void SD_Card_DelayUs(uint32_t us)
{
    uint32_t start = _CP0_GET_COUNT();
    uint32_t ticks = SD_CARD_TIMEOUT_TICKS(us);
    while ((uint32_t)(_CP0_GET_COUNT() - start) < ticks)
    {
        // busy-wait
    }
}

// Sends CMD55 (APP_CMD, addressed to the current RCA -- 0 before CMD3 has
// assigned one, which is required/correct during ACMD41 polling) followed
// by the requested application command. Returns false if either command
// fails; `response` may be NULL if the caller doesn't need it.
static bool SD_Card_SendAppCommand(uint8_t acmd, uint32_t argument, sdhc_response_type_t respType, uint32_t response[4])
{
    if (!SDHC_SendCommand(SD_CMD_APP_CMD, (uint32_t)sd_card_rca << 16, SDHC_RESP_R1, false))
    {
        return false;
    }

    if (!SDHC_SendCommand(acmd, argument, respType, false))
    {
        return false;
    }

    if (response != NULL)
    {
        SDHC_GetResponse(response);
    }

    return true;
}

// Extracts CID fields from a completed CMD2/CMD9-style R2 response.
// ASSUMES resp[3] holds the highest-order 24 bits of the 120-bit CID
// payload (CID[127:104]) and resp[0] the lowest 32 bits (CID[39:8]) -- see
// sd_card.h file header. Re-derive this extraction if fields come back
// garbled during bring-up (e.g. manufacturer/product name reads as
// garbage ASCII).
static void SD_Card_ParseCID(const uint32_t resp[4], sd_card_info_t *info)
{
    uint8_t b[15];

    b[0]  = (uint8_t)((resp[3] >> 16) & 0xFFu);
    b[1]  = (uint8_t)((resp[3] >> 8)  & 0xFFu);
    b[2]  = (uint8_t)(resp[3] & 0xFFu);
    b[3]  = (uint8_t)((resp[2] >> 24) & 0xFFu);
    b[4]  = (uint8_t)((resp[2] >> 16) & 0xFFu);
    b[5]  = (uint8_t)((resp[2] >> 8)  & 0xFFu);
    b[6]  = (uint8_t)(resp[2] & 0xFFu);
    b[7]  = (uint8_t)((resp[1] >> 24) & 0xFFu);
    b[8]  = (uint8_t)((resp[1] >> 16) & 0xFFu);
    b[9]  = (uint8_t)((resp[1] >> 8)  & 0xFFu);
    b[10] = (uint8_t)(resp[1] & 0xFFu);
    b[11] = (uint8_t)((resp[0] >> 24) & 0xFFu);
    b[12] = (uint8_t)((resp[0] >> 16) & 0xFFu);
    b[13] = (uint8_t)((resp[0] >> 8)  & 0xFFu);
    b[14] = (uint8_t)(resp[0] & 0xFFu);

    info->manufacturer_id = b[0];

    info->oem_id[0] = (char)b[1];
    info->oem_id[1] = (char)b[2];
    info->oem_id[2] = '\0';

    info->product_name[0] = (char)b[3];
    info->product_name[1] = (char)b[4];
    info->product_name[2] = (char)b[5];
    info->product_name[3] = (char)b[6];
    info->product_name[4] = (char)b[7];
    info->product_name[5] = '\0';

    info->product_revision = b[8];
    info->serial_number = ((uint32_t)b[9] << 24) | ((uint32_t)b[10] << 16) | ((uint32_t)b[11] << 8) | b[12];

    uint8_t yy = (uint8_t)(((b[13] & 0x0Fu) << 4) | (b[14] >> 4));
    info->manufacture_year = (uint16_t)(2000u + yy);
    info->manufacture_month = (uint8_t)(b[14] & 0x0Fu);
}

// Extracts capacity from a completed CMD9 (SEND_CSD) R2 response. Same
// resp[]-word assumption as SD_Card_ParseCID(); CSD Version 1.0 (SDSC)
// and Version 2.0 (SDHC/SDXC) use different capacity field layouts, both
// per the SD Physical Layer Simplified Specification.
static void SD_Card_ParseCSD(const uint32_t resp[4], sd_card_info_t *info)
{
    uint8_t csdStructure = (uint8_t)((resp[3] >> 22) & 0x3u);

    if (csdStructure == 0u)
    {
        // CSD 1.0: capacity = (C_SIZE+1) * 2^(C_SIZE_MULT+2) * 2^READ_BL_LEN bytes
        uint8_t readBlLen = (uint8_t)((resp[2] >> 8) & 0xFu);
        uint32_t cSize = ((resp[2] & 0x3u) << 10) | ((resp[1] >> 22) & 0x3FFu);
        uint8_t cSizeMult = (uint8_t)((resp[1] >> 7) & 0x7u);

        uint32_t blockCount = (cSize + 1u) << (cSizeMult + 2u);
        uint32_t blockLen = 1UL << readBlLen;

        info->capacity_blocks = (blockCount * blockLen) / 512u;
    }
    else
    {
        // CSD 2.0: capacity = (C_SIZE+1) * 512KB = (C_SIZE+1) * 1024 blocks
        uint32_t cSize = (resp[1] >> 8) & 0x3FFFFFu;
        info->capacity_blocks = (cSize + 1u) * 1024u;
    }
}

bool SD_Card_IsPresent(void)
{
    const uint8_t requiredConsistentReads = 5u;
    uint8_t consistent = 0;
    uint8_t lastLevel = (uint8_t)SD_CARD_DETECT_PIN;

    while (consistent < requiredConsistentReads)
    {
        SD_Card_DelayUs(1000u);
        uint8_t level = (uint8_t)SD_CARD_DETECT_PIN;
        if (level != lastLevel)
        {
            lastLevel = level;
            consistent = 0;
        }
        else
        {
            consistent++;
        }
    }

    // ASSUMES ACTIVE-LOW (card present -> pin reads low) -- see sd_card.h
    return (lastLevel == LOW);
}

const sd_card_info_t *SD_Card_GetInfo(void)
{
    return sd_card_info_valid ? &sd_card_info : NULL;
}

// Sleep-entry ONLY -- do not call this on mount failure, eject, or card
// removal. The card-detect pull-up is powered from the switched card rail,
// so dropping SD_PWR_EN_PIN makes SD_CARD_DETECT_PIN drift low and read
// as "card present" with the slot empty. During normal operation the rail
// must stay high for card-detect to mean anything (this caused a
// remove/re-insert infinite mount loop on 2026-07-18: power-down -> CD
// drifts low -> "insertion" edge -> init re-applies power -> CD reads
// high/"no card" -> init fails and powers down -> repeat). Use
// SD_Card_Deinitialize() for every state-teardown that isn't sleep.
bool SD_Card_PowerDown(void)
{
    uint32_t start = _CP0_GET_COUNT();
    while (SDHC_IsDataLineBusy() && ((uint32_t)(_CP0_GET_COUNT() - start) < SD_CARD_POWERDOWN_TIMEOUT_TICKS))
    {
        // wait for any in-flight transfer to finish before removing power
    }

    SD_PWR_EN_PIN = LOW;
    sd_card_info_valid = false;
    sd_card_rca = 0;

    return true;
}

// Clears the cached card state (so SD_Card_GetInfo() reports no card)
// WITHOUT touching SD_PWR_EN_PIN -- see SD_Card_PowerDown() for why the
// rail stays up in normal operation.
void SD_Card_Deinitialize(void)
{
    sd_card_info_valid = false;
    sd_card_rca = 0;

    // Quiet the bus while no card is mounted -- see SDHC_StopClock():
    // leaving the 25MHz operating clock free-running into an empty slot
    // means the next hot-inserted card clocks in contact-bounce garbage
    // while seating and misses the first real command (the deterministic
    // first-CMD8 CTOEIF on every hot insert, 2026-07-18)
    SDHC_StopClock();
}

bool SD_Card_Initialize(void)
{
    sd_card_info_valid = false;
    
    sd_card_rca = 0;

    // Card power is applied once (normally the first Initialize after
    // boot) and then left on for good -- card-detect sensing depends on
    // the rail staying up, see SD_Card_PowerDown(). Only a first-time
    // (or post-sleep) power application needs the settling delay:
    // load-switch turn-on time (measured ~5-6.5ms rise for this board's
    // CT = 0.1uF into a 10uF load, see bring-up notes) + SD spec's
    // required supply-ramp/74-clock-cycle wait before the first command.
    // On a hot re-insert the rail is already up (the card did its own
    // power-on reset as its contacts mated) so no wait is needed.
    if (SD_PWR_EN_PIN == LOW)
    {
        SD_PWR_EN_PIN = HIGH;
        SD_Card_DelayUs(50000u);
    }

    if (!SD_Card_IsPresent())
    {
        terminalTextAttributes(YELLOW_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("    SD_Card_Initialize: card-detect pin reports no card present\r\n");
        terminalTextAttributesReset();

        // Quiet the bus on this path too (not just card removal) --
        // otherwise a card-less boot leaves main.c's SDHC_Initialize()
        // 400kHz clock free-running into the empty slot, recreating the
        // insertion bounce-sampling hazard SDHC_StopClock() exists for
        SD_Card_Deinitialize();
        return false;
    }

    // Full host-controller re-init before every identification attempt,
    // replicating the known-good cold-boot path exactly. SWRALL wipes
    // whatever a previous card session or a mid-removal event left behind
    // (SDHCI hosts may auto-clear SDBP bus power on a removal event, on
    // top of the 25MHz/4-bit operating settings a successful init leaves
    // configured -- either one makes a hot-inserted card deaf to CMD8),
    // and SDHC_Initialize() rebuilds everything from scratch: internal
    // clock, bus power, 400kHz identification clock, 1-bit width,
    // interrupt plumbing. At boot this repeats what main.c's own
    // SDHC_Initialize() call just did, which is harmless. This was the
    // 2026-07-18 fix for "cold boot mounts fine, hot re-insert times out
    // on CMD8 even at the correct identification clock".
    if (!SDHC_Initialize())
    {
        terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("    SD_Card_Initialize: SDHC controller re-init failed\r\n");
        terminalTextAttributesReset();
        return false;
    }

    // SD spec: the card must see >= 74 SDCLK cycles between clock start
    // and the first command (185us at 400kHz). At cold boot main.c's own
    // SDHC_Initialize() call left the clock running long before we get
    // here, but on a hot insert the re-init above just restarted a
    // stopped clock (see SD_Card_Deinitialize()) microseconds ago.
    SD_Card_DelayUs(1000u);

    uint32_t response[4];

    // CMD0: GO_IDLE_STATE. No response expected; retry a couple of times
    // since real cards sometimes miss the very first command after
    // power-up.
    bool idleOk = false;
    for (uint8_t attempt = 0; (attempt < 3u) && !idleOk; attempt++)
    {
        idleOk = SDHC_SendCommand(SD_CMD_GO_IDLE_STATE, 0u, SDHC_RESP_NONE, false);
    }
    if (!idleOk)
    {
        terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("    SD_Card_Initialize: CMD0 (GO_IDLE_STATE) failed after 3 attempts\r\n");
        terminalTextAttributesReset();
        SD_Card_Deinitialize();
        return false;
    }

    // CMD8: SEND_IF_COND -- probes for Physical Layer Spec v2.00+
    // support (voltage window 2.7-3.6V + 0xAA check pattern). A timeout/
    // error here means a Version 1.x card (or not an SD card at all);
    // either way this driver proceeds with a legacy (non-HCS) ACMD41.
    // Retried like CMD0: a hot-inserted card can miss the first CMD8
    // while still finishing its own power-on reset, and a missed CMD8 is
    // expensive -- an SDHC/SDXC card that never saw CMD8 will NEVER
    // report ready to ACMD41 (stays busy, OCR bit31 = 0), so the
    // misclassification costs the full 1s ACMD41 timeout before failing.
    // A genuine v1.x card just fails all three attempts (~30ms extra).
    bool v2OrLater = false;
    for (uint8_t attempt = 0; (attempt < 3u) && !v2OrLater; attempt++)
    {
        if (attempt > 0u)
        {
            SD_Card_DelayUs(10000u);
        }
        v2OrLater = SDHC_SendCommand(SD_CMD_SEND_IF_COND, 0x1AAu, SDHC_RESP_R6R7, false);
    }
    if (v2OrLater)
    {
        SDHC_GetResponse(response);
        if ((response[0] & 0xFFu) != 0xAAu)
        {
            terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
            printf("    SD_Card_Initialize: CMD8 echo pattern mismatch (got 0x%02X, expected 0xAA)\r\n",
                    (unsigned)(response[0] & 0xFFu));
            terminalTextAttributesReset();
            SD_Card_Deinitialize();
            return false;
        }
    }

    // ACMD41: SD_SEND_OP_COND -- poll until the card clears its busy bit
    // (response bit 31). HCS (argument bit 30) requests High Capacity
    // support if CMD8 succeeded; the response's CCS bit (bit 30) then
    // tells us whether the card actually is SDHC/SDXC.
    uint32_t acmd41Arg = 0x00FF8000u | (v2OrLater ? (1UL << 30) : 0u);
    bool ready = false;
    uint32_t ocr = 0;
    uint32_t start = _CP0_GET_COUNT();

    while ((uint32_t)(_CP0_GET_COUNT() - start) < SD_CARD_ACMD41_TIMEOUT_TICKS)
    {
        if (!SD_Card_SendAppCommand(SD_ACMD_SD_SEND_OP_COND, acmd41Arg, SDHC_RESP_R3, response))
        {
            terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
            printf("    SD_Card_Initialize: ACMD41 (SD_SEND_OP_COND) command failed\r\n");
            terminalTextAttributesReset();
            SD_Card_Deinitialize();
            return false;
        }

        ocr = response[0];
        if ((ocr & (1UL << 31)) != 0u)
        {
            ready = true;
            break;
        }

        SD_Card_DelayUs(1000u);
    }

    if (!ready)
    {
        terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("    SD_Card_Initialize: ACMD41 timed out waiting for card ready (last OCR=0x%08lX)\r\n",
                (unsigned long)ocr);
        terminalTextAttributesReset();
        SD_Card_Deinitialize();
        return false;
    }

    bool isHighCapacity = v2OrLater && ((ocr & (1UL << 30)) != 0u);

    // CMD2: ALL_SEND_CID
    if (!SDHC_SendCommand(SD_CMD_ALL_SEND_CID, 0u, SDHC_RESP_R2, false))
    {
        terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("    SD_Card_Initialize: CMD2 (ALL_SEND_CID) failed\r\n");
        terminalTextAttributesReset();
        SD_Card_Deinitialize();
        return false;
    }
    SDHC_GetResponse(response);
    SD_Card_ParseCID(response, &sd_card_info);

    // CMD3: SEND_RELATIVE_ADDR -- card publishes its own RCA in the
    // response's upper 16 bits
    if (!SDHC_SendCommand(SD_CMD_SEND_RELATIVE_ADDR, 0u, SDHC_RESP_R6R7, false))
    {
        terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("    SD_Card_Initialize: CMD3 (SEND_RELATIVE_ADDR) failed\r\n");
        terminalTextAttributesReset();
        SD_Card_Deinitialize();
        return false;
    }
    SDHC_GetResponse(response);
    sd_card_rca = (uint16_t)(response[0] >> 16);

    // CMD9: SEND_CSD
    if (!SDHC_SendCommand(SD_CMD_SEND_CSD, (uint32_t)sd_card_rca << 16, SDHC_RESP_R2, false))
    {
        terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("SD_Card_Initialize: CMD9 (SEND_CSD) failed\r\n");
        terminalTextAttributesReset();
        SD_Card_Deinitialize();
        return false;
    }
    SDHC_GetResponse(response);
    SD_Card_ParseCSD(response, &sd_card_info);

    // CMD7: SELECT_CARD -- moves the card into Transfer State
    if (!SDHC_SendCommand(SD_CMD_SELECT_CARD, (uint32_t)sd_card_rca << 16, SDHC_RESP_R1B, false))
    {
        terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("    SD_Card_Initialize: CMD7 (SELECT_CARD) failed\r\n");
        terminalTextAttributesReset();
        SD_Card_Deinitialize();
        return false;
    }

    if (!isHighCapacity)
    {
        // SDSC cards aren't guaranteed to default to a 512-byte block
        // length -- force it. SDHC/SDXC are always fixed at 512 and
        // ignore this.
        if (!SDHC_SendCommand(SD_CMD_SET_BLOCKLEN, 512u, SDHC_RESP_R1, false))
        {
            terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
            printf("    SD_Card_Initialize: CMD16 (SET_BLOCKLEN) failed\r\n");
            terminalTextAttributesReset();
            SD_Card_Deinitialize();
            return false;
        }
    }

    // ACMD6: SET_BUS_WIDTH (argument 0x2 = 4-bit) -- tell the card, then
    // switch the host side to match
    if (!SD_Card_SendAppCommand(SD_ACMD_SET_BUS_WIDTH, 0x2u, SDHC_RESP_R1, NULL))
    {
        terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("    SD_Card_Initialize: ACMD6 (SET_BUS_WIDTH) failed\r\n");
        terminalTextAttributesReset();
        SD_Card_Deinitialize();
        return false;
    }
    SDHC_SetBusWidth(true);

    // Raise the clock from the 400kHz identification rate to Default
    // Speed operating rate
    if (!SDHC_SetClockDivider(SD_CARD_OPERATING_CLOCK_HZ))
    {
        terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("    SD_Card_Initialize: failed to raise SDCLK to operating speed\r\n");
        terminalTextAttributesReset();
        SD_Card_Deinitialize();
        return false;
    }

    sd_card_info.type = isHighCapacity ? SD_CARD_TYPE_SDHC : SD_CARD_TYPE_SDSC;
    if ((sd_card_info.type == SD_CARD_TYPE_SDHC) && (sd_card_info.capacity_blocks > SD_CARD_MAX_SDHC_CAPACITY_BLOCKS))
    {
        // SDXC is the same v2 CSD/HCS path as SDHC, distinguished only by
        // capacity exceeding 32GB
        sd_card_info.type = SD_CARD_TYPE_SDXC;
    }
    sd_card_info.wide_bus_active = true;
    sd_card_info.high_speed_capable = false; // phase-2, see sd_card.h

    sd_card_info_valid = true;
    return true;
}

bool SD_Card_ReadBlocks(uint32_t startBlock, uint8_t *buffer, uint16_t blockCount)
{
    if (!sd_card_info_valid || (buffer == NULL) || (blockCount == 0u))
    {
        return false;
    }

    // SDSC cards are byte-addressed; SDHC/SDXC are block-addressed
    uint32_t argument = (sd_card_info.type == SD_CARD_TYPE_SDSC) ? (startBlock * 512u) : startBlock;
    uint8_t cmdIndex = (blockCount > 1u) ? SD_CMD_READ_MULTIPLE_BLOCK : SD_CMD_READ_SINGLE_BLOCK;

    // REGRESSION 2026-07-20: enabling ADMA2 whenever SDHC_IsADMA2Supported()
    // hung the DATA line on real hardware (CMD17 stuck on CINHDAT forever,
    // never recovering) -- the ADMA2 descriptor format this driver uses was
    // already flagged in sdhc.c as "not confirmed against the PIC32MZ-DA
    // Family Reference Manual for this specific silicon," and turning it on
    // for the first time exercised that gap on a real card. Forced back to
    // PIO-only, the confirmed-working path. SDHC_PrepareADMA2Transfer()/
    // SDHC_WaitADMA2Transfer() in sdhc.c still have their three known bugs
    // fixed (DMASEL, SDHCAADDR ordering, D-cache maintenance) but are not
    // wired up here -- re-enabling needs actual hardware-in-loop debugging
    // of the ADMA2 engine itself, not just those three bugs.
    bool useDMA = false;

    SDHC_ConfigureBlockTransfer(512u, blockCount, false, useDMA);

    if (!SDHC_SendCommand(cmdIndex, argument, SDHC_RESP_R1, true))
    {
        return false;
    }

    bool ok = useDMA
            ? SDHC_WaitADMA2Transfer(buffer, 512u, blockCount, false)
            : SDHC_TransferBlocksPIO(buffer, 512u, blockCount, false);

    if (blockCount > 1u)
    {
        // No Auto CMD12 configured in sdhc.c -- multi-block transfers
        // must be explicitly stopped
        SDHC_SendCommand(SD_CMD_STOP_TRANSMISSION, 0u, SDHC_RESP_R1B, false);
    }

    return ok;
}

bool SD_Card_WriteBlocks(uint32_t startBlock, const uint8_t *buffer, uint16_t blockCount)
{
    if (!sd_card_info_valid || (buffer == NULL) || (blockCount == 0u))
    {
        return false;
    }

    uint32_t argument = (sd_card_info.type == SD_CARD_TYPE_SDSC) ? (startBlock * 512u) : startBlock;
    uint8_t cmdIndex = (blockCount > 1u) ? SD_CMD_WRITE_MULTIPLE_BLOCK : SD_CMD_WRITE_BLOCK;

    // See SD_Card_ReadBlocks() -- forced to PIO after the 2026-07-20 ADMA2
    // hang regression.
    bool useDMA = false;

    SDHC_ConfigureBlockTransfer(512u, blockCount, true, useDMA);

    if (!SDHC_SendCommand(cmdIndex, argument, SDHC_RESP_R1, true))
    {
        return false;
    }

    bool ok = useDMA
            ? SDHC_WaitADMA2Transfer((uint8_t *)(void *)buffer, 512u, blockCount, true)
            : SDHC_TransferBlocksPIO((uint8_t *)(void *)buffer, 512u, blockCount, true);

    if (blockCount > 1u)
    {
        SDHC_SendCommand(SD_CMD_STOP_TRANSMISSION, 0u, SDHC_RESP_R1B, false);
    }

    return ok;
}

static const char *SD_Card_TypeString(sd_card_type_t type)
{
    switch (type)
    {
        case SD_CARD_TYPE_SDSC: return "SDSC (Standard Capacity)";
        case SD_CARD_TYPE_SDHC: return "SDHC (High Capacity)";
        case SD_CARD_TYPE_SDXC: return "SDXC (eXtended Capacity)";
        default:                return "Unknown";
    }
}

void SD_Card_PrintInfo(void)
{
    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, BOLD_FONT);
    printf("    --- SD Card ---\n\r");

    if (!sd_card_info_valid)
    {
        terminalTextAttributes(YELLOW_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("    No card currently initialized\n\r");
        terminalTextAttributesReset();
        return;
    }

    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("    Type: %s\n\r", SD_Card_TypeString(sd_card_info.type));
    printf("    Capacity: %lu blocks (%lu MB)\n\r",
            (unsigned long)sd_card_info.capacity_blocks,
            (unsigned long)(((uint64_t)sd_card_info.capacity_blocks * 512u) / (1024u * 1024u)));
    printf("    Manufacturer ID: 0x%02X\n\r", (unsigned int)sd_card_info.manufacturer_id);
    printf("    OEM ID: %s\n\r", sd_card_info.oem_id);
    printf("    Product Name: %s\n\r", sd_card_info.product_name);
    printf("    Product Revision: %u.%u\n\r",
            (unsigned int)(sd_card_info.product_revision >> 4),
            (unsigned int)(sd_card_info.product_revision & 0x0Fu));
    printf("    Serial Number: 0x%08lX\n\r", (unsigned long)sd_card_info.serial_number);
    printf("    Manufacture Date: %u-%02u\n\r",
            (unsigned int)sd_card_info.manufacture_year, (unsigned int)sd_card_info.manufacture_month);
    printf("    Bus Width: %s\n\r", sd_card_info.wide_bus_active ? "4-bit" : "1-bit");
    printf("    High-Speed Mode: %s\n\r", sd_card_info.high_speed_capable ? "active" : "not active (Default Speed)");

    terminalTextAttributesReset();
}
