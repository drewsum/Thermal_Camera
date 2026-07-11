/*******************************************************************************
  I2C Device Registry

  File Name:
    i2c_devices.c

  Summary:
    The single interface for every physical I2C device on the board:
    presence, address, name, register-level status printing, and
    kind-specific typed reads, all dispatched by device kind.
*******************************************************************************/

#include "i2c/i2c_devices.h"
#include "i2c/device_driver/mcp9804.h"
#include "i2c/device_driver/ina231a.h"
#include "usb_uart/terminal_control.h"

#include <stdio.h>

#define I2C_DEVICE_ADDRESS_ENTRY(name, kind, address, label)  (address),
static const uint16_t i2cDeviceAddresses[I2C_DEVICE_COUNT] =
{
    I2C_DEVICE_LIST(I2C_DEVICE_ADDRESS_ENTRY)
};

#define I2C_DEVICE_KIND_ENTRY(name, kind, address, label)  (kind),
static const I2C_DEVICE_KIND i2cDeviceKinds[I2C_DEVICE_COUNT] =
{
    I2C_DEVICE_LIST(I2C_DEVICE_KIND_ENTRY)
};

#define I2C_DEVICE_NAME_ENTRY(name, kind, address, label)  label,
static const char* const i2cDeviceNames[I2C_DEVICE_COUNT] =
{
    I2C_DEVICE_LIST(I2C_DEVICE_NAME_ENTRY)
};

static bool i2cDevicePresent[I2C_DEVICE_COUNT];

// Only meaningful for I2C_DEVICE_KIND_INA231A entries -- the Current_LSB
// (amps/bit) I2CDevices_ConfigureOne() got back from INA231A_Configure().
static float i2cDeviceCurrentLSB[I2C_DEVICE_COUNT];

// All 6 INA231A power monitors on this board share this shunt resistor and
// expected max current -- update here (or move into I2C_DEVICE_LIST as a
// per-device column) if that ever differs per rail.
#define INA231A_SHUNT_RESISTANCE_OHMS       0.02f
#define INA231A_MAX_EXPECTED_CURRENT_AMPS   3.1f

static bool I2CDevices_IdIsValid(I2C_DEVICE_ID id)
{
    return ((unsigned)id < (unsigned)I2C_DEVICE_COUNT);
}

// Dispatches the presence/identification check for `id` to its kind's driver.
static bool I2CDevices_Verify(I2C_DEVICE_ID id)
{
    switch (i2cDeviceKinds[id])
    {
        case I2C_DEVICE_KIND_MCP9804:
            return MCP9804_Verify(i2cDeviceAddresses[id]);

        case I2C_DEVICE_KIND_INA231A:
            return INA231A_Verify(i2cDeviceAddresses[id]);

        default:
            return false;
    }
}

// Dispatches kind-specific one-time setup for `id`, called once presence is
// confirmed. Kinds that need no setup (MCP9804) just return true.
static bool I2CDevices_ConfigureOne(I2C_DEVICE_ID id)
{
    switch (i2cDeviceKinds[id])
    {
        case I2C_DEVICE_KIND_MCP9804:
            return true;

        case I2C_DEVICE_KIND_INA231A:
            return INA231A_Configure(i2cDeviceAddresses[id],
                                      INA231A_SHUNT_RESISTANCE_OHMS,
                                      INA231A_MAX_EXPECTED_CURRENT_AMPS,
                                      &i2cDeviceCurrentLSB[id]);

        default:
            return false;
    }
}

// Dispatches the full register status dump for `id` to its kind's driver.
static void I2CDevices_PrintOne(I2C_DEVICE_ID id)
{
    switch (i2cDeviceKinds[id])
    {
        case I2C_DEVICE_KIND_MCP9804:
            MCP9804_PrintStatus(i2cDeviceAddresses[id]);
            break;

        case I2C_DEVICE_KIND_INA231A:
            INA231A_PrintStatus(i2cDeviceAddresses[id]);
            break;

        default:
            terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
            printf("    Unknown device kind\n\r");
            break;
    }
}

bool I2CDevices_Initialize(void)
{
    bool allPresent = true;
    uint8_t id;

    for (id = 0; id < I2C_DEVICE_COUNT; id++)
    {
        i2cDevicePresent[id] = I2CDevices_Verify((I2C_DEVICE_ID)id);
        allPresent = allPresent && i2cDevicePresent[id];

        // Only configure devices that are actually there -- e.g. writing an
        // INA231A calibration register to an address nothing ACKed is pointless.
        if (i2cDevicePresent[id] && !I2CDevices_ConfigureOne((I2C_DEVICE_ID)id))
        {
            allPresent = false;
        }
    }

    return allPresent;
}

bool I2CDevices_IsPresent(I2C_DEVICE_ID id)
{
    if (!I2CDevices_IdIsValid(id))
    {
        return false;
    }

    return i2cDevicePresent[id];
}

const char* I2CDevices_GetName(I2C_DEVICE_ID id)
{
    if (!I2CDevices_IdIsValid(id))
    {
        return "Invalid Device ID";
    }

    return i2cDeviceNames[id];
}

uint16_t I2CDevices_GetAddress(I2C_DEVICE_ID id)
{
    if (!I2CDevices_IdIsValid(id))
    {
        return 0;
    }

    return i2cDeviceAddresses[id];
}

void I2CDevices_PrintStatus(void)
{
    uint8_t id;

    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, BOLD_FONT);
    printf("    --- I2C Devices ---\n\r");

    for (id = 0; id < I2C_DEVICE_COUNT; id++)
    {
        terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, BOLD_FONT);
        printf("    %s\n\r", i2cDeviceNames[id]);

        I2CDevices_PrintOne((I2C_DEVICE_ID)id);
    }

    terminalTextAttributesReset();
}

bool I2CDevices_ReadTemperature(I2C_DEVICE_ID id, float *celsius)
{
    if (!I2CDevices_IdIsValid(id) || (i2cDeviceKinds[id] != I2C_DEVICE_KIND_MCP9804))
    {
        return false;
    }

    return MCP9804_ReadTemperature(i2cDeviceAddresses[id], celsius);
}

bool I2CDevices_ReadTemperatureWithStatus(I2C_DEVICE_ID id, I2C_DEVICE_TEMP_READING *reading)
{
    if (!I2CDevices_IdIsValid(id) || (i2cDeviceKinds[id] != I2C_DEVICE_KIND_MCP9804))
    {
        reading->present = false;
        return false;
    }

    reading->present = MCP9804_ReadTemperatureAndStatus(i2cDeviceAddresses[id],
                                                          &reading->celsius, &reading->alerts);

    return reading->present;
}

uint8_t I2CDevices_ReadAllTemperatures(I2C_DEVICE_TEMP_READING readings[I2C_DEVICE_COUNT])
{
    uint8_t successCount = 0;
    uint8_t id;

    for (id = 0; id < I2C_DEVICE_COUNT; id++)
    {
        if (I2CDevices_ReadTemperatureWithStatus((I2C_DEVICE_ID)id, &readings[id]))
        {
            successCount++;
        }
    }

    return successCount;
}

bool I2CDevices_ReadVoltage(I2C_DEVICE_ID id, float *volts)
{
    if (!I2CDevices_IdIsValid(id) || (i2cDeviceKinds[id] != I2C_DEVICE_KIND_INA231A))
    {
        return false;
    }

    return INA231A_ReadBusVoltage(i2cDeviceAddresses[id], volts);
}

bool I2CDevices_ReadCurrent(I2C_DEVICE_ID id, float *amps)
{
    if (!I2CDevices_IdIsValid(id) || (i2cDeviceKinds[id] != I2C_DEVICE_KIND_INA231A))
    {
        return false;
    }

    return INA231A_ReadCurrent(i2cDeviceAddresses[id], i2cDeviceCurrentLSB[id], amps);
}

bool I2CDevices_ReadPower(I2C_DEVICE_ID id, float *watts)
{
    if (!I2CDevices_IdIsValid(id) || (i2cDeviceKinds[id] != I2C_DEVICE_KIND_INA231A))
    {
        return false;
    }

    return INA231A_ReadPower(i2cDeviceAddresses[id], i2cDeviceCurrentLSB[id], watts);
}
