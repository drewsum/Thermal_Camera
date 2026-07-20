/*******************************************************************************
  I2C Device Registry

  File Name:
    i2c_devices.h

  Summary:
    The single interface for every physical I2C device on the board:
    presence, address, name, schematic refdes, register-level status
    printing, and kind-specific typed reads (e.g. temperature), all
    dispatched by device kind.

  Description:
    I2C_DEVICE_LIST below is the single source of truth for every physical
    device: the enum, address table, kind table, name table, and refdes
    table are all generated from it (same X-macro idiom as
    ERROR_HANDLER_FLAG_LIST in error_handler.h). Add, remove, or rename a
    device by editing only that list.

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
#include "i2c/device_driver/ds1683.h"
#include "i2c/device_driver/gt911.h"
#include "i2c/device_driver/bq27441.h"

#ifdef __cplusplus
extern "C" {
#endif

// Every device driver this board knows how to identify/print. Add an entry
// here when a new device type is brought up.
typedef enum
{
    I2C_DEVICE_KIND_MCP9804,   // temperature sensor
    I2C_DEVICE_KIND_INA231A,   // current/power monitor
    I2C_DEVICE_KIND_DS1683,    // total-elapsed-time and event recorder
    I2C_DEVICE_KIND_GT911,     // LCD capacitive touch controller
    I2C_DEVICE_KIND_BQ27441,   // Li-Ion fuel gauge
} I2C_DEVICE_KIND;

// TODO: addresses/labels below for the 6x INA231A power monitors are
// placeholders -- fill in real addresses (A1:A0 strapping) and labels.
// TODO: refdes column below is all placeholder text -- fill in real
// schematic reference designators (e.g. "U42").
#warning "POS2P8 PSU Power Monitor is not present on this board, so its I2C address and label are commented out in i2c_devices.h"
#define I2C_DEVICE_LIST(X) \
    X(I2C_DEV_TEMP_1, I2C_DEVICE_KIND_MCP9804, 0x18, "POS12 Input Gate Temp Sensor", "U302") \
    X(I2C_DEV_TEMP_2, I2C_DEVICE_KIND_MCP9804, 0x19, "POS3P0 PSU Temp Sensor", "U502") \
    X(I2C_DEV_TEMP_3, I2C_DEVICE_KIND_MCP9804, 0x1A, "POS1P8 PSU Temp Sensor", "U702") \
    X(I2C_DEV_TEMP_4, I2C_DEVICE_KIND_MCP9804, 0x1B, "POS2P8 PSU Temp Sensor", "U902") \
    X(I2C_DEV_TEMP_5, I2C_DEVICE_KIND_MCP9804, 0x1C, "POS1P2 PSU Temp Sensor", "U1102") \
    X(I2C_DEV_TEMP_6, I2C_DEVICE_KIND_MCP9804, 0x1D, "Backlight PSU Temp Sensor", "U1302") \
    X(I2C_DEV_TEMP_7, I2C_DEVICE_KIND_MCP9804, 0x1F, "Ambient Temp Sensor", "U2302") \
    X(I2C_DEV_PWR_1, I2C_DEVICE_KIND_INA231A, 0x40, "POS12 Input Gate Power Monitor", "U301") \
    X(I2C_DEV_PWR_2, I2C_DEVICE_KIND_INA231A, 0x41, "POS3P0 PSU Power Monitor", "U501") \
    X(I2C_DEV_PWR_3, I2C_DEVICE_KIND_INA231A, 0x42, "POS1P8 PSU Power Monitor", "U701") \
    X(I2C_DEV_PWR_5, I2C_DEVICE_KIND_INA231A, 0x44, "POS1P2 PSU Power Monitor", "U1101") \
    X(I2C_DEV_PWR_6, I2C_DEVICE_KIND_INA231A, 0x45, "Backlight PSU Power Monitor", "U1301") \
    X(I2C_DEV_ETR_1, I2C_DEVICE_KIND_DS1683, 0x6B, "System Elapsed Time Recorder", "U2301") \
    X(I2C_DEV_BATT_1, I2C_DEVICE_KIND_BQ27441, 0x55, "Li-Ion Fuel Gauge", "U1401")
    //X(I2C_DEV_CTP_1, I2C_DEVICE_KIND_GT911, GT911_ADDRESS, "LCD Touch Panel Controller", "N2101")

#define I2C_DEVICE_ENUM(name, kind, address, label, refdes)  name,
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

// Returns the schematic reference designator for `id` (from
// I2C_DEVICE_LIST). Currently placeholder text for every device -- see the
// TODO on I2C_DEVICE_LIST.
const char* I2CDevices_GetRefdes(I2C_DEVICE_ID id);

// Latches `id`'s error_handler.flags.<I2C_DEV_...>_i2c_error flag (see
// error_handler.h). Called internally by I2CDevices_Initialize() and every
// blocking Read* function below on failure. Callers driving the queued
// Read/Decode API further down must call this themselves when a queued read
// comes back failed, since i2c_devices.c never observes that completion --
// see updateTemperatureTelemetry()/telemetryTasks() in telemetry.c for the
// pattern. Safe to call from I2C interrupt context (integer-only). No-op if
// `id` is out of range.
void I2CDevices_ReportI2CError(I2C_DEVICE_ID id);

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

// Reads `id`'s accumulated elapsed time in whole seconds. Returns false if
// `id` isn't an I2C_DEVICE_KIND_DS1683 device, or on I2C error (call
// I2C_ErrorGet() for the reason).
bool I2CDevices_ReadElapsedSeconds(I2C_DEVICE_ID id, uint32_t *seconds);

// Reads `id`'s event count (number of falling edges seen on its EVENT pin).
// Same kind requirement as I2CDevices_ReadElapsedSeconds().
bool I2CDevices_ReadEventCount(I2C_DEVICE_ID id, uint16_t *count);

// --- Queued (non-blocking) reads ------------------------------------------
// Each call queues one register read for `id` and returns immediately;
// false means the device kind doesn't match, or the I2C queue was full
// (nothing queued). `callback` fires from I2C interrupt context when the
// read completes; raw[2] (MSB first) must stay valid until then. Decode the
// bytes afterwards -- from thread context, since decoding does float math --
// with the matching Decode function, which also returns false on a kind
// mismatch.

bool I2CDevices_QueueTemperatureRead(I2C_DEVICE_ID id, uint8_t raw[2],
                                     I2C_TRANSFER_CALLBACK callback, uintptr_t context);
bool I2CDevices_QueueVoltageRead(I2C_DEVICE_ID id, uint8_t raw[2],
                                 I2C_TRANSFER_CALLBACK callback, uintptr_t context);
bool I2CDevices_QueueCurrentRead(I2C_DEVICE_ID id, uint8_t raw[2],
                                 I2C_TRANSFER_CALLBACK callback, uintptr_t context);
bool I2CDevices_QueuePowerRead(I2C_DEVICE_ID id, uint8_t raw[2],
                               I2C_TRANSFER_CALLBACK callback, uintptr_t context);

bool I2CDevices_DecodeTemperature(I2C_DEVICE_ID id, const uint8_t raw[2],
                                  I2C_DEVICE_TEMP_READING *reading);
bool I2CDevices_DecodeVoltage(I2C_DEVICE_ID id, const uint8_t raw[2], float *volts);
bool I2CDevices_DecodeCurrent(I2C_DEVICE_ID id, const uint8_t raw[2], float *amps);
bool I2CDevices_DecodePower(I2C_DEVICE_ID id, const uint8_t raw[2], float *watts);

// --- BQ27441 fuel gauge accessors -----------------------------------------
// Separate from the INA231A-only I2CDevices_Read{Voltage,Current,Power}()
// above (different device kind, different scaling/units) -- do not reuse
// those for the fuel gauge.

// Per-device battery flag snapshot, valid only for I2C_DEVICE_KIND_BQ27441
// devices. Mirrors BQ27441_FLAG_STATUS (bq27441.h) minus the
// diagnostic-only configUpdateMode field.
typedef struct
{
    bool overTemperature;
    bool underTemperature;
    bool fullyCharged;
    bool fastChargingAllowed;
    bool dischargeDetected;
    bool lowStateOfCharge;
} I2C_DEVICE_BATTERY_FLAGS;

// Blocking reads. Each returns false if `id` isn't an
// I2C_DEVICE_KIND_BQ27441 device, or on I2C error (call I2C_ErrorGet() for
// the reason).
bool I2CDevices_ReadBatteryVoltage(I2C_DEVICE_ID id, float *volts);
bool I2CDevices_ReadBatteryCurrent(I2C_DEVICE_ID id, float *amps);
bool I2CDevices_ReadBatteryTemperature(I2C_DEVICE_ID id, float *celsius);
bool I2CDevices_ReadBatteryStateOfCharge(I2C_DEVICE_ID id, uint8_t *percent);
bool I2CDevices_ReadBatteryStateOfHealth(I2C_DEVICE_ID id, uint8_t *percent);
bool I2CDevices_ReadBatteryRemainingCapacity(I2C_DEVICE_ID id, float *milliamphours);
bool I2CDevices_ReadBatteryFullChargeCapacity(I2C_DEVICE_ID id, float *milliamphours);
bool I2CDevices_ReadBatteryFlags(I2C_DEVICE_ID id, I2C_DEVICE_BATTERY_FLAGS *flags);

// Queued (non-blocking) reads -- same shape/contract as the queued reads
// further up this header.
bool I2CDevices_QueueBatteryVoltageRead(I2C_DEVICE_ID id, uint8_t raw[2],
                                        I2C_TRANSFER_CALLBACK callback, uintptr_t context);
bool I2CDevices_QueueBatteryCurrentRead(I2C_DEVICE_ID id, uint8_t raw[2],
                                        I2C_TRANSFER_CALLBACK callback, uintptr_t context);
bool I2CDevices_QueueBatteryTemperatureRead(I2C_DEVICE_ID id, uint8_t raw[2],
                                            I2C_TRANSFER_CALLBACK callback, uintptr_t context);
bool I2CDevices_QueueBatteryStateOfChargeRead(I2C_DEVICE_ID id, uint8_t raw[2],
                                              I2C_TRANSFER_CALLBACK callback, uintptr_t context);
bool I2CDevices_QueueBatteryStateOfHealthRead(I2C_DEVICE_ID id, uint8_t raw[2],
                                              I2C_TRANSFER_CALLBACK callback, uintptr_t context);
bool I2CDevices_QueueBatteryRemainingCapacityRead(I2C_DEVICE_ID id, uint8_t raw[2],
                                                  I2C_TRANSFER_CALLBACK callback, uintptr_t context);
bool I2CDevices_QueueBatteryFullChargeCapacityRead(I2C_DEVICE_ID id, uint8_t raw[2],
                                                   I2C_TRANSFER_CALLBACK callback, uintptr_t context);
bool I2CDevices_QueueBatteryFlagsRead(I2C_DEVICE_ID id, uint8_t raw[2],
                                     I2C_TRANSFER_CALLBACK callback, uintptr_t context);

bool I2CDevices_DecodeBatteryVoltage(I2C_DEVICE_ID id, const uint8_t raw[2], float *volts);
bool I2CDevices_DecodeBatteryCurrent(I2C_DEVICE_ID id, const uint8_t raw[2], float *amps);
bool I2CDevices_DecodeBatteryTemperature(I2C_DEVICE_ID id, const uint8_t raw[2], float *celsius);
bool I2CDevices_DecodeBatteryStateOfCharge(I2C_DEVICE_ID id, const uint8_t raw[2], uint8_t *percent);
bool I2CDevices_DecodeBatteryStateOfHealth(I2C_DEVICE_ID id, const uint8_t raw[2], uint8_t *percent);
bool I2CDevices_DecodeBatteryRemainingCapacity(I2C_DEVICE_ID id, const uint8_t raw[2], float *milliamphours);
bool I2CDevices_DecodeBatteryFullChargeCapacity(I2C_DEVICE_ID id, const uint8_t raw[2], float *milliamphours);
bool I2CDevices_DecodeBatteryFlags(I2C_DEVICE_ID id, const uint8_t raw[2], I2C_DEVICE_BATTERY_FLAGS *flags);

#ifdef __cplusplus
}
#endif

#endif /* I2C_DEVICES_H */
