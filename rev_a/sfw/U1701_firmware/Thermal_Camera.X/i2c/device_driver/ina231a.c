/*******************************************************************************
  INA231A I2C Current/Power Monitor Driver

  File Name:
    ina231a.c

  Summary:
    Driver for the Texas Instruments INA231A bidirectional current-shunt and
    power monitor, built on plib_i2c.h.
*******************************************************************************/

#include "i2c/device_driver/ina231a.h"
#include "i2c/plib_i2c.h"
#include "usb_uart/terminal_control.h"

#include <stdio.h>

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
#define INA231A_CONFIG_POR_DEFAULT  0x4127u

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
// MCP9804 does, so this can only check the Configuration register against
// its documented power-on-reset value. That means this is reliable right
// after a power cycle, but returns false-negative "not present" for a
// device that's alive and working but whose Configuration/Calibration
// registers were already written this session (e.g. after a warm MCU reset
// that didn't power-cycle the INA231A, or if Verify() is called again after
// INA231A_Configure()).
bool INA231A_Verify(uint16_t address)
{
    uint16_t config;

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

void INA231A_PrintStatus(uint16_t address)
{
    uint16_t config;
    uint16_t calibration;
    uint16_t rawCurrent;
    uint16_t rawPower;
    float busVoltage;
    float shuntVoltage;
    bool porDefault;

    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, BOLD_FONT);
    printf("    --- INA231A (address 0x%02X) ---\n\r", address);

    if (!INA231A_ReadReg16(address, INA231A_REG_CONFIG, &config))
    {
        terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("    No response from device (I2C error: %d)\n\r", (int)I2C_ErrorGet());
        terminalTextAttributesReset();
        return;
    }

    porDefault = (config == INA231A_CONFIG_POR_DEFAULT);
    terminalTextAttributes(porDefault ? GREEN_COLOR : YELLOW_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("    Configuration register: 0x%04X (%s)\n\r", config,
           porDefault ? "power-on-reset default" : "reconfigured, or not an INA231A");

    if (INA231A_ReadReg16(address, INA231A_REG_CALIBRATION, &calibration))
    {
        terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("    Calibration register: 0x%04X%s\n\r", calibration,
               (calibration == 0) ? " (uncalibrated -- Current/Power read as 0)" : "");
    }

    if (INA231A_ReadBusVoltage(address, &busVoltage))
    {
        printf("    Bus voltage: %.4f V\n\r", busVoltage);
    }

    if (INA231A_ReadShuntVoltage(address, &shuntVoltage))
    {
        printf("    Shunt voltage: %.6f V\n\r", shuntVoltage);
    }

    if (INA231A_ReadReg16(address, INA231A_REG_CURRENT, &rawCurrent))
    {
        printf("    Current register (raw code, needs INA231A_Configure() to convert): 0x%04X (%d)\n\r",
               rawCurrent, (int16_t)rawCurrent);
    }

    if (INA231A_ReadReg16(address, INA231A_REG_POWER, &rawPower))
    {
        printf("    Power register (raw code, needs INA231A_Configure() to convert): 0x%04X (%u)\n\r",
               rawPower, rawPower);
    }

    terminalTextAttributesReset();
}
