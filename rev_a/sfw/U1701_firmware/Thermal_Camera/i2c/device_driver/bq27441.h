/*******************************************************************************
  BQ27441-G1 I2C Li-Ion Fuel Gauge Driver

  File Name:
    bq27441.h

  Summary:
    Driver for the Texas Instruments BQ27441-G1 single-cell Li-Ion
    Impedance Track fuel gauge, built on i2c_master.h.

  Description:
    There is no per-device "instance" state here -- every function takes the
    device's 7-bit I2C address explicitly, matching MCP9804/INA231A. The
    BQ27441 has a fixed address (0x55, no address-strapping pins), so this
    board only ever has one.

    Unlike MCP9804/INA231A (big-endian/SMBus-style 16-bit registers), the
    BQ27441's standard-command registers are LITTLE-ENDIAN (LSB at the
    given register address, MSB at address+1) -- every raw[2]/Decode*Raw()
    helper here uses the opposite byte order from those two drivers.

    This board wires the BQ27441's BIN pin to an on-board NTC thermistor
    (external temperature sense) and its GPOUT pin to BATT_LOWBATT_PIN
    (gpio/pin_macros.h) as a hardware low-battery indicator. Both of those
    behaviors are OpConfig-defined and are configured once by
    BQ27441_ConfigureOpConfig() -- see the VERIFY comments in bq27441.c
    next to BQ27441_OPCONFIG_* for the data-memory details that must be
    checked against the TI TRM (SLUUAC9) before this is relied on in the
    field. Battery-insertion detection (Flags().BAT_DET) is deliberately
    NOT enabled/used here -- the BIN divider is a fixed board component
    that doesn't disconnect when the cell is removed from its 2-contact
    holder, so BAT_DET can't distinguish "installed" from "not installed"
    on this board. Boot-time presence is instead decided by a
    voltage-threshold heuristic in main.c.
*******************************************************************************/

#ifndef BQ27441_H
#define BQ27441_H

#include <stdint.h>
#include <stdbool.h>

#include "i2c/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

// Fixed 7-bit I2C address -- no address-strapping pins on this part.
#define BQ27441_ADDRESS   0x55u

// Flags() bits surfaced by this driver (see BQ27441_FLAG_STATUS below).
// BAT_DET is intentionally not exposed -- see the header comment above.
typedef struct
{
    bool overTemperature;      // Flags().OT
    bool underTemperature;     // Flags().UT
    bool fullyCharged;         // Flags().FC
    bool fastChargingAllowed;  // Flags().CHG
    bool dischargeDetected;    // Flags().DSG
    bool lowStateOfCharge;     // Flags().SOCF -- final low-SOC discharge warning
    bool configUpdateMode;     // Flags().CFGUPMODE -- diagnostic only, should read false in normal operation
} BQ27441_FLAG_STATUS;

// Confirms a device at `address` responds and identifies as a BQ27441
// (Control() DEVICE_TYPE subcommand returns 0x0421). Mirrors
// MCP9804_Verify()'s manufacturer/device-ID check, but via a Control()
// subcommand round-trip instead of a plain register read.
bool BQ27441_Verify(uint16_t address);

// One-time (idempotent) configuration performed after BQ27441_Verify()
// succeeds: selects the external NTC thermistor on BIN as the temperature
// source (instead of the internal die sensor) and configures GPOUT to
// mirror the SOC1 low-battery threshold onto BATT_LOWBATT_PIN, via the
// CFGUPDATE + OpConfig data-flash sequence. OpConfig lives in nonvolatile
// data flash with finite write endurance, so this reads the current
// OpConfig value first and only performs the enter-cfgupdate/write-block/
// checksum/exit sequence if a bit doesn't already match -- unlike
// INA231A_Configure(), which rewrites its (volatile) Calibration register
// unconditionally every boot. Always attempts to exit CFGUPDATE mode
// before returning, even on failure, so the gauge isn't left stuck in
// config-update mode. Returns false on any I2C error or checksum/verify
// mismatch.
bool BQ27441_ConfigureOpConfig(uint16_t address);

// Blocking reads of the standard commands this board's telemetry/status
// code needs. Each returns false (leaving the output unmodified) on I2C
// error; call I2C_ErrorGet() for the reason.
bool BQ27441_ReadVoltage(uint16_t address, float *volts);
bool BQ27441_ReadAverageCurrent(uint16_t address, float *amps);       // signed; + = charging, - = discharging
bool BQ27441_ReadTemperature(uint16_t address, float *celsius);      // external thermistor, once configured
bool BQ27441_ReadStateOfCharge(uint16_t address, uint8_t *percent);
bool BQ27441_ReadStateOfHealth(uint16_t address, uint8_t *percent);
bool BQ27441_ReadRemainingCapacity(uint16_t address, float *milliamphours);
bool BQ27441_ReadFullChargeCapacity(uint16_t address, float *milliamphours);
bool BQ27441_ReadFlags(uint16_t address, BQ27441_FLAG_STATUS *status);

// Queue a non-blocking read of the raw register into raw[2] and return
// immediately; `callback` fires from I2C interrupt context on completion.
// `raw` must stay valid until then. raw[2] is LITTLE-ENDIAN here
// (raw[0]=LSB, raw[1]=MSB) -- the mirror image of MCP9804/INA231A's
// Decode*Raw() byte order. Decode the bytes afterwards (from thread
// context, since decoding does float math) with the helpers below.
bool BQ27441_QueueReadVoltage(uint16_t address, uint8_t raw[2], I2C_TRANSFER_CALLBACK callback, uintptr_t context);
bool BQ27441_QueueReadAverageCurrent(uint16_t address, uint8_t raw[2], I2C_TRANSFER_CALLBACK callback, uintptr_t context);
bool BQ27441_QueueReadTemperature(uint16_t address, uint8_t raw[2], I2C_TRANSFER_CALLBACK callback, uintptr_t context);
bool BQ27441_QueueReadStateOfCharge(uint16_t address, uint8_t raw[2], I2C_TRANSFER_CALLBACK callback, uintptr_t context);
bool BQ27441_QueueReadStateOfHealth(uint16_t address, uint8_t raw[2], I2C_TRANSFER_CALLBACK callback, uintptr_t context);
bool BQ27441_QueueReadRemainingCapacity(uint16_t address, uint8_t raw[2], I2C_TRANSFER_CALLBACK callback, uintptr_t context);
bool BQ27441_QueueReadFullChargeCapacity(uint16_t address, uint8_t raw[2], I2C_TRANSFER_CALLBACK callback, uintptr_t context);
bool BQ27441_QueueReadFlags(uint16_t address, uint8_t raw[2], I2C_TRANSFER_CALLBACK callback, uintptr_t context);

// Converts raw register images (as filled in by the QueueRead functions
// above) to engineering units / decoded flags.
float   BQ27441_DecodeVoltageRaw(const uint8_t raw[2]);
float   BQ27441_DecodeAverageCurrentRaw(const uint8_t raw[2]);
float   BQ27441_DecodeTemperatureRaw(const uint8_t raw[2]);
uint8_t BQ27441_DecodeStateOfChargeRaw(const uint8_t raw[2]);
uint8_t BQ27441_DecodeStateOfHealthRaw(const uint8_t raw[2]);
float   BQ27441_DecodeRemainingCapacityRaw(const uint8_t raw[2]);
float   BQ27441_DecodeFullChargeCapacityRaw(const uint8_t raw[2]);
void    BQ27441_DecodeFlagsRaw(const uint8_t raw[2], BQ27441_FLAG_STATUS *status);

// Prints the device's identification, configuration, and current
// measurements to the terminal, matching MCP9804_PrintStatus()/
// INA231A_PrintStatus()'s style.
void BQ27441_PrintStatus(uint16_t address);

#ifdef __cplusplus
}
#endif

#endif /* BQ27441_H */
