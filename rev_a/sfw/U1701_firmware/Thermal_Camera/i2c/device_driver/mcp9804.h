/*******************************************************************************
  MCP9804 I2C Temperature Sensor Driver

  File Name:
    mcp9804.h

  Summary:
    Driver for the Microchip MCP9804 (and register-compatible MCP9805/9808
    family) I2C digital temperature sensor, built on i2c_master.h.

  Description:
    There is no per-device "instance" state here -- every function takes the
    device's 7-bit I2C address explicitly, so the same code drives any
    number of MCP9804s on the bus (they're distinguished by their A2:A0
    address pins, giving addresses 0x18-0x1F).
*******************************************************************************/

#ifndef MCP9804_H
#define MCP9804_H

#include <stdint.h>
#include <stdbool.h>

#include "i2c/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

// 7-bit I2C address is 0b0011,A2,A1,A0 -- A2:A0 are set by the device's
// address pins, giving a range of MCP9804_BASE_ADDRESS to +7.
#define MCP9804_BASE_ADDRESS   0x18u

// Resolution register (ADC resolution used for the T_A conversion).
typedef enum
{
    MCP9804_RESOLUTION_0_5C    = 0,  // 0.5 C,    tCONV = 30ms
    MCP9804_RESOLUTION_0_25C   = 1,  // 0.25 C,   tCONV = 65ms
    MCP9804_RESOLUTION_0_125C  = 2,  // 0.125 C,  tCONV = 130ms
    MCP9804_RESOLUTION_0_0625C = 3,  // 0.0625 C, tCONV = 250ms (power-up default)
} MCP9804_RESOLUTION;

// Ambient-temperature-vs-limit alert flags, latched alongside every T_A read.
typedef struct
{
    bool aboveCritical;   // TA >= T_CRIT
    bool aboveUpper;      // TA > T_UPPER
    bool belowLower;      // TA < T_LOWER
} MCP9804_ALERT_STATUS;

// Confirms a device at `address` responds and identifies as an MCP9804
// family part (Manufacturer ID 0x0054, Device ID upper byte 0x02).
bool MCP9804_Verify(uint16_t address);

// Reads ambient temperature in degrees Celsius. Returns false (and leaves
// *celsius unmodified) on any I2C error; call I2C_ErrorGet() for the reason.
bool MCP9804_ReadTemperature(uint16_t address, float *celsius);

// Reads ambient temperature and the three limit-comparison alert flags together.
bool MCP9804_ReadTemperatureAndStatus(uint16_t address, float *celsius, MCP9804_ALERT_STATUS *status);

// Everything MCP9804_PrintStatus() shows, as data: the identity registers,
// the configuration and resolution words, the three temperature limits, and
// the live reading. Exists so the GUI's I2C status screen can show the same
// diagnostics the console does without either one re-deriving the register
// map -- MCP9804_PrintStatus() is written on top of this.
//
// A field is only meaningful when its `*Valid` companion is true: the
// identity read is the gate for the whole struct (false there means the part
// did not answer at all), and each later register is reported separately so
// one unreadable register does not discard the rest.
typedef struct
{
    bool identityValid;      // false = no response; nothing else is filled in
    bool identified;         // manufacturer and device ID both as expected
    uint16_t manufacturerId;
    uint16_t deviceId;       // device ID in the high byte, revision in the low

    bool configValid;
    uint16_t config;

    bool resolutionValid;
    uint16_t resolution;

    bool upperValid, lowerValid, criticalValid;
    float upperLimit, lowerLimit, criticalLimit;   // degrees Celsius

    bool temperatureValid;
    float celsius;
    MCP9804_ALERT_STATUS alerts;
} MCP9804_DIAGNOSTICS;

// Fills `out` with the above. Returns identityValid, i.e. whether the device
// answered at all. Read-only: touches no configuration.
bool MCP9804_ReadDiagnostics(uint16_t address, MCP9804_DIAGNOSTICS *out);

// Names for the two multi-bit fields in the registers above. Pure functions
// of the register value -- no bus access.
const char* MCP9804_HysteresisName(uint16_t config);
const char* MCP9804_ResolutionName(uint16_t resolution);

// Queues a non-blocking read of the raw T_A register into raw[2] (MSB
// first) and returns immediately; `callback` fires from I2C interrupt
// context on completion. `raw` must stay valid until then. Decode the bytes
// afterwards (from thread context) with the helpers below.
bool MCP9804_QueueReadTemperature(uint16_t address, uint8_t raw[2],
                                  I2C_TRANSFER_CALLBACK callback, uintptr_t context);

// Converts a raw T_A register image (as filled in by
// MCP9804_QueueReadTemperature()) to degrees Celsius / alert flags.
float MCP9804_DecodeTemperatureRaw(const uint8_t raw[2]);
void MCP9804_DecodeAlertFlagsRaw(const uint8_t raw[2], MCP9804_ALERT_STATUS *status);

// Sets the ADC resolution used for the T_A conversion.
bool MCP9804_SetResolution(uint16_t address, MCP9804_RESOLUTION resolution);

// Sets the T_UPPER / T_LOWER / T_CRIT alert limits, in degrees Celsius.
// Values are clamped to the device's representable range ([-256, 255.9375]).
bool MCP9804_SetUpperLimit(uint16_t address, float celsius);
bool MCP9804_SetLowerLimit(uint16_t address, float celsius);
bool MCP9804_SetCriticalLimit(uint16_t address, float celsius);

// Enables/disables the device's low-power shutdown mode. Register contents
// (limits, resolution, etc.) are preserved; T_A reads return the last
// conversion result rather than updating.
bool MCP9804_SetShutdown(uint16_t address, bool shutdown);

// Prints the device's identification, configuration, limits, and current
// temperature/alert status to the terminal.
void MCP9804_PrintStatus(uint16_t address);

#ifdef __cplusplus
}
#endif

#endif /* MCP9804_H */
