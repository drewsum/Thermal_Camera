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
#include "i2c/device_driver/ds1683.h"
#include "usb_uart/terminal_control.h"
#include "application/error_handler.h"

#include <stdio.h>

#define I2C_DEVICE_ADDRESS_ENTRY(name, kind, address, label, refdes)  (address),
static const uint16_t i2cDeviceAddresses[I2C_DEVICE_COUNT] =
{
    I2C_DEVICE_LIST(I2C_DEVICE_ADDRESS_ENTRY)
};

#define I2C_DEVICE_KIND_ENTRY(name, kind, address, label, refdes)  (kind),
static const I2C_DEVICE_KIND i2cDeviceKinds[I2C_DEVICE_COUNT] =
{
    I2C_DEVICE_LIST(I2C_DEVICE_KIND_ENTRY)
};

#define I2C_DEVICE_NAME_ENTRY(name, kind, address, label, refdes)  label,
static const char* const i2cDeviceNames[I2C_DEVICE_COUNT] =
{
    I2C_DEVICE_LIST(I2C_DEVICE_NAME_ENTRY)
};

#define I2C_DEVICE_REFDES_ENTRY(name, kind, address, label, refdes)  refdes,
static const char* const i2cDeviceRefdes[I2C_DEVICE_COUNT] =
{
    I2C_DEVICE_LIST(I2C_DEVICE_REFDES_ENTRY)
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

void I2CDevices_ReportI2CError(I2C_DEVICE_ID id)
{
    if (!I2CDevices_IdIsValid(id))
    {
        return;
    }

    ERROR_HANDLER_I2C_DEVICE_FLAG(id) = 1;
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

        case I2C_DEVICE_KIND_DS1683:
            return DS1683_Verify(i2cDeviceAddresses[id]);

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

        case I2C_DEVICE_KIND_DS1683:
            return true;

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

        case I2C_DEVICE_KIND_DS1683:
            DS1683_PrintStatus(i2cDeviceAddresses[id]);
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
        bool ok = I2CDevices_Verify((I2C_DEVICE_ID)id);

        i2cDevicePresent[id] = ok;

        // Only configure devices that are actually there -- e.g. writing an
        // INA231A calibration register to an address nothing ACKed is pointless.
        if (ok && !I2CDevices_ConfigureOne((I2C_DEVICE_ID)id))
        {
            ok = false;
        }

        // Record failure against this specific device's own I2C error flag
        // (generated from I2C_DEVICE_LIST -- see error_handler.h) rather than
        // a single flag shared by every I2C device on the board. Like every
        // other error_handler flag this only latches -- a device that inits
        // fine here does NOT clear a flag some earlier runtime read may have
        // set, since that flag is meant to survive warm resets.
        if (!ok)
        {
            I2CDevices_ReportI2CError((I2C_DEVICE_ID)id);
        }

        allPresent = allPresent && ok;
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

const char* I2CDevices_GetRefdes(I2C_DEVICE_ID id)
{
    if (!I2CDevices_IdIsValid(id))
    {
        return "Invalid Device ID";
    }

    return i2cDeviceRefdes[id];
}

void I2CDevices_PrintStatus(void)
{
    uint8_t id;

    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, BOLD_FONT);
    printf("    --- I2C Devices ---\n\r");

    for (id = 0; id < I2C_DEVICE_COUNT; id++)
    {
        terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, BOLD_FONT);
        printf("    %s (Refdes: %s)\n\r", i2cDeviceNames[id], i2cDeviceRefdes[id]);

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

    if (!MCP9804_ReadTemperature(i2cDeviceAddresses[id], celsius))
    {
        I2CDevices_ReportI2CError(id);
        return false;
    }

    return true;
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

    if (!reading->present)
    {
        I2CDevices_ReportI2CError(id);
    }

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

    if (!INA231A_ReadBusVoltage(i2cDeviceAddresses[id], volts))
    {
        I2CDevices_ReportI2CError(id);
        return false;
    }

    return true;
}

bool I2CDevices_ReadCurrent(I2C_DEVICE_ID id, float *amps)
{
    if (!I2CDevices_IdIsValid(id) || (i2cDeviceKinds[id] != I2C_DEVICE_KIND_INA231A))
    {
        return false;
    }

    if (!INA231A_ReadCurrent(i2cDeviceAddresses[id], i2cDeviceCurrentLSB[id], amps))
    {
        I2CDevices_ReportI2CError(id);
        return false;
    }

    return true;
}

bool I2CDevices_ReadPower(I2C_DEVICE_ID id, float *watts)
{
    if (!I2CDevices_IdIsValid(id) || (i2cDeviceKinds[id] != I2C_DEVICE_KIND_INA231A))
    {
        return false;
    }

    if (!INA231A_ReadPower(i2cDeviceAddresses[id], i2cDeviceCurrentLSB[id], watts))
    {
        I2CDevices_ReportI2CError(id);
        return false;
    }

    return true;
}

bool I2CDevices_ReadElapsedSeconds(I2C_DEVICE_ID id, uint32_t *seconds)
{
    if (!I2CDevices_IdIsValid(id) || (i2cDeviceKinds[id] != I2C_DEVICE_KIND_DS1683))
    {
        return false;
    }

    if (!DS1683_ReadElapsedSeconds(i2cDeviceAddresses[id], seconds))
    {
        I2CDevices_ReportI2CError(id);
        return false;
    }

    return true;
}

bool I2CDevices_ReadEventCount(I2C_DEVICE_ID id, uint16_t *count)
{
    if (!I2CDevices_IdIsValid(id) || (i2cDeviceKinds[id] != I2C_DEVICE_KIND_DS1683))
    {
        return false;
    }

    if (!DS1683_ReadEventCount(i2cDeviceAddresses[id], count))
    {
        I2CDevices_ReportI2CError(id);
        return false;
    }

    return true;
}

static bool I2CDevices_IdIsKind(I2C_DEVICE_ID id, I2C_DEVICE_KIND kind)
{
    return I2CDevices_IdIsValid(id) && (i2cDeviceKinds[id] == kind);
}

bool I2CDevices_QueueTemperatureRead(I2C_DEVICE_ID id, uint8_t raw[2],
                                     I2C_TRANSFER_CALLBACK callback, uintptr_t context)
{
    if (!I2CDevices_IdIsKind(id, I2C_DEVICE_KIND_MCP9804))
    {
        return false;
    }

    return MCP9804_QueueReadTemperature(i2cDeviceAddresses[id], raw, callback, context);
}

bool I2CDevices_QueueVoltageRead(I2C_DEVICE_ID id, uint8_t raw[2],
                                 I2C_TRANSFER_CALLBACK callback, uintptr_t context)
{
    if (!I2CDevices_IdIsKind(id, I2C_DEVICE_KIND_INA231A))
    {
        return false;
    }

    return INA231A_QueueReadBusVoltage(i2cDeviceAddresses[id], raw, callback, context);
}

bool I2CDevices_QueueCurrentRead(I2C_DEVICE_ID id, uint8_t raw[2],
                                 I2C_TRANSFER_CALLBACK callback, uintptr_t context)
{
    if (!I2CDevices_IdIsKind(id, I2C_DEVICE_KIND_INA231A))
    {
        return false;
    }

    return INA231A_QueueReadCurrent(i2cDeviceAddresses[id], raw, callback, context);
}

bool I2CDevices_QueuePowerRead(I2C_DEVICE_ID id, uint8_t raw[2],
                               I2C_TRANSFER_CALLBACK callback, uintptr_t context)
{
    if (!I2CDevices_IdIsKind(id, I2C_DEVICE_KIND_INA231A))
    {
        return false;
    }

    return INA231A_QueueReadPower(i2cDeviceAddresses[id], raw, callback, context);
}

bool I2CDevices_DecodeTemperature(I2C_DEVICE_ID id, const uint8_t raw[2],
                                  I2C_DEVICE_TEMP_READING *reading)
{
    if (!I2CDevices_IdIsKind(id, I2C_DEVICE_KIND_MCP9804))
    {
        return false;
    }

    reading->present = true;
    reading->celsius = MCP9804_DecodeTemperatureRaw(raw);
    MCP9804_DecodeAlertFlagsRaw(raw, &reading->alerts);
    return true;
}

bool I2CDevices_DecodeVoltage(I2C_DEVICE_ID id, const uint8_t raw[2], float *volts)
{
    if (!I2CDevices_IdIsKind(id, I2C_DEVICE_KIND_INA231A))
    {
        return false;
    }

    *volts = INA231A_DecodeBusVoltageRaw(raw);
    return true;
}

bool I2CDevices_DecodeCurrent(I2C_DEVICE_ID id, const uint8_t raw[2], float *amps)
{
    if (!I2CDevices_IdIsKind(id, I2C_DEVICE_KIND_INA231A))
    {
        return false;
    }

    *amps = INA231A_DecodeCurrentRaw(raw, i2cDeviceCurrentLSB[id]);
    return true;
}

bool I2CDevices_DecodePower(I2C_DEVICE_ID id, const uint8_t raw[2], float *watts)
{
    if (!I2CDevices_IdIsKind(id, I2C_DEVICE_KIND_INA231A))
    {
        return false;
    }

    *watts = INA231A_DecodePowerRaw(raw, i2cDeviceCurrentLSB[id]);
    return true;
}
