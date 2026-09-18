/*******************************************************************************
  INA231A I2C Current/Power Monitor Driver

  File Name:
    ina231a.c

  Summary:
    Driver for the Texas Instruments INA231A bidirectional current-shunt and
    power monitor, built on i2c_master.h.
*******************************************************************************/

#include "i2c/device_driver/ina231a.h"
#include "i2c/i2c_master.h"
#include "usb_uart/terminal_control.h"

#include <stdio.h>
#include <string.h>

// *****************************************************************************
// Section: Register Map
// *****************************************************************************
// All registers are 16-bit and transferred MSB first.

#define INA231A_REG_CONFIG          0x00u
#define INA231A_REG_SHUNT_VOLTAGE   0x01u   // read-only, signed
#define INA231A_REG_BUS_VOLTAGE     0x02u   // read-only, unsigned
#define INA231A_REG_POWER           0x03u   // read-only, unsigned
#define INA231A_REG_CURRENT         0x04u   // read-only, signed
#define INA231A_REG_CALIBRATION     0x05u

// Documented power-on-reset value of the Configuration register (16 x
// averaging disabled, 1.1ms bus/shunt conversion time, continuous
// shunt+bus mode). Used by INA231A_Verify() -- see its caveat below.
#define INA231A_REG_MASK_ENABLE     0x06u
#define INA231A_REG_ALERT_LIMIT     0x07u

#define INA231A_CONFIG_POR_DEFAULT  0x4127u

// Configuration register (0x00) fields, decoded by INA231A_PrintStatus().
#define INA231A_CONFIG_RST          0x8000u
#define INA231A_CONFIG_AVG_MASK     0x0E00u   // averaging count
#define INA231A_CONFIG_VBUSCT_MASK  0x01C0u   // bus voltage conversion time
#define INA231A_CONFIG_VSHCT_MASK   0x0038u   // shunt voltage conversion time
#define INA231A_CONFIG_MODE_MASK    0x0007u

// Mask/Enable register (0x06): alert sources in the high bits, live status
// flags in the low bits. OVF and CVRF are the two worth watching -- OVF
// means the power/current math overflowed and those registers are invalid.
#define INA231A_MASK_SOL            0x8000u   // shunt over-voltage alert
#define INA231A_MASK_SUL            0x4000u   // shunt under-voltage alert
#define INA231A_MASK_BOL            0x2000u   // bus over-voltage alert
#define INA231A_MASK_BUL            0x1000u   // bus under-voltage alert
#define INA231A_MASK_POL            0x0800u   // power over-limit alert
#define INA231A_MASK_CNVR           0x0400u   // conversion ready alert
#define INA231A_MASK_AFF            0x0010u   // alert function flag
#define INA231A_MASK_CVRF           0x0008u   // conversion ready flag
#define INA231A_MASK_OVF            0x0004u   // math overflow flag
#define INA231A_MASK_APOL           0x0002u   // alert polarity (1 = active high)
#define INA231A_MASK_LEN            0x0001u   // alert latch enable

// Fixed scaling constants shared by the whole INA226/INA230/INA231 family.
#define INA231A_BUS_VOLTAGE_LSB     0.00125f     // 1.25 mV/LSB
#define INA231A_SHUNT_VOLTAGE_LSB   0.0000025f   // 2.5 uV/LSB
#define INA231A_POWER_LSB_RATIO     25.0f        // Power_LSB = 25 x Current_LSB
#define INA231A_CALIBRATION_CONST   0.00512f     // Cal = 0.00512 / (Current_LSB * Rshunt)

// *****************************************************************************
// Section: Register Encode/Decode Helpers
// *****************************************************************************

static bool INA231A_ReadReg16(uint16_t address, uint8_t reg, uint16_t *value)
{
    uint8_t raw[2];

    if (!I2C_ReadRegister(address, reg, raw, sizeof(raw)))
    {
        return false;
    }

    *value = ((uint16_t)raw[0] << 8) | raw[1];
    return true;
}

static bool INA231A_WriteReg16(uint16_t address, uint8_t reg, uint16_t value)
{
    uint8_t raw[2] = { (uint8_t)(value >> 8), (uint8_t)(value & 0xFFu) };

    return I2C_WriteRegister(address, reg, raw, sizeof(raw));
}

static float INA231A_DecodeBusVoltage(uint16_t raw)
{
    return (float)raw * INA231A_BUS_VOLTAGE_LSB;
}

static float INA231A_DecodeShuntVoltage(uint16_t raw)
{
    return (float)(int16_t)raw * INA231A_SHUNT_VOLTAGE_LSB;
}

static float INA231A_DecodeCurrent(uint16_t raw, float currentLSB)
{
    return (float)(int16_t)raw * currentLSB;
}

static float INA231A_DecodePower(uint16_t raw, float currentLSB)
{
    return (float)raw * (currentLSB * INA231A_POWER_LSB_RATIO);
}

// *****************************************************************************
// Section: Interface Routines
// *****************************************************************************

// The INA231A doesn't expose a manufacturer/device ID register the way the
// MCP9804 does, so this checks the Configuration register against its
// documented power-on-reset value. A device whose CONFIG doesn't match is
// NOT immediately reported absent: an INA231A that was reconfigured in a
// previous session and then warm-reset arrives here alive but non-default
// -- enterLowPowerSleep() parks every INA231A in MODE=0, and the Software
// Reset it wakes into doesn't power-cycle the sensors, so before 2026-07-22
// every post-sleep boot misreported all of them as I2C errors until a
// battery pull. Instead, command a device self-reset (the RST bit, which
// self-clears and restores POR defaults) and re-check: a real INA231A
// comes back reading 0x4127, anything else at this address won't.
bool INA231A_Verify(uint16_t address)
{
    uint16_t config;

    if (!INA231A_ReadReg16(address, INA231A_REG_CONFIG, &config))
    {
        return false;
    }

    if (config == INA231A_CONFIG_POR_DEFAULT)
    {
        return true;
    }

    if (!INA231A_WriteReg16(address, INA231A_REG_CONFIG, INA231A_CONFIG_RST))
    {
        return false;
    }

    if (!INA231A_ReadReg16(address, INA231A_REG_CONFIG, &config))
    {
        return false;
    }

    return (config == INA231A_CONFIG_POR_DEFAULT);
}

bool INA231A_Configure(uint16_t address, float shuntResistanceOhms,
                        float maxExpectedCurrentAmps, float *currentLSBOut)
{
    float currentLSB;
    uint16_t calibration;

    if ((shuntResistanceOhms <= 0.0f) || (maxExpectedCurrentAmps <= 0.0f) || (currentLSBOut == NULL))
    {
        return false;
    }

    // Current_LSB is sized so the signed 16-bit Current register doesn't
    // clip at the largest current this rail should ever see.
    currentLSB = maxExpectedCurrentAmps / 32768.0f;

    calibration = (uint16_t)(INA231A_CALIBRATION_CONST / (currentLSB * shuntResistanceOhms));

    if (!INA231A_WriteReg16(address, INA231A_REG_CALIBRATION, calibration))
    {
        return false;
    }

    *currentLSBOut = currentLSB;
    return true;
}

bool INA231A_ReadBusVoltage(uint16_t address, float *volts)
{
    uint16_t raw;

    if (!INA231A_ReadReg16(address, INA231A_REG_BUS_VOLTAGE, &raw))
    {
        return false;
    }

    *volts = INA231A_DecodeBusVoltage(raw);
    return true;
}

bool INA231A_ReadShuntVoltage(uint16_t address, float *volts)
{
    uint16_t raw;

    if (!INA231A_ReadReg16(address, INA231A_REG_SHUNT_VOLTAGE, &raw))
    {
        return false;
    }

    *volts = INA231A_DecodeShuntVoltage(raw);
    return true;
}

bool INA231A_ReadCurrent(uint16_t address, float currentLSB, float *amps)
{
    uint16_t raw;

    if (!INA231A_ReadReg16(address, INA231A_REG_CURRENT, &raw))
    {
        return false;
    }

    *amps = INA231A_DecodeCurrent(raw, currentLSB);
    return true;
}

bool INA231A_ReadPower(uint16_t address, float currentLSB, float *watts)
{
    uint16_t raw;

    if (!INA231A_ReadReg16(address, INA231A_REG_POWER, &raw))
    {
        return false;
    }

    *watts = INA231A_DecodePower(raw, currentLSB);
    return true;
}

bool INA231A_QueueReadBusVoltage(uint16_t address, uint8_t raw[2],
                                 I2C_TRANSFER_CALLBACK callback, uintptr_t context)
{
    return I2C_QueueReadRegister(address, INA231A_REG_BUS_VOLTAGE, raw, 2, callback, context);
}

bool INA231A_QueueReadCurrent(uint16_t address, uint8_t raw[2],
                              I2C_TRANSFER_CALLBACK callback, uintptr_t context)
{
    return I2C_QueueReadRegister(address, INA231A_REG_CURRENT, raw, 2, callback, context);
}

bool INA231A_QueueReadPower(uint16_t address, uint8_t raw[2],
                            I2C_TRANSFER_CALLBACK callback, uintptr_t context)
{
    return I2C_QueueReadRegister(address, INA231A_REG_POWER, raw, 2, callback, context);
}

float INA231A_DecodeBusVoltageRaw(const uint8_t raw[2])
{
    return INA231A_DecodeBusVoltage(((uint16_t)raw[0] << 8) | raw[1]);
}

float INA231A_DecodeCurrentRaw(const uint8_t raw[2], float currentLSB)
{
    return INA231A_DecodeCurrent(((uint16_t)raw[0] << 8) | raw[1], currentLSB);
}

float INA231A_DecodePowerRaw(const uint8_t raw[2], float currentLSB)
{
    return INA231A_DecodePower(((uint16_t)raw[0] << 8) | raw[1], currentLSB);
}

bool INA231A_ReadAll(uint16_t address, float currentLSB, INA231A_READING *reading)
{
    if (!INA231A_ReadBusVoltage(address, &reading->busVoltage))
    {
        return false;
    }

    if (!INA231A_ReadShuntVoltage(address, &reading->shuntVoltage))
    {
        return false;
    }

    if (!INA231A_ReadCurrent(address, currentLSB, &reading->current))
    {
        return false;
    }

    if (!INA231A_ReadPower(address, currentLSB, &reading->power))
    {
        return false;
    }

    return true;
}

bool INA231A_SetPowerDown(uint16_t address, bool powerDown)
{
    uint16_t config;

    if (!INA231A_ReadReg16(address, INA231A_REG_CONFIG, &config))
    {
        return false;
    }

    config &= (uint16_t)~INA231A_CONFIG_MODE_MASK;

    // MODE = 0 is power-down; anything else resumes conversions. 0b111 is
    // shunt+bus continuous, the mode this board runs in normally.
    if (!powerDown)
    {
        config |= INA231A_CONFIG_MODE_MASK;
    }

    // The Calibration register is unaffected by a mode change, so resuming
    // does not need INA231A_Configure() to run again.
    return INA231A_WriteReg16(address, INA231A_REG_CONFIG, config);
}

// AVG/VBUSCT/VSHCT all index their own table. Public (see ina231a.h) so the
// GUI's I2C status screen decodes them identically rather than growing its
// own copy of the same tables.
const char* INA231A_AveragingName(uint16_t config)
{
    static const char* const names[8] = { "1", "4", "16", "64", "128", "256", "512", "1024" };

    return names[(config & INA231A_CONFIG_AVG_MASK) >> 9];
}

const char* INA231A_ConversionTimeName(uint16_t field)
{
    static const char* const names[8] = { "140 us", "204 us", "332 us", "588 us",
                                          "1.1 ms", "2.116 ms", "4.156 ms", "8.244 ms" };

    return names[field & 0x7u];
}

const char* INA231A_ModeName(uint16_t config)
{
    switch (config & INA231A_CONFIG_MODE_MASK)
    {
        case 0x0u: return "power-down";
        case 0x1u: return "shunt triggered";
        case 0x2u: return "bus triggered";
        case 0x3u: return "shunt+bus triggered";
        case 0x4u: return "power-down";
        case 0x5u: return "shunt continuous";
        case 0x6u: return "bus continuous";
        default:   return "shunt+bus continuous";
    }
}

bool INA231A_ReadDiagnostics(uint16_t address, INA231A_DIAGNOSTICS *out)
{
    uint16_t rawCurrent;

    if (out == NULL) return false;

    // Cleared first so every *Valid flag starts false: a register the reads
    // below never reach then reports "not read" rather than a stale value
    memset(out, 0, sizeof(*out));

    if (!INA231A_ReadReg16(address, INA231A_REG_CONFIG, &out->config))
    {
        return false;
    }

    out->configValid = true;
    out->porDefault = (out->config == INA231A_CONFIG_POR_DEFAULT);

    out->maskEnableValid = INA231A_ReadReg16(address, INA231A_REG_MASK_ENABLE, &out->maskEnable);
    out->mathOverflow = out->maskEnableValid &&
            ((out->maskEnable & INA231A_MASK_OVF) != 0);

    out->calibrationValid = INA231A_ReadReg16(address, INA231A_REG_CALIBRATION, &out->calibration);

    out->busVoltageValid = INA231A_ReadBusVoltage(address, &out->busVoltage);
    out->shuntVoltageValid = INA231A_ReadShuntVoltage(address, &out->shuntVoltage);

    // Raw codes, not amps/watts: converting needs the current LSB that
    // INA231A_Configure() derived from the shunt, which this driver does not
    // retain. The caller that knows it can scale them; the point of showing
    // the codes is that they are what the part actually holds.
    out->rawCurrentValid = INA231A_ReadReg16(address, INA231A_REG_CURRENT, &rawCurrent);
    out->rawCurrent = (int16_t)rawCurrent;

    out->rawPowerValid = INA231A_ReadReg16(address, INA231A_REG_POWER, &out->rawPower);

    return true;
}

void INA231A_PrintStatus(uint16_t address)
{
    INA231A_DIAGNOSTICS diag;
    uint16_t config;
    uint16_t maskEnable;
    uint16_t calibration;
    bool porDefault;

    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, BOLD_FONT);
    printf("    --- INA231A (address 0x%02X) ---\n\r", address);

    // One pass over the registers, shared with the GUI's I2C status screen,
    // so the two cannot disagree about what this part is reporting
    if (!INA231A_ReadDiagnostics(address, &diag))
    {
        terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("    No response from device (I2C error: %d)\n\r", (int)I2C_ErrorGet());
        terminalTextAttributesReset();
        return;
    }

    config = diag.config;
    maskEnable = diag.maskEnable;
    calibration = diag.calibration;
    porDefault = diag.porDefault;
    terminalTextAttributes(porDefault ? GREEN_COLOR : YELLOW_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("    Configuration register: 0x%04X (%s%s)\n\r", config,
           (config & INA231A_CONFIG_RST) ? "RST " : "",
           porDefault ? "power-on-reset default" : "reconfigured, or not an INA231A");
    printf("        Mode: %s, Averaging: %s samples\n\r",
           INA231A_ModeName(config), INA231A_AveragingName(config));
    printf("        Conversion time -- bus: %s, shunt: %s\n\r",
           INA231A_ConversionTimeName((config & INA231A_CONFIG_VBUSCT_MASK) >> 6),
           INA231A_ConversionTimeName((config & INA231A_CONFIG_VSHCT_MASK) >> 3));

    if (diag.maskEnableValid)
    {
        // OVF invalidates the Current/Power registers, so flag it loudly
        terminalTextAttributes((maskEnable & INA231A_MASK_OVF) ? RED_COLOR : GREEN_COLOR,
                               BLACK_COLOR, NORMAL_FONT);
        printf("    Mask/Enable register: 0x%04X (%s%s%s%s%s%s%s%s%s%s%s)\n\r", maskEnable,
               (maskEnable & INA231A_MASK_SOL)  ? "SOL "  : "",
               (maskEnable & INA231A_MASK_SUL)  ? "SUL "  : "",
               (maskEnable & INA231A_MASK_BOL)  ? "BOL "  : "",
               (maskEnable & INA231A_MASK_BUL)  ? "BUL "  : "",
               (maskEnable & INA231A_MASK_POL)  ? "POL "  : "",
               (maskEnable & INA231A_MASK_CNVR) ? "CNVR " : "",
               (maskEnable & INA231A_MASK_AFF)  ? "AFF "  : "",
               (maskEnable & INA231A_MASK_CVRF) ? "CVRF " : "",
               (maskEnable & INA231A_MASK_OVF)  ? "OVF "  : "",
               (maskEnable & INA231A_MASK_APOL) ? "APOL " : "",
               (maskEnable & INA231A_MASK_LEN)  ? "LEN "  : "");

        if (maskEnable & INA231A_MASK_OVF)
        {
            printf("        MATH OVERFLOW -- Current and Power registers are invalid\n\r");
        }
        terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    }

    if (diag.calibrationValid)
    {
        terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("    Calibration register: 0x%04X%s\n\r", calibration,
               (calibration == 0) ? " (uncalibrated -- Current/Power read as 0)" : "");
    }

    if (diag.busVoltageValid)
    {
        printf("    Bus voltage: %.4f V\n\r", diag.busVoltage);
    }

    if (diag.shuntVoltageValid)
    {
        printf("    Shunt voltage: %.6f V\n\r", diag.shuntVoltage);
    }

    if (diag.rawCurrentValid)
    {
        printf("    Current register (raw code, needs INA231A_Configure() to convert): 0x%04X (%d)\n\r",
               (uint16_t)diag.rawCurrent, diag.rawCurrent);
    }

    if (diag.rawPowerValid)
    {
        printf("    Power register (raw code, needs INA231A_Configure() to convert): 0x%04X (%u)\n\r",
               diag.rawPower, diag.rawPower);
    }

    terminalTextAttributesReset();
}
