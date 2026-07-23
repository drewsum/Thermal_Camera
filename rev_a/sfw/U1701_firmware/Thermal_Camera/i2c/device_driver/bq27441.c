/*******************************************************************************
  BQ27441-G1 I2C Li-Ion Fuel Gauge Driver

  File Name:
    bq27441.c

  Summary:
    Driver for the Texas Instruments BQ27441-G1 single-cell Li-Ion
    Impedance Track fuel gauge, built on i2c_master.h.
*******************************************************************************/

#include "i2c/device_driver/bq27441.h"
#include "i2c/i2c_master.h"
#include "usb_uart/terminal_control.h"

#include <stdio.h>

// *****************************************************************************
// Section: Register Map
// *****************************************************************************
// Standard commands are all 16-bit and transferred LITTLE-ENDIAN (LSB at
// `reg`, MSB at `reg`+1) -- the OPPOSITE byte order from MCP9804/INA231A.

#define BQ27441_REG_CONTROL            0x00u
#define BQ27441_REG_TEMPERATURE        0x02u
#define BQ27441_REG_VOLTAGE            0x04u
#define BQ27441_REG_FLAGS              0x06u
#define BQ27441_REG_NOM_CAPACITY       0x08u
#define BQ27441_REG_AVAIL_CAPACITY     0x0Au
#define BQ27441_REG_REM_CAPACITY       0x0Cu
#define BQ27441_REG_FULL_CAPACITY      0x0Eu
#define BQ27441_REG_AVG_CURRENT        0x10u   // signed
#define BQ27441_REG_STDBY_CURRENT      0x12u   // signed
#define BQ27441_REG_MAX_CURRENT        0x14u   // signed
#define BQ27441_REG_AVG_POWER          0x18u   // signed
#define BQ27441_REG_SOC                0x1Cu
#define BQ27441_REG_INT_TEMPERATURE    0x1Eu
#define BQ27441_REG_SOH                0x20u   // low byte = %, high byte = status code

// Read-only convenience mirrors of two data-memory values (SLUUAC9A 5.1,
// 5.2), readable even SEALED with no block-data session. These are the
// ground truth for whether the BQ27441_Configure() data-flash commits
// actually landed -- the gauge silently discards a block whose checksum
// doesn't match, so a "successful" write sequence proves nothing by itself.
#define BQ27441_REG_OPCONFIG           0x3Au
#define BQ27441_REG_DESIGN_CAPACITY    0x3Cu

// Control() subcommands -- write [reg=0x00, lsb, msb] (3 bytes), then
// re-read the 2-byte CONTROL register for the ones that return data.
#define BQ27441_CTRL_CONTROL_STATUS    0x0000u
#define BQ27441_CTRL_DEVICE_TYPE       0x0001u
#define BQ27441_CTRL_FW_VERSION        0x0002u
#define BQ27441_CTRL_BAT_INSERT        0x000Cu
#define BQ27441_CTRL_BAT_REMOVE        0x000Du
#define BQ27441_CTRL_SET_CFGUPDATE     0x0013u
#define BQ27441_CTRL_SOFT_RESET        0x0042u
#define BQ27441_CTRL_EXIT_CFGUPDATE    0x0043u
#define BQ27441_CTRL_EXIT_RESIM        0x0044u

#define BQ27441_DEVICE_TYPE_EXPECTED   0x0421u

// Factory-default UNSEAL key pair (TI TRM SLUUAC9). Sent as two
// consecutive raw 16-bit writes to Control() -- not a normal subcommand
// round-trip, no readback. If the device is already unsealed, TI
// documents this as simply ignored (no matching subcommand), so it's
// safe to send unconditionally before every OpConfig session rather than
// tracking sealed/unsealed state.
//
// The bq27441-G1's sealed-to-unsealed key is two IDENTICAL words of
// 0x8000 (SLUUAC9: "the Sealed to Unsealed key has two identical words
// stored in ROM with a value of 0x8000 8000"). This was previously
// 0x0414/0x3672 -- the default key for the bq27500/bq27520/bq34z100
// family, NOT this part. A wrong key fails silently: both writes are
// still legal I2C writes and ACK, the gauge just stays SEALED, and the
// SET_CFGUPDATE that follows is then ignored, so
// BQ27441_EnterConfigUpdate() below spins out its poll limit and
// BQ27441_ConfigureOpConfig() returns false while every standard-command
// read (which works fine while sealed) keeps succeeding.
#define BQ27441_UNSEAL_KEY_1           0x8000u
#define BQ27441_UNSEAL_KEY_2           0x8000u

// CONTROL_STATUS bitfield -- the value returned by the Control()
// CONTROL_STATUS (0x0000) subcommand. Diagnostic only (printed by
// BQ27441_PrintStatus()); nothing in the driver branches on these.
// SS is the one that matters for BQ27441_ConfigureOpConfig(): the gauge
// ships SEALED and re-seals on every reset, and a sealed gauge silently
// ignores SET_CFGUPDATE, so SS reading 1 after boot means the unseal
// didn't take and no data-memory access is possible.
#define BQ27441_CTRLSTAT_SHUTDOWNEN    0x8000u
#define BQ27441_CTRLSTAT_WDRESET       0x4000u
#define BQ27441_CTRLSTAT_SS            0x2000u  // 1 = SEALED
#define BQ27441_CTRLSTAT_CALMODE       0x1000u
#define BQ27441_CTRLSTAT_CCA           0x0800u
#define BQ27441_CTRLSTAT_BCA           0x0400u
#define BQ27441_CTRLSTAT_QMAX_UP       0x0200u
#define BQ27441_CTRLSTAT_RES_UP        0x0100u
#define BQ27441_CTRLSTAT_INITCOMP      0x0080u  // 1 = initialization complete
#define BQ27441_CTRLSTAT_HIBERNATE     0x0040u
#define BQ27441_CTRLSTAT_SLEEP         0x0010u
#define BQ27441_CTRLSTAT_LDMD          0x0008u
#define BQ27441_CTRLSTAT_RUP_DIS       0x0004u
#define BQ27441_CTRLSTAT_VOK           0x0002u  // 1 = cell voltage OK for Qmax update

// Flags() bitfield.
#define BQ27441_FLAG_OT         0x8000u  // over temperature
#define BQ27441_FLAG_UT         0x4000u  // under temperature
#define BQ27441_FLAG_FC         0x0200u  // fully charged
#define BQ27441_FLAG_CHG        0x0100u  // fast charging allowed
#define BQ27441_FLAG_OCVTAKEN   0x0080u
#define BQ27441_FLAG_ITPOR      0x0020u
#define BQ27441_FLAG_CFGUPMODE  0x0010u  // currently in CFGUPDATE mode
#define BQ27441_FLAG_BAT_DET    0x0008u  // NOT reliable on this board -- see bq27441.h
#define BQ27441_FLAG_SOC1       0x0004u
#define BQ27441_FLAG_SOCF       0x0002u  // low-battery / final SOC threshold
#define BQ27441_FLAG_DSG        0x0001u  // discharging

// Extended/block-data ("data flash") access -- used only by the
// BQ27441_Configure() path. No existing driver in this codebase touches
// extended data; modeled on TI SLUUAC9's data-memory access procedure
// (BlockDataControl/Class/Offset/Data/Checksum), written fresh to match
// this codebase's style. The SparkFun BQ27441_Arduino_Library was used as
// a cross-reference early on but is NOT trustworthy against the TRM: its
// subclass-82 Taper Rate offset (21) is wrong for this part.
#define BQ27441_REG_BLOCKDATACONTROL   0x61u   // write 0x00 to enable block-data access
#define BQ27441_REG_BLOCKDATACLASS     0x3Eu
#define BQ27441_REG_BLOCKDATAOFFSET    0x3Fu
#define BQ27441_REG_BLOCKDATA          0x40u   // 32-byte window, 0x40-0x5F
#define BQ27441_BLOCKDATA_SIZE         32u
#define BQ27441_REG_BLOCKDATACHECKSUM  0x60u

// Confirmed against SLUUAC9's data-memory table and verified on hardware
// (the block read back OpConfig 0x25F8 / OpConfigB 0x0F, both exactly the
// documented defaults, and the gauge's own BlockDataChecksum agreed):
// subclass 64 "Registers" holds OpConfig at offset 0 as a big-endian 16-bit
// field, with OpConfigB at offset 2.
//
// Getting that read to line up needed a fix elsewhere: this part requires
// t(BUF) >= 66us between consecutive I2C packets addressed to it, and below
// that it merges packets and consumes the next one's register-address byte
// as write data -- which made every block read come back one byte late.
// i2c_master.c now enforces that per-device gap; see
// BQ27441_BUS_FREE_TIME_US in bq27441.h.
#define BQ27441_OPCONFIG_CLASS_ID          64u     // SLUUAC9 "Registers" subclass
#define BQ27441_OPCONFIG_BYTE_OFFSET       0u      // OpConfig sits at offset 0, big-endian
#define BQ27441_OPCONFIG_TEMPS_EXTERNAL    0x0001u // 1 = external thermistor on BIN
#define BQ27441_OPCONFIG_BATLOWEN          0x0004u // 1 = GPOUT mirrors SOC1 instead of SOC_INT
#define BQ27441_OPCONFIG_GPIOPOL           0x0800u // 0 = GPOUT active-low when SOC1 asserted, 1 = active-high

// Subclass 82 "State" holds the pack description Impedance Track gauges
// against. Out of the box these describe a 1200mAh cell, so an unconfigured
// gauge reports SOC as a fraction of 1200mAh no matter what is fitted --
// which is exactly what a Full Charge Capacity readback near 1213mAh means.
// All four fields are big-endian 16-bit, same as OpConfig.
#define BQ27441_STATE_CLASS_ID             82u
#define BQ27441_STATE_DESIGN_CAPACITY      10u   // mAh
#define BQ27441_STATE_DESIGN_ENERGY        12u   // mWh
#define BQ27441_STATE_TERMINATE_VOLTAGE    16u   // mV
// Offset 27 per SLUUAC9A Table 6-3 -- NOT 21, which the SparkFun Arduino
// library uses. On this part offset 21 is an unlisted/reserved byte and
// offsets 22-23 are T Rise (thermal model, default 20), so writing the
// taper word at 21 silently corrupts T Rise's MSB.
#define BQ27441_STATE_TAPER_RATE           27u   // DesignCapacity / (0.1 * taper current)

// Bounded retry cap for CFGUPDATE enter/exit polling -- this codebase has
// no delay/sleep primitive, so these are busy-poll loops capped at a
// fixed iteration count rather than a timed one (mirrors the
// `while(usbUartCheckIfBusy());` busy-poll idiom used elsewhere).
// *** VERIFY: TI specifies a data-flash commit delay around the
// checksum write / CFGUPDATE exit (on the order of ~200ms) -- this
// iteration cap is a stand-in for that and hasn't been tuned against a
// measured I2C1 bus speed. ***
#define BQ27441_CFGUPDATE_POLL_LIMIT   2000u

// *****************************************************************************
// Section: Register Encode/Decode Helpers
// *****************************************************************************

static bool BQ27441_ReadReg16LE(uint16_t address, uint8_t reg, uint16_t *value)
{
    uint8_t raw[2];

    if (!I2C_ReadRegister(address, reg, raw, sizeof(raw)))
    {
        return false;
    }

    *value = (uint16_t)raw[0] | ((uint16_t)raw[1] << 8);
    return true;
}

static bool BQ27441_ReadReg16SignedLE(uint16_t address, uint8_t reg, int16_t *value)
{
    uint16_t raw;

    if (!BQ27441_ReadReg16LE(address, reg, &raw))
    {
        return false;
    }

    *value = (int16_t)raw;
    return true;
}

// Writes a 16-bit word directly into Control() -- used both for Control()
// subcommands (see BQ27441_ControlRead()) and for sending the raw UNSEAL
// key words, which are the same wire operation with different intent.
static bool BQ27441_ControlWrite(uint16_t address, uint16_t word)
{
    uint8_t data[3] = { BQ27441_REG_CONTROL, (uint8_t)(word & 0xFFu), (uint8_t)(word >> 8) };

    return I2C_Write(address, data, sizeof(data));
}

static bool BQ27441_ControlRead(uint16_t address, uint16_t subcommand, uint16_t *value)
{
    if (!BQ27441_ControlWrite(address, subcommand))
    {
        return false;
    }

    return BQ27441_ReadReg16LE(address, BQ27441_REG_CONTROL, value);
}

static float BQ27441_DecodeVoltage(uint16_t raw)
{
    return (float)raw / 1000.0f;   // mV -> V
}

static float BQ27441_DecodeAverageCurrent(int16_t raw)
{
    return (float)raw / 1000.0f;   // mA -> A, signed
}

static float BQ27441_DecodeTemperature(uint16_t raw)
{
    return ((float)raw / 10.0f) - 273.15f;   // 0.1K -> C
}

static uint8_t BQ27441_DecodeStateOfCharge(uint16_t raw)
{
    return (uint8_t)(raw & 0xFFu);
}

static uint8_t BQ27441_DecodeStateOfHealth(uint16_t raw)
{
    return (uint8_t)(raw & 0xFFu);   // low byte = %, high byte = status code (unused here)
}

static float BQ27441_DecodeCapacity(uint16_t raw)
{
    return (float)raw;   // already mAh
}

static void BQ27441_DecodeFlags(uint16_t raw, BQ27441_FLAG_STATUS *status)
{
    status->overTemperature     = (raw & BQ27441_FLAG_OT) != 0;
    status->underTemperature    = (raw & BQ27441_FLAG_UT) != 0;
    status->fullyCharged         = (raw & BQ27441_FLAG_FC) != 0;
    status->fastChargingAllowed = (raw & BQ27441_FLAG_CHG) != 0;
    status->dischargeDetected   = (raw & BQ27441_FLAG_DSG) != 0;
    status->lowStateOfCharge    = (raw & BQ27441_FLAG_SOCF) != 0;
    status->configUpdateMode    = (raw & BQ27441_FLAG_CFGUPMODE) != 0;
}

// *****************************************************************************
// Section: Extended (Block-)Data Access -- used only by BQ27441_ConfigureOpConfig()
// *****************************************************************************

// When any step of a block-data session fails, these capture WHICH I2C
// transaction it was, the driver error it failed with, and the gauge's
// Flags() register AT THAT MOMENT, so the failure report in
// BQ27441_Configure() can say more than "a write failed somewhere" -- a
// NACK and a timeout point at very different problems, and CFGUPMODE in
// the captured flags settles whether the gauge was actually still in
// CONFIG UPDATE mode when it refused (it can't be read after the fact:
// the sequence always exits CFGUPDATE before returning). Only the FIRST
// failure is kept, matching BQ27441_ConfigureVerbose()'s
// first-failing-step reporting; ConfigureVerbose() clears it on entry.
static const char *bq27441BlockFailStep;
static I2C_ERROR   bq27441BlockFailError;
static uint16_t    bq27441BlockFailFlags;
static bool        bq27441BlockFailFlagsValid;

// On a window-readback mismatch, the window itself and the first offending
// edit are kept for the failure report. The raw hex distinguishes the two
// remaining explanations at a glance: a clean default-valued block means
// the gauge ignored the writes; the same bytes shifted by one (the classic
// misframe on this part) means the READBACK is lying, not the writes.
static uint8_t  bq27441FailWindow[BQ27441_BLOCKDATA_SIZE];
static bool     bq27441FailWindowValid;
static uint8_t  bq27441FailEditOffset;
static uint16_t bq27441FailEditExpected;
static uint16_t bq27441FailEditActual;

// Records the first block-data sub-step failure. Always returns false so
// call sites can `return BQ27441_BlockFail(...)`.
static bool BQ27441_BlockFail(uint16_t address, const char *step)
{
    I2C_ERROR error = I2C_ErrorGet();

    if (bq27441BlockFailStep == NULL)
    {
        bq27441BlockFailStep  = step;
        bq27441BlockFailError = error;
        // Flags() is a standard command and readable in any state, even the
        // ones that refuse block-data writes -- but if the bus itself is
        // down this read fails too, hence the valid flag.
        bq27441BlockFailFlagsValid =
            BQ27441_ReadReg16LE(address, BQ27441_REG_FLAGS, &bq27441BlockFailFlags);
    }

    return false;
}

static const char* BQ27441_I2CErrorName(I2C_ERROR error)
{
    switch (error)
    {
        case I2C_ERROR_NONE:              return "none";
        case I2C_ERROR_NACK:              return "NACK";
        case I2C_ERROR_BUS_COLLISION:     return "bus collision";
        case I2C_ERROR_TIMEOUT:           return "timeout";
        case I2C_ERROR_INVALID_PARAMETER: return "invalid parameter";
        case I2C_ERROR_QUEUE_FULL:        return "queue full";
        default:                          return "unknown";
    }
}

// Latches the 32-byte window for `classId`/`blockOffset` into the
// BlockData() command space (TRM 6.1.1: enable block access, select class,
// select block).
static bool BQ27441_BlockDataSelect(uint16_t address, uint8_t classId, uint8_t blockOffset)
{
    uint8_t zero = 0x00u;

    if (!I2C_WriteRegister(address, BQ27441_REG_BLOCKDATACONTROL, &zero, 1))
    {
        return BQ27441_BlockFail(address, "BlockDataControl enable (0x61)");
    }

    if (!I2C_WriteRegister(address, BQ27441_REG_BLOCKDATACLASS, &classId, 1))
    {
        return BQ27441_BlockFail(address, "BlockDataClass select (0x3E)");
    }

    if (!I2C_WriteRegister(address, BQ27441_REG_BLOCKDATAOFFSET, &blockOffset, 1))
    {
        return BQ27441_BlockFail(address, "BlockDataOffset select (0x3F)");
    }

    return true;
}

static bool BQ27441_ReadExtendedBlock(uint16_t address, uint8_t classId, uint8_t blockOffset,
                                       uint8_t block[BQ27441_BLOCKDATA_SIZE])
{
    if (!BQ27441_BlockDataSelect(address, classId, blockOffset))
    {
        return false;
    }

    if (!I2C_ReadRegister(address, BQ27441_REG_BLOCKDATA, block, BQ27441_BLOCKDATA_SIZE))
    {
        return BQ27441_BlockFail(address, "BlockData 32-byte read (0x40)");
    }

    return true;
}

// Data-flash fields are big-endian (MSB first) -- opposite of the
// little-endian standard commands.
static uint16_t BQ27441_GetBlockWord(const uint8_t block[BQ27441_BLOCKDATA_SIZE], uint8_t offset)
{
    return ((uint16_t)block[offset] << 8) | block[offset + 1];
}

// One big-endian 16-bit field to change within a 32-byte block window.
typedef struct
{
    uint8_t  offset;   // byte offset of the field within the block
    uint16_t value;
} BQ27441_BLOCK_WORD_EDIT;

// Writes `edits` into the block window with SINGLE-BYTE transfers (the
// packet shape TI's own data-memory example, TRM section 3.1, uses), then
// commits with a checksum computed from a full read-back of the window
// rather than TI's old-checksum/data-replacement arithmetic. The gauge
// NACKs a checksum write whose value doesn't match its own sum of the
// window (confirmed by TI for this gauge family on E2E), so replacement
// math is only as good as every 1-byte read it's built on -- and 1-byte
// reads of the block window are exactly the access pattern this part has
// a history of misframing on this board. Reading the whole window back in
// one 32-byte transfer (the proven-aligned access pattern here) does two
// jobs at once:
//   1. verifies each written byte actually landed at its offset -- if the
//      gauge ignored the writes (e.g. not genuinely in CFGUPDATE) or an
//      merged packet displaced one, this reports it by name instead of
//      NACKing mysteriously at the checksum;
//   2. makes the checksum a sum over what the gauge itself returned, so
//      if the gauge still NACKs it, its reads and its internal window
//      genuinely disagree -- a bus-integrity fact worth knowing.
//
// A full 32-byte block WRITE is deliberately never used: besides needing
// a 33-byte packet no reference implementation uses, it would drag live
// gauging state back through the bus (for subclass 82 that includes Qmax
// and Update Status, whose bit 7 makes the gauge re-SEAL itself on every
// CFGUPDATE exit).
//
// The gauge only transfers the window to Data Memory once the correct
// checksum lands at BlockDataChecksum(). After that this re-selects the
// block (re-latching the window from Data Memory) and reads the checksum
// back: the new value proves the commit landed.
//
// Assumes the caller has already unsealed the gauge and entered CFGUPDATE
// mode.
static bool BQ27441_WriteBlockWords(uint16_t address, uint8_t classId, uint8_t blockOffset,
                                    const BQ27441_BLOCK_WORD_EDIT *edits, uint8_t editCount)
{
    uint8_t window[BQ27441_BLOCKDATA_SIZE];
    uint16_t sum;
    uint8_t newChecksum;
    uint8_t readback;
    uint8_t i;

    if (!BQ27441_BlockDataSelect(address, classId, blockOffset))
    {
        return false;
    }

    for (i = 0; i < editCount; i++)
    {
        // Big-endian: MSB at the field's offset -- see BQ27441_GetBlockWord()
        uint8_t bytes[2] = { (uint8_t)(edits[i].value >> 8), (uint8_t)(edits[i].value & 0xFFu) };
        uint8_t j;

        if (edits[i].offset > (BQ27441_BLOCKDATA_SIZE - 2u))
        {
            return BQ27441_BlockFail(address, "edit offset outside 32-byte window");
        }

        for (j = 0; j < 2u; j++)
        {
            uint8_t reg = (uint8_t)(BQ27441_REG_BLOCKDATA + edits[i].offset + j);

            if (!I2C_WriteRegister(address, reg, &bytes[j], 1))
            {
                return BQ27441_BlockFail(address, "BlockData single-byte write (0x40+offset)");
            }
        }
    }

    if (!I2C_ReadRegister(address, BQ27441_REG_BLOCKDATA, window, BQ27441_BLOCKDATA_SIZE))
    {
        return BQ27441_BlockFail(address, "BlockData window readback (0x40, 32 bytes)");
    }

    for (i = 0; i < editCount; i++)
    {
        uint16_t actual = BQ27441_GetBlockWord(window, edits[i].offset);

        if (actual != edits[i].value)
        {
            uint8_t b;

            for (b = 0; b < BQ27441_BLOCKDATA_SIZE; b++)
            {
                bq27441FailWindow[b] = window[b];
            }
            bq27441FailWindowValid  = true;
            bq27441FailEditOffset   = edits[i].offset;
            bq27441FailEditExpected = edits[i].value;
            bq27441FailEditActual   = actual;

            return BQ27441_BlockFail(address, "window readback mismatch (byte writes not applied)");
        }
    }

    sum = 0;
    for (i = 0; i < BQ27441_BLOCKDATA_SIZE; i++)
    {
        sum += window[i];
    }
    newChecksum = (uint8_t)(255u - (sum & 0xFFu));

    if (!I2C_WriteRegister(address, BQ27441_REG_BLOCKDATACHECKSUM, &newChecksum, 1))
    {
        return BQ27441_BlockFail(address, "BlockDataChecksum write (0x60)");
    }

    /* Commit verification -- see the function comment */
    if (!BQ27441_BlockDataSelect(address, classId, blockOffset))
    {
        return false;
    }

    if (!I2C_ReadRegister(address, BQ27441_REG_BLOCKDATACHECKSUM, &readback, 1))
    {
        return BQ27441_BlockFail(address, "BlockDataChecksum verify read (0x60)");
    }

    if (readback != newChecksum)
    {
        return BQ27441_BlockFail(address, "commit rejected (checksum readback mismatch)");
    }

    return true;
}

static bool BQ27441_EnterConfigUpdate(uint16_t address)
{
    uint32_t attempt;
    uint16_t flags;

    if (!BQ27441_ControlWrite(address, BQ27441_CTRL_SET_CFGUPDATE))
    {
        return false;
    }

    for (attempt = 0; attempt < BQ27441_CFGUPDATE_POLL_LIMIT; attempt++)
    {
        if (BQ27441_ReadReg16LE(address, BQ27441_REG_FLAGS, &flags) && ((flags & BQ27441_FLAG_CFGUPMODE) != 0))
        {
            return true;
        }
    }

    return false;
}

static bool BQ27441_ExitConfigUpdate(uint16_t address)
{
    uint32_t attempt;
    uint16_t flags;

    // EXIT_RESIM (0x0044) is TRM-sanctioned (SLUUAC9A Table 4-2): exits
    // CONFIG UPDATE without an OCV measurement and resimulates with the
    // updated configuration to refresh StateOfCharge(). TI's section 3.1
    // example uses SOFT_RESET (0x0042) instead, which also exits;
    // EXIT_RESIM is kept because it recomputes SOC from the new pack
    // description without disturbing the OCV/Qmax state.
    if (!BQ27441_ControlWrite(address, BQ27441_CTRL_EXIT_RESIM))
    {
        return false;
    }

    for (attempt = 0; attempt < BQ27441_CFGUPDATE_POLL_LIMIT; attempt++)
    {
        if (BQ27441_ReadReg16LE(address, BQ27441_REG_FLAGS, &flags) && ((flags & BQ27441_FLAG_CFGUPMODE) == 0))
        {
            return true;
        }
    }

    return false;
}

// *****************************************************************************
// Section: Interface Routines
// *****************************************************************************

bool BQ27441_Verify(uint16_t address)
{
    uint16_t deviceType;

    if (!BQ27441_ControlRead(address, BQ27441_CTRL_DEVICE_TYPE, &deviceType))
    {
        return false;
    }

    return (deviceType == BQ27441_DEVICE_TYPE_EXPECTED);
}

// Applies the OpConfig bits this board needs. Assumes the caller has already
// unsealed the gauge and entered CFGUPDATE mode.
static BQ27441_CONFIG_STEP BQ27441_ApplyOpConfig(uint16_t address)
{
    uint8_t block[BQ27441_BLOCKDATA_SIZE];
    BQ27441_BLOCK_WORD_EDIT edit;
    uint16_t opConfig;
    uint16_t desired;

    if (!BQ27441_ReadExtendedBlock(address, BQ27441_OPCONFIG_CLASS_ID, 0, block))
    {
        return BQ27441_CONFIG_STEP_OPCONFIG_READ;
    }

    opConfig = BQ27441_GetBlockWord(block, BQ27441_OPCONFIG_BYTE_OFFSET);

    desired  = opConfig;

    // TEMPS is deliberately CLEARED -- internal die sensor, not the external
    // thermistor on BIN. Setting it was tried on hardware and the gauge
    // reported 3029.25 C (raw 0x8100, a saturated 0.1K reading), which then
    // latched the OT flag and stopped Impedance Track from gauging. Cause is
    // the rev A divider: TH1401 is a 10k NTC against R1402, a 1.8M pull-up,
    // so BIN sits essentially at a rail and the ratiometric conversion has
    // nothing to work with. Do not re-enable this without changing R1402 to
    // something in the same decade as the thermistor.
    desired &= (uint16_t)~BQ27441_OPCONFIG_TEMPS_EXTERNAL;
    desired |= BQ27441_OPCONFIG_BATLOWEN;         // GPOUT mirrors SOC1 (-> BATT_LOWBATT_PIN)
    desired &= (uint16_t)~BQ27441_OPCONFIG_GPIOPOL; // GPIOPOL=0: GPOUT active-low when SOC1 asserted

    if (desired == opConfig)
    {
        return BQ27441_CONFIG_STEP_NONE;   // already configured -- don't spend a data-flash write
    }

    edit.offset = BQ27441_OPCONFIG_BYTE_OFFSET;
    edit.value  = desired;

    return BQ27441_WriteBlockWords(address, BQ27441_OPCONFIG_CLASS_ID, 0, &edit, 1)
               ? BQ27441_CONFIG_STEP_NONE : BQ27441_CONFIG_STEP_OPCONFIG_WRITE;
}

// Applies the pack description in `profile`. Assumes the caller has already
// unsealed the gauge and entered CFGUPDATE mode.
static BQ27441_CONFIG_STEP BQ27441_ApplyBatteryProfile(uint16_t address,
                                                       const BQ27441_BATTERY_PROFILE *profile)
{
    uint8_t block[BQ27441_BLOCKDATA_SIZE];
    BQ27441_BLOCK_WORD_EDIT edits[4];
    uint8_t editCount = 0;

    if (!BQ27441_ReadExtendedBlock(address, BQ27441_STATE_CLASS_ID, 0, block))
    {
        return BQ27441_CONFIG_STEP_PROFILE_READ;
    }

    // Design Capacity and Design Energy must agree with each other or the
    // gauge's power/energy predictions drift apart from its charge ones, so
    // they are always written as a pair when either differs.
    if ((BQ27441_GetBlockWord(block, BQ27441_STATE_DESIGN_CAPACITY) != profile->designCapacity_mAh) ||
        (BQ27441_GetBlockWord(block, BQ27441_STATE_DESIGN_ENERGY)   != profile->designEnergy_mWh))
    {
        edits[editCount].offset = BQ27441_STATE_DESIGN_CAPACITY;
        edits[editCount].value  = profile->designCapacity_mAh;
        editCount++;
        edits[editCount].offset = BQ27441_STATE_DESIGN_ENERGY;
        edits[editCount].value  = profile->designEnergy_mWh;
        editCount++;
    }

    if (BQ27441_GetBlockWord(block, BQ27441_STATE_TERMINATE_VOLTAGE) != profile->terminateVoltage_mV)
    {
        edits[editCount].offset = BQ27441_STATE_TERMINATE_VOLTAGE;
        edits[editCount].value  = profile->terminateVoltage_mV;
        editCount++;
    }

    if (BQ27441_GetBlockWord(block, BQ27441_STATE_TAPER_RATE) != profile->taperRate)
    {
        edits[editCount].offset = BQ27441_STATE_TAPER_RATE;
        edits[editCount].value  = profile->taperRate;
        editCount++;
    }

    if (editCount == 0)
    {
        return BQ27441_CONFIG_STEP_NONE;   // already programmed -- data flash has finite endurance
    }

    return BQ27441_WriteBlockWords(address, BQ27441_STATE_CLASS_ID, 0, edits, editCount)
               ? BQ27441_CONFIG_STEP_NONE : BQ27441_CONFIG_STEP_PROFILE_WRITE;
}

const char* BQ27441_ConfigStepName(BQ27441_CONFIG_STEP step)
{
    switch (step)
    {
        case BQ27441_CONFIG_STEP_NONE:            return "no failure";
        case BQ27441_CONFIG_STEP_UNSEAL:          return "UNSEAL (Control key write)";
        case BQ27441_CONFIG_STEP_ENTER_CFGUPDATE: return "enter CFGUPDATE (SET_CFGUPDATE / CFGUPMODE poll)";
        case BQ27441_CONFIG_STEP_OPCONFIG_READ:   return "read OpConfig block (subclass 64)";
        case BQ27441_CONFIG_STEP_OPCONFIG_WRITE:  return "write OpConfig block (subclass 64)";
        case BQ27441_CONFIG_STEP_PROFILE_READ:    return "read pack description block (subclass 82)";
        case BQ27441_CONFIG_STEP_PROFILE_WRITE:   return "write pack description block (subclass 82)";
        case BQ27441_CONFIG_STEP_EXIT_CFGUPDATE:  return "exit CFGUPDATE (EXIT_RESIM / CFGUPMODE poll)";
        default:                                  return "unknown";
    }
}

// Body of BQ27441_ConfigureVerbose() -- split out so the wrapper can
// bracket every exit path with the config-session bus pacing below.
static BQ27441_CONFIG_STEP BQ27441_ConfigureSession(uint16_t address, const BQ27441_BATTERY_PROFILE *profile)
{
    BQ27441_CONFIG_STEP failedStep = BQ27441_CONFIG_STEP_NONE;
    BQ27441_CONFIG_STEP step;

    if (profile == NULL)
    {
        return BQ27441_CONFIG_STEP_UNSEAL;
    }

    // Harmless if already unsealed -- see the key defines' comment above.
    if (!BQ27441_ControlWrite(address, BQ27441_UNSEAL_KEY_1) ||
        !BQ27441_ControlWrite(address, BQ27441_UNSEAL_KEY_2))
    {
        return BQ27441_CONFIG_STEP_UNSEAL;
    }

    if (!BQ27441_EnterConfigUpdate(address))
    {
        return BQ27441_CONFIG_STEP_ENTER_CFGUPDATE;
    }

    // Both subclasses are updated inside one CFGUPDATE session: entering and
    // exiting is the expensive part (each exit triggers a resimulation), and
    // a half-applied configuration is worse than none. On failure the FIRST
    // failing step is what gets reported, since later ones are usually
    // consequences of it -- but the sequence still runs to completion so the
    // gauge is never left sitting in CFGUPDATE mode.
    step = BQ27441_ApplyOpConfig(address);
    if ((step != BQ27441_CONFIG_STEP_NONE) && (failedStep == BQ27441_CONFIG_STEP_NONE))
    {
        failedStep = step;
    }

    step = BQ27441_ApplyBatteryProfile(address, profile);
    if ((step != BQ27441_CONFIG_STEP_NONE) && (failedStep == BQ27441_CONFIG_STEP_NONE))
    {
        failedStep = step;
    }

    if (!BQ27441_ExitConfigUpdate(address) && (failedStep == BQ27441_CONFIG_STEP_NONE))
    {
        failedStep = BQ27441_CONFIG_STEP_EXIT_CFGUPDATE;
    }

    return failedStep;
}

// Inter-packet gap used for the duration of the configuration session,
// replacing the normal BQ27441_BUS_FREE_TIME_US (70us). REQUIRED, root
// cause confirmed on hardware 2026-07-22: the gauge's I2C hardware ACKs
// single-byte writes into the block-data window immediately, but its
// internal firmware applies them asynchronously -- at the datasheet's own
// 66us t(BUF) pacing, writes into the subclass-82 window were dropped
// (ACKed, then read back unmodified moments later, CFGUPMODE still set),
// which also made every checksum the host computed "wrong" and NACKed at
// 0x60. At 2ms/packet the same sequence commits first try. The TRM/
// datasheet document no such service latency; 2ms is empirical with
// ~30x margin over the documented t(BUF) and costs only tens of ms once
// per boot, so it is deliberately not tuned tighter. Applies only inside
// BQ27441_ConfigureVerbose(); telemetry reads stay at 70us.
#define BQ27441_CONFIG_SESSION_GAP_US   2000u

BQ27441_CONFIG_STEP BQ27441_ConfigureVerbose(uint16_t address, const BQ27441_BATTERY_PROFILE *profile)
{
    BQ27441_CONFIG_STEP failedStep;

    bq27441BlockFailStep       = NULL;
    bq27441BlockFailError      = I2C_ERROR_NONE;
    bq27441BlockFailFlags      = 0;
    bq27441BlockFailFlagsValid = false;
    bq27441FailWindowValid     = false;

    I2C_SetDeviceBusFreeTime(address, BQ27441_CONFIG_SESSION_GAP_US);

    failedStep = BQ27441_ConfigureSession(address, profile);

    I2C_SetDeviceBusFreeTime(address, BQ27441_BUS_FREE_TIME_US);

    return failedStep;
}

bool BQ27441_Configure(uint16_t address, const BQ27441_BATTERY_PROFILE *profile)
{
    BQ27441_CONFIG_STEP failedStep = BQ27441_ConfigureVerbose(address, profile);

    if (failedStep != BQ27441_CONFIG_STEP_NONE)
    {
        // Printed only on failure. Without it the configuration error flag
        // says something went wrong but not where, and this sequence has
        // seven distinct ways to fail. The second line narrows a block-data
        // failure to the exact I2C transaction and driver error.
        terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("    BQ27441 configuration failed at: %s\n\r", BQ27441_ConfigStepName(failedStep));
        if (bq27441BlockFailStep != NULL)
        {
            printf("    Failing block-data transaction: %s, I2C error: %s\n\r",
                   bq27441BlockFailStep, BQ27441_I2CErrorName(bq27441BlockFailError));
            if (bq27441BlockFailFlagsValid)
            {
                // CFGUPMODE here is the decisive bit: the gauge refuses
                // data-memory commits outside CONFIG UPDATE mode, and this
                // is the only record of whether it was still in it at the
                // moment of failure (the sequence exits before returning).
                printf("    Flags() at failure: 0x%04X (CFGUPMODE=%u)\n\r",
                       bq27441BlockFailFlags,
                       (bq27441BlockFailFlags & BQ27441_FLAG_CFGUPMODE) ? 1u : 0u);
            }
            if (bq27441FailWindowValid)
            {
                uint8_t b;

                printf("    Edit at offset %u: wrote 0x%04X, read back 0x%04X\n\r",
                       (unsigned)bq27441FailEditOffset,
                       bq27441FailEditExpected, bq27441FailEditActual);
                printf("    Window readback:");
                for (b = 0; b < BQ27441_BLOCKDATA_SIZE; b++)
                {
                    printf("%s%02X", ((b % 16u) == 0u) ? "\n\r        " : " ",
                           bq27441FailWindow[b]);
                }
                printf("\n\r");
            }
        }
        terminalTextAttributesReset();
    }

    return (failedStep == BQ27441_CONFIG_STEP_NONE);
}

bool BQ27441_ReadVoltage(uint16_t address, float *volts)
{
    uint16_t raw;

    if (!BQ27441_ReadReg16LE(address, BQ27441_REG_VOLTAGE, &raw))
    {
        return false;
    }

    *volts = BQ27441_DecodeVoltage(raw);
    return true;
}

bool BQ27441_ReadAverageCurrent(uint16_t address, float *amps)
{
    int16_t raw;

    if (!BQ27441_ReadReg16SignedLE(address, BQ27441_REG_AVG_CURRENT, &raw))
    {
        return false;
    }

    *amps = BQ27441_DecodeAverageCurrent(raw);
    return true;
}

bool BQ27441_ReadTemperature(uint16_t address, float *celsius)
{
    uint16_t raw;

    if (!BQ27441_ReadReg16LE(address, BQ27441_REG_TEMPERATURE, &raw))
    {
        return false;
    }

    *celsius = BQ27441_DecodeTemperature(raw);
    return true;
}

bool BQ27441_ReadStateOfCharge(uint16_t address, uint8_t *percent)
{
    uint16_t raw;

    if (!BQ27441_ReadReg16LE(address, BQ27441_REG_SOC, &raw))
    {
        return false;
    }

    *percent = BQ27441_DecodeStateOfCharge(raw);
    return true;
}

bool BQ27441_ReadStateOfHealth(uint16_t address, uint8_t *percent)
{
    uint16_t raw;

    if (!BQ27441_ReadReg16LE(address, BQ27441_REG_SOH, &raw))
    {
        return false;
    }

    *percent = BQ27441_DecodeStateOfHealth(raw);
    return true;
}

bool BQ27441_ReadRemainingCapacity(uint16_t address, float *milliamphours)
{
    uint16_t raw;

    if (!BQ27441_ReadReg16LE(address, BQ27441_REG_REM_CAPACITY, &raw))
    {
        return false;
    }

    *milliamphours = BQ27441_DecodeCapacity(raw);
    return true;
}

bool BQ27441_ReadFullChargeCapacity(uint16_t address, float *milliamphours)
{
    uint16_t raw;

    if (!BQ27441_ReadReg16LE(address, BQ27441_REG_FULL_CAPACITY, &raw))
    {
        return false;
    }

    *milliamphours = BQ27441_DecodeCapacity(raw);
    return true;
}

bool BQ27441_ReadFlags(uint16_t address, BQ27441_FLAG_STATUS *status)
{
    uint16_t raw;

    if (!BQ27441_ReadReg16LE(address, BQ27441_REG_FLAGS, &raw))
    {
        return false;
    }

    BQ27441_DecodeFlags(raw, status);
    return true;
}

bool BQ27441_QueueReadVoltage(uint16_t address, uint8_t raw[2],
                               I2C_TRANSFER_CALLBACK callback, uintptr_t context)
{
    return I2C_QueueReadRegister(address, BQ27441_REG_VOLTAGE, raw, 2, callback, context);
}

bool BQ27441_QueueReadAverageCurrent(uint16_t address, uint8_t raw[2],
                                     I2C_TRANSFER_CALLBACK callback, uintptr_t context)
{
    return I2C_QueueReadRegister(address, BQ27441_REG_AVG_CURRENT, raw, 2, callback, context);
}

bool BQ27441_QueueReadTemperature(uint16_t address, uint8_t raw[2],
                                  I2C_TRANSFER_CALLBACK callback, uintptr_t context)
{
    return I2C_QueueReadRegister(address, BQ27441_REG_TEMPERATURE, raw, 2, callback, context);
}

bool BQ27441_QueueReadStateOfCharge(uint16_t address, uint8_t raw[2],
                                    I2C_TRANSFER_CALLBACK callback, uintptr_t context)
{
    return I2C_QueueReadRegister(address, BQ27441_REG_SOC, raw, 2, callback, context);
}

bool BQ27441_QueueReadStateOfHealth(uint16_t address, uint8_t raw[2],
                                    I2C_TRANSFER_CALLBACK callback, uintptr_t context)
{
    return I2C_QueueReadRegister(address, BQ27441_REG_SOH, raw, 2, callback, context);
}

bool BQ27441_QueueReadRemainingCapacity(uint16_t address, uint8_t raw[2],
                                        I2C_TRANSFER_CALLBACK callback, uintptr_t context)
{
    return I2C_QueueReadRegister(address, BQ27441_REG_REM_CAPACITY, raw, 2, callback, context);
}

bool BQ27441_QueueReadFullChargeCapacity(uint16_t address, uint8_t raw[2],
                                         I2C_TRANSFER_CALLBACK callback, uintptr_t context)
{
    return I2C_QueueReadRegister(address, BQ27441_REG_FULL_CAPACITY, raw, 2, callback, context);
}

bool BQ27441_QueueReadFlags(uint16_t address, uint8_t raw[2],
                            I2C_TRANSFER_CALLBACK callback, uintptr_t context)
{
    return I2C_QueueReadRegister(address, BQ27441_REG_FLAGS, raw, 2, callback, context);
}

float BQ27441_DecodeVoltageRaw(const uint8_t raw[2])
{
    return BQ27441_DecodeVoltage((uint16_t)raw[0] | ((uint16_t)raw[1] << 8));
}

float BQ27441_DecodeAverageCurrentRaw(const uint8_t raw[2])
{
    return BQ27441_DecodeAverageCurrent((int16_t)((uint16_t)raw[0] | ((uint16_t)raw[1] << 8)));
}

float BQ27441_DecodeTemperatureRaw(const uint8_t raw[2])
{
    return BQ27441_DecodeTemperature((uint16_t)raw[0] | ((uint16_t)raw[1] << 8));
}

uint8_t BQ27441_DecodeStateOfChargeRaw(const uint8_t raw[2])
{
    return BQ27441_DecodeStateOfCharge((uint16_t)raw[0] | ((uint16_t)raw[1] << 8));
}

uint8_t BQ27441_DecodeStateOfHealthRaw(const uint8_t raw[2])
{
    return BQ27441_DecodeStateOfHealth((uint16_t)raw[0] | ((uint16_t)raw[1] << 8));
}

float BQ27441_DecodeRemainingCapacityRaw(const uint8_t raw[2])
{
    return BQ27441_DecodeCapacity((uint16_t)raw[0] | ((uint16_t)raw[1] << 8));
}

float BQ27441_DecodeFullChargeCapacityRaw(const uint8_t raw[2])
{
    return BQ27441_DecodeCapacity((uint16_t)raw[0] | ((uint16_t)raw[1] << 8));
}

void BQ27441_DecodeFlagsRaw(const uint8_t raw[2], BQ27441_FLAG_STATUS *status)
{
    BQ27441_DecodeFlags((uint16_t)raw[0] | ((uint16_t)raw[1] << 8), status);
}

void BQ27441_PrintStatus(uint16_t address)
{
    uint16_t deviceType;
    uint16_t controlStatus;
    uint16_t rawFlags;
    uint16_t rawSoc;
    uint16_t rawSoh;
    uint16_t opConfig;
    uint16_t designCapacity;
    int16_t rawCurrent;
    float volts;
    float celsius;
    float remCap;
    float fullCap;
    BQ27441_FLAG_STATUS flags;
    bool identified;

    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, BOLD_FONT);
    printf("    --- BQ27441 (address 0x%02X) ---\n\r", address);

    if (!BQ27441_ControlRead(address, BQ27441_CTRL_DEVICE_TYPE, &deviceType))
    {
        terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("    No response from device (I2C error: %d)\n\r", (int)I2C_ErrorGet());
        terminalTextAttributesReset();
        return;
    }

    identified = (deviceType == BQ27441_DEVICE_TYPE_EXPECTED);
    terminalTextAttributes(identified ? GREEN_COLOR : RED_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("    Device Type: 0x%04X (%s)\n\r", deviceType, identified ? "recognized" : "unrecognized");

    // CONTROL_STATUS, mainly for its SS (sealed) bit: the gauge ships
    // sealed and re-seals on every reset, and while sealed it silently
    // ignores the SET_CFGUPDATE that BQ27441_ConfigureOpConfig() needs. So
    // SS here is the direct readout of whether that boot-time unseal took.
    if (BQ27441_ControlRead(address, BQ27441_CTRL_CONTROL_STATUS, &controlStatus))
    {
        bool sealed = (controlStatus & BQ27441_CTRLSTAT_SS) != 0;

        terminalTextAttributes(sealed ? YELLOW_COLOR : GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("    Control Status: 0x%04X (%s%s%s%s%s%s%s%s)\n\r", controlStatus,
               sealed ? "SEALED " : "UNSEALED ",
               (controlStatus & BQ27441_CTRLSTAT_INITCOMP)   ? "INITCOMP " : "",
               (controlStatus & BQ27441_CTRLSTAT_VOK)        ? "VOK " : "",
               (controlStatus & BQ27441_CTRLSTAT_SLEEP)      ? "SLEEP " : "",
               (controlStatus & BQ27441_CTRLSTAT_HIBERNATE)  ? "HIBERNATE " : "",
               (controlStatus & BQ27441_CTRLSTAT_CALMODE)    ? "CALMODE " : "",
               (controlStatus & BQ27441_CTRLSTAT_WDRESET)    ? "WDRESET " : "",
               (controlStatus & BQ27441_CTRLSTAT_SHUTDOWNEN) ? "SHUTDOWNEN " : "");
    }

    if (BQ27441_ReadReg16LE(address, BQ27441_REG_FLAGS, &rawFlags))
    {
        BQ27441_DecodeFlags(rawFlags, &flags);
        terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("    Flags: 0x%04X (%s%s%s%s%s%s%s)\n\r", rawFlags,
               flags.overTemperature ? "OT " : "",
               flags.underTemperature ? "UT " : "",
               flags.fullyCharged ? "FC " : "",
               flags.fastChargingAllowed ? "CHG " : "",
               flags.dischargeDetected ? "DSG " : "",
               flags.lowStateOfCharge ? "SOCF " : "",
               flags.configUpdateMode ? "CFGUPMODE " : "");
    }

    // Ground truth for both BQ27441_Configure() data-flash writes -- these
    // read the gauge's Data Memory directly (read-only mirrors, no block
    // session), so they show what actually committed, not what was sent.
    if (BQ27441_ReadReg16LE(address, BQ27441_REG_OPCONFIG, &opConfig))
    {
        printf("    OpConfig: 0x%04X (TEMPS=%u BATLOWEN=%u GPIOPOL=%u)\n\r", opConfig,
               (opConfig & BQ27441_OPCONFIG_TEMPS_EXTERNAL) ? 1u : 0u,
               (opConfig & BQ27441_OPCONFIG_BATLOWEN)       ? 1u : 0u,
               (opConfig & BQ27441_OPCONFIG_GPIOPOL)        ? 1u : 0u);
    }

    if (BQ27441_ReadReg16LE(address, BQ27441_REG_DESIGN_CAPACITY, &designCapacity))
    {
        printf("    Design Capacity (data memory): %u mAh\n\r", (unsigned)designCapacity);
    }

    if (BQ27441_ReadVoltage(address, &volts))
    {
        printf("    Voltage: %.3f V\n\r", volts);
    }

    if (BQ27441_ReadReg16SignedLE(address, BQ27441_REG_AVG_CURRENT, &rawCurrent))
    {
        printf("    Average Current: %.3f A\n\r", BQ27441_DecodeAverageCurrent(rawCurrent));
    }

    if (BQ27441_ReadTemperature(address, &celsius))
    {
        printf("    Temperature: %.2f C\n\r", celsius);
    }

    if (BQ27441_ReadReg16LE(address, BQ27441_REG_SOC, &rawSoc))
    {
        printf("    State of Charge: %u%%\n\r", (unsigned)BQ27441_DecodeStateOfCharge(rawSoc));
    }

    if (BQ27441_ReadReg16LE(address, BQ27441_REG_SOH, &rawSoh))
    {
        printf("    State of Health: %u%%\n\r", (unsigned)BQ27441_DecodeStateOfHealth(rawSoh));
    }

    if (BQ27441_ReadRemainingCapacity(address, &remCap))
    {
        printf("    Remaining Capacity: %.1f mAh\n\r", remCap);
    }

    if (BQ27441_ReadFullChargeCapacity(address, &fullCap))
    {
        printf("    Full Charge Capacity: %.1f mAh\n\r", fullCap);
    }

    terminalTextAttributesReset();
}
