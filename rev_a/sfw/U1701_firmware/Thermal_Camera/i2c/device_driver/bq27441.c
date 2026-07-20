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
#define BQ27441_UNSEAL_KEY_1           0x0414u
#define BQ27441_UNSEAL_KEY_2           0x3672u

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

// *** VERIFY BEFORE RELYING ON IN THE FIELD (do not guess-and-ship): ***
// - BQ27441_OPCONFIG_CLASS_ID: the "Registers" data-memory subclass ID
//   that contains OpConfig -- confirmed against SLUUAC9's data-memory
//   table to be subclass 64 for this family; double-check against the
//   TRM copy in hand before shipping.
// - BQ27441_OPCONFIG_BYTE_OFFSET: which byte(s) within that subclass's
//   first 32-byte block hold the 2-byte OpConfig register -- assumed to
//   be the first 2 bytes (offset 0), matching the common TI data-memory
//   layout, but not independently confirmed here.
// - Byte order of OpConfig WITHIN the block-data window: assumed
//   big-endian (MSB first), matching TI's documented data-flash
//   convention (opposite of the little-endian standard commands above).
// - OPCONFIG_TEMPS bit polarity: confirm which value (0 or 1) selects
//   external-thermistor-via-BIN vs internal die sensor before shipping.
#define BQ27441_OPCONFIG_CLASS_ID          64u     // TRM "Registers" subclass -- VERIFY
#define BQ27441_OPCONFIG_BYTE_OFFSET       0u      // VERIFY exact offset within the block
#define BQ27441_OPCONFIG_TEMPS_EXTERNAL    0x0001u // VERIFY polarity against TRM before shipping
#define BQ27441_OPCONFIG_BATLOWEN          0x0004u // 1 = GPOUT mirrors SOC1 instead of SOC_INT
#define BQ27441_OPCONFIG_GPIOPOL           0x0800u // 0 = GPOUT active-low when SOC1 asserted, 1 = active-high

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

bool BQ27441_ConfigureOpConfig(uint16_t address)
{
    uint8_t block[BQ27441_BLOCKDATA_SIZE];
    uint16_t opConfig;
    uint16_t desiredOpConfig;
    bool ok = true;

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

    if (!BQ27441_ReadExtendedBlock(address, BQ27441_OPCONFIG_CLASS_ID, 0, block))
    {
        BQ27441_ExitConfigUpdate(address);
        return false;
    }

    // Data-flash fields are big-endian (MSB first) -- opposite of the
    // little-endian standard commands. See the VERIFY comment on
    // BQ27441_OPCONFIG_BYTE_OFFSET above.
    opConfig = ((uint16_t)block[BQ27441_OPCONFIG_BYTE_OFFSET] << 8) |
               block[BQ27441_OPCONFIG_BYTE_OFFSET + 1];

    desiredOpConfig = opConfig;
    desiredOpConfig |= BQ27441_OPCONFIG_TEMPS_EXTERNAL;   // external thermistor via BIN (TH1401)
    desiredOpConfig |= BQ27441_OPCONFIG_BATLOWEN;         // GPOUT mirrors SOC1 (-> BATT_LOWBATT_PIN)
    desiredOpConfig &= (uint16_t)~BQ27441_OPCONFIG_GPIOPOL; // GPIOPOL=0: GPOUT active-low when SOC1 asserted

    if (desiredOpConfig != opConfig)
    {
        block[BQ27441_OPCONFIG_BYTE_OFFSET]     = (uint8_t)(desiredOpConfig >> 8);
        block[BQ27441_OPCONFIG_BYTE_OFFSET + 1] = (uint8_t)(desiredOpConfig & 0xFFu);

        ok = BQ27441_WriteExtendedBlock(address, BQ27441_OPCONFIG_CLASS_ID, 0, block);
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
