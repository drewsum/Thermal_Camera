/*******************************************************************************
  I2C Device Registry

  File Name:
    i2c_devices.h

  Summary:
    The single interface for every physical I2C device on the board:
    presence, address, name, register-level status printing, and
    kind-specific typed reads (e.g. temperature), all dispatched by device
    kind.

  Description:
    I2C_DEVICE_LIST below is the single source of truth for every physical
    device: the enum, address table, kind table, and name table are all
    generated from it (same X-macro idiom as ERROR_HANDLER_FLAG_LIST in
    error_handler.h). Add, remove, or rename a device by editing only that
    list.

    Bringing up a new device kind (e.g. a power monitor) means adding a
    driver file (mirroring mcp9804.c/h), a case to the I2CDevices_Verify()/
    PrintOne() dispatch switches in i2c_devices.c, and whatever typed
    accessors that kind needs (mirroring I2CDevices_ReadTemperature() below).
*******************************************************************************/

#ifndef I2C_DEVICES_H
#define I2C_DEVICES_H

#include <stdint.h>
#include <stdbool.h>

#include "i2c/device_driver/mcp9804.h"
#include "i2c/device_driver/ina231a.h"

#ifdef __cplusplus
extern "C" {
#endif

// Every device driver this board knows how to identify/print. Add an entry
// here when a new device type is brought up.
typedef enum
{
    I2C_DEVICE_KIND_MCP9804,   // temperature sensor
    I2C_DEVICE_KIND_INA231A,   // current/power monitor
} I2C_DEVICE_KIND;

// TODO: addresses/labels below for the 6x INA231A power monitors are
// placeholders -- fill in real addresses (A1:A0 strapping) and labels.
#define I2C_DEVICE_LIST(X) \
    X(I2C_DEV_TEMP_1, I2C_DEVICE_KIND_MCP9804, 0x18, "POS12 Input Gate Temp Sensor") \
    X(I2C_DEV_TEMP_2, I2C_DEVICE_KIND_MCP9804, 0x19, "POS3P0 PSU Temp Sensor") \
    X(I2C_DEV_TEMP_3, I2C_DEVICE_KIND_MCP9804, 0x1A, "POS1P8 PSU Temp Sensor") \
    X(I2C_DEV_TEMP_4, I2C_DEVICE_KIND_MCP9804, 0x1B, "POS2P8 PSU Temp Sensor") \
    X(I2C_DEV_TEMP_5, I2C_DEVICE_KIND_MCP9804, 0x1C, "POS1P2 PSU Temp Sensor") \
    X(I2C_DEV_TEMP_6, I2C_DEVICE_KIND_MCP9804, 0x1D, "Backlight PSU Temp Sensor") \
    X(I2C_DEV_TEMP_7, I2C_DEVICE_KIND_MCP9804, 0x1F, "Ambient Temp Sensor") \
    X(I2C_DEV_PWR_1, I2C_DEVICE_KIND_INA231A, 0x40, "POS12 Input Gate Power Monitor") \
    X(I2C_DEV_PWR_2, I2C_DEVICE_KIND_INA231A, 0x41, "POS3P0 PSU Power Monitor") \
    X(I2C_DEV_PWR_3, I2C_DEVICE_KIND_INA231A, 0x42, "POS1P8 PSU Power Monitor") \
    X(I2C_DEV_PWR_4, I2C_DEVICE_KIND_INA231A, 0x43, "POS2P8 PSU Power Monitor") \
    X(I2C_DEV_PWR_5, I2C_DEVICE_KIND_INA231A, 0x44, "POS1P2 PSU Power Monitor") \
    X(I2C_DEV_PWR_6, I2C_DEVICE_KIND_INA231A, 0x45, "Backlight PSU Power Monitor")

#define I2C_DEVICE_ENUM(name, kind, address, label)  name,
typedef enum
{
    I2C_DEVICE_LIST(I2C_DEVICE_ENUM)
    I2C_DEVICE_COUNT
} I2C_DEVICE_ID;

// Per-device temperature snapshot, valid only for I2C_DEVICE_KIND_MCP9804
// devices. Returned by I2CDevices_ReadTemperatureWithStatus()/ReadAllTemperatures().
typedef struct
{
    bool present;                   // false if the device did not respond
    float celsius;                  // valid only if present
    MCP9804_ALERT_STATUS alerts;    // valid only if present
} I2C_DEVICE_TEMP_READING;

// Probes every device in I2C_DEVICE_LIST (dispatched by kind) and records
// which ones responded/identified correctly. Returns true only if all of
// them did -- check I2CDevices_IsPresent() per-device to find out which
// one(s) didn't.
bool I2CDevices_Initialize(void);

// Returns whether `id` responded during the last Initialize() call.
bool I2CDevices_IsPresent(I2C_DEVICE_ID id);

// Returns the display label for `id` (from I2C_DEVICE_LIST).
const char* I2CDevices_GetName(I2C_DEVICE_ID id);

// Returns the 7-bit I2C address for `id` (from I2C_DEVICE_LIST).
uint16_t I2CDevices_GetAddress(I2C_DEVICE_ID id);

// Prints every device's full register status to the terminal, dispatched
// by kind (e.g. MCP9804_PrintStatus() for temperature sensors).
void I2CDevices_PrintStatus(void);

// Reads `id`'s ambient temperature in degrees Celsius. Returns false if
// `id` isn't an I2C_DEVICE_KIND_MCP9804 device, or on I2C error (call
// I2C_ErrorGet() for the reason).
bool I2CDevices_ReadTemperature(I2C_DEVICE_ID id, float *celsius);

// Reads `id`'s temperature and alert flags together.
bool I2CDevices_ReadTemperatureWithStatus(I2C_DEVICE_ID id, I2C_DEVICE_TEMP_READING *reading);

// Reads every I2C_DEVICE_KIND_MCP9804 device into `readings[I2C_DEVICE_ID]`;
// entries for other device kinds are left with `.present = false`. Returns
// the number that responded successfully.
uint8_t I2CDevices_ReadAllTemperatures(I2C_DEVICE_TEMP_READING readings[I2C_DEVICE_COUNT]);

// Reads `id`'s bus voltage in volts. Returns false if `id` isn't an
// I2C_DEVICE_KIND_INA231A device, or on I2C error (call I2C_ErrorGet() for
// the reason).
bool I2CDevices_ReadVoltage(I2C_DEVICE_ID id, float *volts);

// Reads `id`'s current in amps. Requires `id` to have been successfully
// calibrated during I2CDevices_Initialize() (see I2CDevices_ConfigureOne()
// in i2c_devices.c); returns false otherwise, if `id` isn't an
// I2C_DEVICE_KIND_INA231A device, or on I2C error.
bool I2CDevices_ReadCurrent(I2C_DEVICE_ID id, float *amps);

// Reads `id`'s power in watts. Same calibration requirement as
// I2CDevices_ReadCurrent().
bool I2CDevices_ReadPower(I2C_DEVICE_ID id, float *watts);

#ifdef __cplusplus
}
#endif

#endif /* I2C_DEVICES_H */
