/*******************************************************************************
  MCP9804 I2C Temperature Sensor Driver

  File Name:
    mcp9804.c

  Summary:
    Driver for the Microchip MCP9804 (and register-compatible MCP9805/9808
    family) I2C digital temperature sensor, built on i2c_master.h.
*******************************************************************************/

#include "i2c/device_driver/mcp9804.h"
#include "i2c/i2c_master.h"
#include "usb_uart/terminal_control.h"

#include <stdio.h>

// *****************************************************************************
// Section: Register Map
// *****************************************************************************
// All registers except RESOLUTION are 16-bit and transferred MSB first.

#define MCP9804_REG_CONFIG        0x01u
#define MCP9804_REG_T_UPPER       0x02u
#define MCP9804_REG_T_LOWER       0x03u
#define MCP9804_REG_T_CRIT        0x04u
#define MCP9804_REG_T_AMBIENT     0x05u
#define MCP9804_REG_MANUFACTURER  0x06u
#define MCP9804_REG_DEVICE_ID     0x07u
#define MCP9804_REG_RESOLUTION    0x08u

#define MCP9804_MANUFACTURER_ID   0x0054u
#define MCP9804_DEVICE_ID         0x02u   // upper byte of the Device ID/Revision register

// CONFIG register (0x01) bit fields used by this driver; the rest (alert
// pin behavior, hysteresis, register locks) are left at their power-on
// defaults since nothing here needs to touch them.
#define MCP9804_CONFIG_SHDN       0x0100u

// *****************************************************************************
// Section: Register Encode/Decode Helpers
// *****************************************************************************

static bool MCP9804_ReadReg16(uint16_t address, uint8_t reg, uint16_t *value)
{
    uint8_t raw[2];

    if (!I2C_ReadRegister(address, reg, raw, sizeof(raw)))
    {
        return false;
    }

    *value = ((uint16_t)raw[0] << 8) | raw[1];
    return true;
}

static bool MCP9804_WriteReg16(uint16_t address, uint8_t reg, uint16_t value)
{
    uint8_t raw[2] = { (uint8_t)(value >> 8), (uint8_t)(value & 0xFFu) };

    return I2C_WriteRegister(address, reg, raw, sizeof(raw));
}

// Converts a raw T_A/T_UPPER/T_LOWER/T_CRIT register value to degrees
// Celsius. Bits 15:13 (the alert flags, only meaningful on T_A) are ignored
// here -- see MCP9804_DecodeAlertFlags().
static float MCP9804_DecodeTemperature(uint16_t raw)
{
    bool negative = (raw & 0x1000u) != 0;
    uint16_t magnitude = raw & 0x0FFFu;
    float celsius = magnitude / 16.0f;

    if (negative)
    {
        celsius -= 256.0f;
    }

    return celsius;
}

// Encodes a Celsius value into the sign + 12-bit-magnitude format shared by
// T_UPPER/T_LOWER/T_CRIT (bits 15:13 are unimplemented on those registers).
static uint16_t MCP9804_EncodeTemperature(float celsius)
{
    bool negative = (celsius < 0.0f);
    float magnitude = negative ? (celsius + 256.0f) : celsius;
    uint16_t raw;

    if (magnitude < 0.0f)
    {
        magnitude = 0.0f;
    }
    if (magnitude > 255.9375f)
    {
        magnitude = 255.9375f;
    }

    raw = (uint16_t)(magnitude * 16.0f + 0.5f) & 0x0FFFu;
    if (negative)
    {
        raw |= 0x1000u;
    }

    return raw;
}

static void MCP9804_DecodeAlertFlags(uint16_t raw, MCP9804_ALERT_STATUS *status)
{
    status->aboveCritical = (raw & 0x8000u) != 0;
    status->aboveUpper    = (raw & 0x4000u) != 0;
    status->belowLower    = (raw & 0x2000u) != 0;
}

static bool MCP9804_ReadTempReg(uint16_t address, uint8_t reg, float *celsius)
{
    uint16_t raw;

    if (!MCP9804_ReadReg16(address, reg, &raw))
    {
        return false;
    }

    *celsius = MCP9804_DecodeTemperature(raw);
    return true;
}

// *****************************************************************************
// Section: Interface Routines
// *****************************************************************************

bool MCP9804_Verify(uint16_t address)
{
    uint16_t manufacturerId;
    uint16_t deviceId;

    if (!MCP9804_ReadReg16(address, MCP9804_REG_MANUFACTURER, &manufacturerId))
    {
        return false;
    }

    if (!MCP9804_ReadReg16(address, MCP9804_REG_DEVICE_ID, &deviceId))
    {
        return false;
    }

    return (manufacturerId == MCP9804_MANUFACTURER_ID) && ((deviceId >> 8) == MCP9804_DEVICE_ID);
}

bool MCP9804_ReadTemperature(uint16_t address, float *celsius)
{
    return MCP9804_ReadTempReg(address, MCP9804_REG_T_AMBIENT, celsius);
}

bool MCP9804_ReadTemperatureAndStatus(uint16_t address, float *celsius, MCP9804_ALERT_STATUS *status)
{
    uint16_t raw;

    if (!MCP9804_ReadReg16(address, MCP9804_REG_T_AMBIENT, &raw))
    {
        return false;
    }

    *celsius = MCP9804_DecodeTemperature(raw);
    MCP9804_DecodeAlertFlags(raw, status);
    return true;
}

bool MCP9804_QueueReadTemperature(uint16_t address, uint8_t raw[2],
                                  I2C_TRANSFER_CALLBACK callback, uintptr_t context)
{
    return I2C_QueueReadRegister(address, MCP9804_REG_T_AMBIENT, raw, 2, callback, context);
}

float MCP9804_DecodeTemperatureRaw(const uint8_t raw[2])
{
    return MCP9804_DecodeTemperature(((uint16_t)raw[0] << 8) | raw[1]);
}

void MCP9804_DecodeAlertFlagsRaw(const uint8_t raw[2], MCP9804_ALERT_STATUS *status)
{
    MCP9804_DecodeAlertFlags(((uint16_t)raw[0] << 8) | raw[1], status);
}

bool MCP9804_SetResolution(uint16_t address, MCP9804_RESOLUTION resolution)
{
    uint8_t value = (uint8_t)resolution & 0x03u;

    return I2C_WriteRegister(address, MCP9804_REG_RESOLUTION, &value, 1);
}

bool MCP9804_SetUpperLimit(uint16_t address, float celsius)
{
    return MCP9804_WriteReg16(address, MCP9804_REG_T_UPPER, MCP9804_EncodeTemperature(celsius));
}

bool MCP9804_SetLowerLimit(uint16_t address, float celsius)
{
    return MCP9804_WriteReg16(address, MCP9804_REG_T_LOWER, MCP9804_EncodeTemperature(celsius));
}

bool MCP9804_SetCriticalLimit(uint16_t address, float celsius)
{
    return MCP9804_WriteReg16(address, MCP9804_REG_T_CRIT, MCP9804_EncodeTemperature(celsius));
}

bool MCP9804_SetShutdown(uint16_t address, bool shutdown)
{
    uint16_t config;

    if (!MCP9804_ReadReg16(address, MCP9804_REG_CONFIG, &config))
    {
        return false;
    }

    if (shutdown)
    {
        config |= MCP9804_CONFIG_SHDN;
    }
    else
    {
        config &= (uint16_t)~MCP9804_CONFIG_SHDN;
    }

    return MCP9804_WriteReg16(address, MCP9804_REG_CONFIG, config);
}

void MCP9804_PrintStatus(uint16_t address)
{
    uint16_t manufacturerId;
    uint16_t deviceId;
    uint16_t config;
    float celsius;
    float limit;
    MCP9804_ALERT_STATUS status;
    bool identified;

    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, BOLD_FONT);
    printf("    --- MCP9804 (address 0x%02X) ---\n\r", address);

    if (!MCP9804_ReadReg16(address, MCP9804_REG_MANUFACTURER, &manufacturerId) ||
        !MCP9804_ReadReg16(address, MCP9804_REG_DEVICE_ID, &deviceId))
    {
        terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("    No response from device (I2C error: %d)\n\r", (int)I2C_ErrorGet());
        terminalTextAttributesReset();
        return;
    }

    identified = (manufacturerId == MCP9804_MANUFACTURER_ID) && ((deviceId >> 8) == MCP9804_DEVICE_ID);

    terminalTextAttributes(identified ? GREEN_COLOR : RED_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("    Manufacturer ID: 0x%04X, Device ID: 0x%02X, Revision: 0x%02X (%s)\n\r",
           manufacturerId, (deviceId >> 8) & 0xFFu, deviceId & 0xFFu,
           identified ? "recognized" : "unrecognized");

    if (MCP9804_ReadReg16(address, MCP9804_REG_CONFIG, &config))
    {
        terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("    Shutdown mode: %s\n\r", (config & MCP9804_CONFIG_SHDN) ? "enabled" : "disabled");
    }

    if (MCP9804_ReadTempReg(address, MCP9804_REG_T_UPPER, &limit))
    {
        printf("    T_UPPER limit: %.4f C\n\r", limit);
    }
    if (MCP9804_ReadTempReg(address, MCP9804_REG_T_LOWER, &limit))
    {
        printf("    T_LOWER limit: %.4f C\n\r", limit);
    }
    if (MCP9804_ReadTempReg(address, MCP9804_REG_T_CRIT, &limit))
    {
        printf("    T_CRIT limit:  %.4f C\n\r", limit);
    }

    if (MCP9804_ReadTemperatureAndStatus(address, &celsius, &status))
    {
        bool anyAlert = status.aboveCritical || status.aboveUpper || status.belowLower;

        terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("    Ambient temperature: %.4f C\n\r", celsius);

        terminalTextAttributes(anyAlert ? YELLOW_COLOR : GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("    Alerts: %s%s%s%s\n\r",
               status.aboveCritical ? "ABOVE CRITICAL " : "",
               status.aboveUpper    ? "ABOVE UPPER "    : "",
               status.belowLower    ? "BELOW LOWER "    : "",
               anyAlert ? "" : "none");
    }

    terminalTextAttributesReset();
}
