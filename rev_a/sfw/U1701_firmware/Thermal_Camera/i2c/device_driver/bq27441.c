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

// Extended/block-data ("data flash") access -- used only by
// BQ27441_ConfigureOpConfig(). No existing driver in this codebase
// touches extended data; modeled on TI SLUUAC9's data-memory access
// procedure (BlockDataControl/Class/Offset/Data/Checksum) and the
// well-known open-source SparkFun BQ27441_Arduino_Library's equivalent
// helpers, written fresh to match this codebase's style.
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
#define BQ27441_STATE_TAPER_RATE           21u   // DesignCapacity / (0.1 * taper current)

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

static bool BQ27441_BlockDataControl(uint16_t address)
{
    uint8_t zero = 0x00u;

    return I2C_WriteRegister(address, BQ27441_REG_BLOCKDATACONTROL, &zero, 1);
}

static bool BQ27441_BlockDataClass(uint16_t address, uint8_t classId)
{
    return I2C_WriteRegister(address, BQ27441_REG_BLOCKDATACLASS, &classId, 1);
}

static bool BQ27441_BlockDataOffset(uint16_t address, uint8_t blockOffset)
{
    return I2C_WriteRegister(address, BQ27441_REG_BLOCKDATAOFFSET, &blockOffset, 1);
}

static bool BQ27441_ReadExtendedBlock(uint16_t address, uint8_t classId, uint8_t blockOffset,
                                       uint8_t block[BQ27441_BLOCKDATA_SIZE])
{
    if (!BQ27441_BlockDataControl(address)) return false;
    if (!BQ27441_BlockDataClass(address, classId)) return false;
    if (!BQ27441_BlockDataOffset(address, blockOffset)) return false;

    return I2C_ReadRegister(address, BQ27441_REG_BLOCKDATA, block, BQ27441_BLOCKDATA_SIZE);
}

// The gauge stores (255 - (sum of the block's 32 bytes)) at
// BlockDataChecksum; writing it is what commits a modified block.
static uint8_t BQ27441_ComputeBlockChecksum(const uint8_t block[BQ27441_BLOCKDATA_SIZE])
{
    uint16_t sum = 0;
    uint8_t i;

    for (i = 0; i < BQ27441_BLOCKDATA_SIZE; i++)
    {
        sum += block[i];
    }

    return (uint8_t)(255u - (sum & 0xFFu));
}

static bool BQ27441_WriteExtendedBlock(uint16_t address, uint8_t classId, uint8_t blockOffset,
                                        const uint8_t block[BQ27441_BLOCKDATA_SIZE])
{
    uint8_t checksum;

    if (!BQ27441_BlockDataControl(address)) return false;
    if (!BQ27441_BlockDataClass(address, classId)) return false;
    if (!BQ27441_BlockDataOffset(address, blockOffset)) return false;

    if (!I2C_WriteRegister(address, BQ27441_REG_BLOCKDATA, block, BQ27441_BLOCKDATA_SIZE))
    {
        return false;
    }

    checksum = BQ27441_ComputeBlockChecksum(block);

    return I2C_WriteRegister(address, BQ27441_REG_BLOCKDATACHECKSUM, &checksum, 1);
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

    // *** VERIFY: confirm EXIT_RESIM (0x0044) is the TRM-sanctioned way
    // to exit CFGUPDATE and recompute against a data-memory change made
    // in this session, vs. a SOFT_RESET (0x0042) issued while still in
    // CFGUPDATE mode -- some reference implementations use the latter. ***
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

// Data-flash fields are big-endian (MSB first) -- opposite of the
// little-endian standard commands.
static uint16_t BQ27441_GetBlockWord(const uint8_t block[BQ27441_BLOCKDATA_SIZE], uint8_t offset)
{
    return ((uint16_t)block[offset] << 8) | block[offset + 1];
}

static void BQ27441_SetBlockWord(uint8_t block[BQ27441_BLOCKDATA_SIZE], uint8_t offset, uint16_t value)
{
    block[offset]     = (uint8_t)(value >> 8);
    block[offset + 1] = (uint8_t)(value & 0xFFu);
}

// Applies the OpConfig bits this board needs. Assumes the caller has already
// unsealed the gauge and entered CFGUPDATE mode.
static bool BQ27441_ApplyOpConfig(uint16_t address)
{
    uint8_t block[BQ27441_BLOCKDATA_SIZE];
    uint16_t opConfig;
    uint16_t desired;

    if (!BQ27441_ReadExtendedBlock(address, BQ27441_OPCONFIG_CLASS_ID, 0, block))
    {
        return false;
    }

    opConfig = BQ27441_GetBlockWord(block, BQ27441_OPCONFIG_BYTE_OFFSET);

    desired  = opConfig;
    desired |= BQ27441_OPCONFIG_TEMPS_EXTERNAL;   // external thermistor via BIN (TH1401)
    desired |= BQ27441_OPCONFIG_BATLOWEN;         // GPOUT mirrors SOC1 (-> BATT_LOWBATT_PIN)
    desired &= (uint16_t)~BQ27441_OPCONFIG_GPIOPOL; // GPIOPOL=0: GPOUT active-low when SOC1 asserted

    if (desired == opConfig)
    {
        return true;   // already configured -- don't spend a data-flash write
    }

    BQ27441_SetBlockWord(block, BQ27441_OPCONFIG_BYTE_OFFSET, desired);

    return BQ27441_WriteExtendedBlock(address, BQ27441_OPCONFIG_CLASS_ID, 0, block);
}

// Applies the pack description in `profile`. Assumes the caller has already
// unsealed the gauge and entered CFGUPDATE mode.
static bool BQ27441_ApplyBatteryProfile(uint16_t address, const BQ27441_BATTERY_PROFILE *profile)
{
    uint8_t block[BQ27441_BLOCKDATA_SIZE];
    bool changed = false;

    if (!BQ27441_ReadExtendedBlock(address, BQ27441_STATE_CLASS_ID, 0, block))
    {
        return false;
    }

    // Design Capacity and Design Energy must agree with each other or the
    // gauge's power/energy predictions drift apart from its charge ones, so
    // they are always written as a pair when either differs.
    if ((BQ27441_GetBlockWord(block, BQ27441_STATE_DESIGN_CAPACITY) != profile->designCapacity_mAh) ||
        (BQ27441_GetBlockWord(block, BQ27441_STATE_DESIGN_ENERGY)   != profile->designEnergy_mWh))
    {
        BQ27441_SetBlockWord(block, BQ27441_STATE_DESIGN_CAPACITY, profile->designCapacity_mAh);
        BQ27441_SetBlockWord(block, BQ27441_STATE_DESIGN_ENERGY,   profile->designEnergy_mWh);
        changed = true;
    }

    if (BQ27441_GetBlockWord(block, BQ27441_STATE_TERMINATE_VOLTAGE) != profile->terminateVoltage_mV)
    {
        BQ27441_SetBlockWord(block, BQ27441_STATE_TERMINATE_VOLTAGE, profile->terminateVoltage_mV);
        changed = true;
    }

    if (BQ27441_GetBlockWord(block, BQ27441_STATE_TAPER_RATE) != profile->taperRate)
    {
        BQ27441_SetBlockWord(block, BQ27441_STATE_TAPER_RATE, profile->taperRate);
        changed = true;
    }

    if (!changed)
    {
        return true;   // already programmed -- data flash has finite endurance
    }

    return BQ27441_WriteExtendedBlock(address, BQ27441_STATE_CLASS_ID, 0, block);
}

bool BQ27441_Configure(uint16_t address, const BQ27441_BATTERY_PROFILE *profile)
{
    bool ok;

    if (profile == NULL)
    {
        return false;
    }

    // Harmless if already unsealed -- see the key defines' comment above.
    if (!BQ27441_ControlWrite(address, BQ27441_UNSEAL_KEY_1) ||
        !BQ27441_ControlWrite(address, BQ27441_UNSEAL_KEY_2))
    {
        return false;
    }

    if (!BQ27441_EnterConfigUpdate(address))
    {
        return false;
    }

    // Both subclasses are updated inside one CFGUPDATE session: entering and
    // exiting is the expensive part (each exit triggers a resimulation), and
    // a half-applied configuration is worse than none.
    ok = BQ27441_ApplyOpConfig(address);

    if (!BQ27441_ApplyBatteryProfile(address, profile))
    {
        ok = false;
    }

    if (!BQ27441_ExitConfigUpdate(address))
    {
        ok = false;
    }

    return ok;
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
