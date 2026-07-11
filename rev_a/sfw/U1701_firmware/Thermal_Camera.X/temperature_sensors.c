/*******************************************************************************
  Temperature Sensor Manager

  File Name:
    temperature_sensors.c

  Summary:
    Manages all MCP9804 I2C temperature sensors on the board.
*******************************************************************************/

#include "temperature_sensors.h"
#include "terminal_control.h"

#include <stdio.h>

#define TEMPERATURE_SENSOR_ADDRESS_ENTRY(name, address, label)  (address),
static const uint16_t temperatureSensorAddresses[TEMP_SENSOR_COUNT] =
{
    TEMPERATURE_SENSOR_LIST(TEMPERATURE_SENSOR_ADDRESS_ENTRY)
};

#define TEMPERATURE_SENSOR_NAME_ENTRY(name, address, label)  label,
static const char* const temperatureSensorNames[TEMP_SENSOR_COUNT] =
{
    TEMPERATURE_SENSOR_LIST(TEMPERATURE_SENSOR_NAME_ENTRY)
};

static bool temperatureSensorPresent[TEMP_SENSOR_COUNT];

static bool TemperatureSensors_IdIsValid(TEMP_SENSOR_ID id)
{
    return ((unsigned)id < (unsigned)TEMP_SENSOR_COUNT);
}

bool TemperatureSensors_Initialize(void)
{
    bool allPresent = true;
    uint8_t id;

    for (id = 0; id < TEMP_SENSOR_COUNT; id++)
    {
        temperatureSensorPresent[id] = MCP9804_Verify(temperatureSensorAddresses[id]);
        allPresent = allPresent && temperatureSensorPresent[id];
    }

    return allPresent;
}

bool TemperatureSensors_IsPresent(TEMP_SENSOR_ID id)
{
    if (!TemperatureSensors_IdIsValid(id))
    {
        return false;
    }

    return temperatureSensorPresent[id];
}

const char* TemperatureSensors_GetName(TEMP_SENSOR_ID id)
{
    if (!TemperatureSensors_IdIsValid(id))
    {
        return "Invalid Sensor ID";
    }

    return temperatureSensorNames[id];
}

bool TemperatureSensors_Read(TEMP_SENSOR_ID id, float *celsius)
{
    if (!TemperatureSensors_IdIsValid(id))
    {
        return false;
    }

    return MCP9804_ReadTemperature(temperatureSensorAddresses[id], celsius);
}

bool TemperatureSensors_ReadWithStatus(TEMP_SENSOR_ID id, TEMP_SENSOR_READING *reading)
{
    if (!TemperatureSensors_IdIsValid(id))
    {
        reading->present = false;
        return false;
    }

    reading->present = MCP9804_ReadTemperatureAndStatus(temperatureSensorAddresses[id],
                                                          &reading->celsius, &reading->alerts);

    return reading->present;
}

uint8_t TemperatureSensors_ReadAll(TEMP_SENSOR_READING readings[TEMP_SENSOR_COUNT])
{
    uint8_t successCount = 0;
    uint8_t id;

    for (id = 0; id < TEMP_SENSOR_COUNT; id++)
    {
        if (TemperatureSensors_ReadWithStatus((TEMP_SENSOR_ID)id, &readings[id]))
        {
            successCount++;
        }
    }

    return successCount;
}

void TemperatureSensors_PrintStatus(void)
{
    uint8_t id;

    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, BOLD_FONT);
    printf("    --- Temperature Sensors ---\n\r");

    for (id = 0; id < TEMP_SENSOR_COUNT; id++)
    {
        TEMP_SENSOR_READING reading;
        bool ok = TemperatureSensors_ReadWithStatus((TEMP_SENSOR_ID)id, &reading);

        terminalTextAttributes(ok ? GREEN_COLOR : RED_COLOR, BLACK_COLOR, NORMAL_FONT);

        if (ok)
        {
            bool anyAlert = reading.alerts.aboveCritical || reading.alerts.aboveUpper || reading.alerts.belowLower;

            printf("    [0x%02X] %-24s %8.4f C%s\n\r",
                   temperatureSensorAddresses[id], temperatureSensorNames[id], reading.celsius,
                   anyAlert ? "  (ALERT)" : "");
        }
        else
        {
            printf("    [0x%02X] %-24s NOT RESPONDING\n\r",
                   temperatureSensorAddresses[id], temperatureSensorNames[id]);
        }
    }

    terminalTextAttributesReset();
}
