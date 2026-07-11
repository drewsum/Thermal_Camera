/*******************************************************************************
  Temperature Sensor Manager

  File Name:
    temperature_sensors.h

  Summary:
    Manages all MCP9804 I2C temperature sensors on the board.

  Description:
    TEMPERATURE_SENSOR_LIST below is the single source of truth for every
    physical sensor: the enum, address table, and name table are all
    generated from it (same X-macro idiom as ERROR_HANDLER_FLAG_LIST in
    error_handler.h). Add, remove, or rename a sensor by editing only that
    list.
*******************************************************************************/

#ifndef TEMPERATURE_SENSORS_H
#define TEMPERATURE_SENSORS_H

#include <stdint.h>
#include <stdbool.h>

#include "mcp9804.h"

#ifdef __cplusplus
extern "C" {
#endif

// TODO: replace these placeholder addresses/labels once each sensor's
// physical location and A2:A0 address-pin strapping is known. Valid
// addresses are MCP9804_BASE_ADDRESS .. MCP9804_BASE_ADDRESS + 7.
#define TEMPERATURE_SENSOR_LIST(X) \
    X(TEMP_SENSOR_1, 0x18, "POS12 Input Gate") \
    X(TEMP_SENSOR_2, 0x19, "POS3P0 PSU") \
    X(TEMP_SENSOR_3, 0x1A, "POS1P8 PSU") \
    X(TEMP_SENSOR_4, 0x1B, "POS2P8 PSU") \
    X(TEMP_SENSOR_5, 0x1C, "POS1P2 PSU") \
    X(TEMP_SENSOR_6, 0x1D, "Backlight PSU") \
    X(TEMP_SENSOR_7, 0x1F, "Ambient")

#define TEMPERATURE_SENSOR_ENUM(name, address, label)  name,
typedef enum
{
    TEMPERATURE_SENSOR_LIST(TEMPERATURE_SENSOR_ENUM)
    TEMP_SENSOR_COUNT
} TEMP_SENSOR_ID;

// Per-sensor snapshot returned by TemperatureSensors_ReadWithStatus()/ReadAll().
typedef struct
{
    bool present;                   // false if the sensor did not respond
    float celsius;                  // valid only if present
    MCP9804_ALERT_STATUS alerts;    // valid only if present
} TEMP_SENSOR_READING;

// Probes every sensor in TEMPERATURE_SENSOR_LIST (MCP9804_Verify) and
// records which ones responded. Returns true only if all of them did --
// check TemperatureSensors_IsPresent() per-sensor to find out which one(s)
// didn't.
bool TemperatureSensors_Initialize(void);

// Returns whether `id` responded during the last Initialize() call.
bool TemperatureSensors_IsPresent(TEMP_SENSOR_ID id);

// Returns the display label for `id` (from TEMPERATURE_SENSOR_LIST).
const char* TemperatureSensors_GetName(TEMP_SENSOR_ID id);

// Reads a single sensor's ambient temperature. Returns false on I2C error;
// call I2C_ErrorGet() for the reason.
bool TemperatureSensors_Read(TEMP_SENSOR_ID id, float *celsius);

// Reads a single sensor's temperature and alert flags together.
bool TemperatureSensors_ReadWithStatus(TEMP_SENSOR_ID id, TEMP_SENSOR_READING *reading);

// Reads every sensor into `readings[TEMP_SENSOR_ID]`. Returns the number
// that responded successfully.
uint8_t TemperatureSensors_ReadAll(TEMP_SENSOR_READING readings[TEMP_SENSOR_COUNT]);

// Prints a one-line-per-sensor status table (address, presence,
// temperature, alerts) to the terminal.
void TemperatureSensors_PrintStatus(void);

#ifdef __cplusplus
}
#endif

#endif /* TEMPERATURE_SENSORS_H */
