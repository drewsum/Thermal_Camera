/*******************************************************************************
  INA231A I2C Current/Power Monitor Driver

  File Name:
    ina231a.h

  Summary:
    Driver for the Texas Instruments INA231A bidirectional current-shunt and
    power monitor, built on i2c_master.h.

  Description:
    Unlike MCP9804, the INA231A has no documented manufacturer/device ID
    register, so INA231A_Verify() can only check the Configuration register
    against its power-on-reset default -- see the caveat on that function in
    ina231a.c.

    The Current and Power registers read as zero until the Calibration
    register is programmed (INA231A_Configure()) using the shunt resistor
    value and expected max current for that specific rail -- both are
    board-specific and must be supplied by the caller.
*******************************************************************************/

#ifndef INA231A_H
#define INA231A_H

#include <stdint.h>
#include <stdbool.h>

#include "i2c/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

// 7-bit I2C address is set by the A1/A0 address pins; see the INA231A
// datasheet's address table for the full 16-address range.
#define INA231A_BASE_ADDRESS   0x40u

// Per-device measurement snapshot returned by INA231A_ReadAll().
typedef struct
{
    float busVoltage;      // volts
    float shuntVoltage;    // volts
    float current;         // amps -- valid only once the device has been INA231A_Configure()'d
    float power;           // watts -- valid only once the device has been INA231A_Configure()'d
} INA231A_READING;

// Confirms a device at `address` responds and its Configuration register
// matches the documented power-on-reset value (0x4127). The INA231A has no
// manufacturer/device ID register (unlike MCP9804), so this is a weaker
// check -- see ina231a.c for the caveat.
bool INA231A_Verify(uint16_t address);

// Computes and writes the Calibration register for a shunt of
// `shuntResistanceOhms` sized so the Current register doesn't clip at
// `maxExpectedCurrentAmps`. Must be called once before ReadCurrent()/
// ReadPower()/ReadAll() return meaningful values. Returns the resulting
// Current_LSB (amps/bit) via `currentLSBOut` -- the caller must hold onto
// it and pass it back into the Current/Power reads below.
bool INA231A_Configure(uint16_t address, float shuntResistanceOhms,
                        float maxExpectedCurrentAmps, float *currentLSBOut);

// Reads bus voltage in volts. Does not require INA231A_Configure().
bool INA231A_ReadBusVoltage(uint16_t address, float *volts);

// Reads the shunt voltage drop in volts (signed). Does not require
// INA231A_Configure().
bool INA231A_ReadShuntVoltage(uint16_t address, float *volts);

// Reads current in amps (signed). Requires INA231A_Configure() first;
// `currentLSB` is the value it returned.
bool INA231A_ReadCurrent(uint16_t address, float currentLSB, float *amps);

// Reads power in watts. Requires INA231A_Configure() first; `currentLSB` is
// the value it returned.
bool INA231A_ReadPower(uint16_t address, float currentLSB, float *watts);

// Reads bus voltage, shunt voltage, current, and power together. Requires
// INA231A_Configure() first for the current/power fields to be meaningful.
bool INA231A_ReadAll(uint16_t address, float currentLSB, INA231A_READING *reading);

// Queue a non-blocking read of the raw Bus Voltage / Current / Power
// register into raw[2] (MSB first) and return immediately; `callback` fires
// from I2C interrupt context on completion. `raw` must stay valid until
// then. Decode the bytes afterwards (from thread context) with the helpers
// below.
bool INA231A_QueueReadBusVoltage(uint16_t address, uint8_t raw[2],
                                 I2C_TRANSFER_CALLBACK callback, uintptr_t context);
bool INA231A_QueueReadCurrent(uint16_t address, uint8_t raw[2],
                              I2C_TRANSFER_CALLBACK callback, uintptr_t context);
bool INA231A_QueueReadPower(uint16_t address, uint8_t raw[2],
                            I2C_TRANSFER_CALLBACK callback, uintptr_t context);

// Convert raw register images (as filled in by the QueueRead functions
// above) to engineering units. `currentLSB` is the value returned by
// INA231A_Configure() for that device.
float INA231A_DecodeBusVoltageRaw(const uint8_t raw[2]);
float INA231A_DecodeCurrentRaw(const uint8_t raw[2], float currentLSB);
float INA231A_DecodePowerRaw(const uint8_t raw[2], float currentLSB);

// Prints the device's configuration/calibration registers and measurements
// to the terminal. Current/Power are shown as raw register codes (not
// converted to amps/watts) since this call has no per-device Current_LSB
// (from INA231A_Configure()) to convert them with.
// Puts the monitor into power-down (MODE = 0) or back into its normal
// shunt+bus continuous mode. Power-down stops conversions and drops the
// part to a few uA while leaving the I2C interface and the Calibration
// register intact, so resuming does not require INA231A_Configure() again.
// Used by enterLowPowerSleep() (application/power_saving.c) via
// I2CDevices_EnterLowPower().
bool INA231A_SetPowerDown(uint16_t address, bool powerDown);

void INA231A_PrintStatus(uint16_t address);

#ifdef __cplusplus
}
#endif

#endif /* INA231A_H */
