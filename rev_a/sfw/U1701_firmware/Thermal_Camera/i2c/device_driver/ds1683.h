/*******************************************************************************
  DS1683 I2C Total-Elapsed-Time and Event Recorder Driver

  File Name:
    ds1683.h

  Summary:
    Driver for the Maxim/Analog Devices DS1683 total-elapsed-time and event
    recorder, built on i2c_master.h.

  Description:
    The DS1683 has a single fixed 7-bit I2C address (0x6B) -- unlike
    MCP9804/INA231A, it has no address-strapping pins, so at most one
    DS1683 can live on a given I2C bus. There is no per-device "instance"
    state here either, for consistency with the other drivers: every
    function still takes the device's I2C address explicitly.

    The device has no manufacturer/device ID register. DS1683_Verify()
    instead checks that the Command register reads back 0x00, which is a
    reliable signal here (unlike a config/calibration register on other
    parts): bit 0 (CLR ALM) always reads as 0 per the datasheet, and the
    register holds no other user state that could drift away from that
    value.

    The Elapsed Time Counter (ETC) is a 32-bit nonvolatile counter that
    accumulates in 250ms (1/4) second ticks while the EVENT pin is held
    high, giving ~34 years of total range. Reads below return the whole
    seconds as a uint32_t rather than a float specifically to avoid losing
    precision near the top of that range (a 32-bit float's ~24-bit mantissa
    can't exactly represent every integer above ~16.7 million).
*******************************************************************************/

#ifndef DS1683_H
#define DS1683_H

#include <stdint.h>
#include <stdbool.h>

#include "i2c/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

// Fixed 7-bit I2C address -- the DS1683 has no address-select pins.
#define DS1683_BASE_ADDRESS   0x6Bu

// Alarm/status flags from the Status register (0x01).
typedef struct
{
    bool etcAlarm;       // ETC AF: ETC register >= ETC Alarm Limit
    bool eventAlarm;     // EVENT AF: Event Counter register >= Event Counter Alarm Limit
    bool eventPinHigh;   // live level of the EVENT pin, after the glitch filter
} DS1683_STATUS;

// Confirms a device at `address` responds and its Command register reads
// back the only value it should ever have (0x00) -- see the caveat above.
bool DS1683_Verify(uint16_t address);

// Reads the accumulated elapsed time in whole seconds (the ETC register's
// native 250ms resolution is truncated away -- see the caveat above).
bool DS1683_ReadElapsedSeconds(uint16_t address, uint32_t *seconds);

// Reads the Event Counter: the number of falling edges seen on the EVENT pin.
bool DS1683_ReadEventCount(uint16_t address, uint16_t *count);

// Reads the ETC/EVENT alarm flags and the live EVENT pin level together.
bool DS1683_ReadStatus(uint16_t address, DS1683_STATUS *status);

// Unlatches the ALARM output (if the alarm condition that set it has since
// cleared). No effect on the ETC/EVENT alarm flags themselves.
bool DS1683_ClearAlarm(uint16_t address);

// Queues a non-blocking read of the raw ETC register into raw[4] (LSB
// first -- the DS1683 is little-endian, unlike MCP9804/INA231A) and returns
// immediately; `callback` fires from I2C interrupt context on completion.
// `raw` must stay valid until then. Decode the bytes afterwards (from
// thread context) with the helper below.
bool DS1683_QueueReadElapsedTime(uint16_t address, uint8_t raw[4],
                                 I2C_TRANSFER_CALLBACK callback, uintptr_t context);

// Converts a raw ETC register image (as filled in by
// DS1683_QueueReadElapsedTime()) to whole seconds.
uint32_t DS1683_DecodeElapsedSecondsRaw(const uint8_t raw[4]);

// Prints the device's identification, configuration, alarm status, elapsed
// time, and event count to the terminal.
// Everything DS1683_PrintStatus() shows, as data: the command/config/status
// registers and the two counters. Exists so the GUI's I2C status screen can
// show the same diagnostics the console does without either one re-deriving
// the register map -- DS1683_PrintStatus() is written on top of this.
//
// A field is only meaningful when its `*Valid` companion is true. The
// Command register read is the gate for the whole struct, and doubles as the
// identity check -- see the caveat on DS1683_Verify().
typedef struct
{
    bool commandValid;       // false = no response; nothing else is filled in
    uint8_t command;
    bool identified;         // Command register reads the only value it should

    bool configValid;
    uint8_t config;

    bool statusValid;
    uint8_t rawStatus;
    DS1683_STATUS status;    // the same register, decoded

    bool elapsedValid;
    uint32_t elapsedSeconds;

    bool eventCountValid;
    uint16_t eventCount;
} DS1683_DIAGNOSTICS;

// Fills `out` with the above. Returns commandValid, i.e. whether the device
// answered at all. Read-only: touches no configuration and does not
// unlatch the alarm.
bool DS1683_ReadDiagnostics(uint16_t address, DS1683_DIAGNOSTICS *out);

void DS1683_PrintStatus(uint16_t address);

#ifdef __cplusplus
}
#endif

#endif /* DS1683_H */
