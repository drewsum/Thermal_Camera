/*******************************************************************************
  I2C Device Registry

  File Name:
    i2c_devices.c

  Summary:
    The single interface for every physical I2C device on the board:
    presence, address, name, register-level status printing, and
    kind-specific typed reads, all dispatched by device kind.
*******************************************************************************/

#include "i2c_devices.h"
#include "mcp9804.h"
#include "terminal_control.h"

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
