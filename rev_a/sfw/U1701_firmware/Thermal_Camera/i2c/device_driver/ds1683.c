/*******************************************************************************
  DS1683 I2C Total-Elapsed-Time and Event Recorder Driver

  File Name:
    ds1683.c

  Summary:
    Driver for the Maxim/Analog Devices DS1683 total-elapsed-time and event
    recorder, built on i2c_master.h.
*******************************************************************************/

#include "i2c/device_driver/ds1683.h"
#include "i2c/i2c_master.h"
#include "usb_uart/terminal_control.h"

#include <stdio.h>

// *****************************************************************************
// Section: Register Map
// *****************************************************************************
// Per the DS1683 datasheet's Table 1 (Register Memory Map). Multi-byte
// registers are little-endian (LSB at the lowest address) -- the opposite
// byte order from MCP9804/INA231A's MSB-first 16-bit registers.

#define DS1683_REG_COMMAND             0x00u   // 1 byte
#define DS1683_REG_STATUS              0x01u   // 1 byte, read-only
#define DS1683_REG_PWE                 0x02u   // 4 bytes, 0x02-0x05, write-only
#define DS1683_REG_EVENT_COUNTER       0x08u   // 2 bytes, 0x08-0x09, LSB first
#define DS1683_REG_ETC                 0x0Au   // 4 bytes, 0x0A-0x0D, LSB first
#define DS1683_REG_EVENT_ALARM_LIMIT   0x10u   // 2 bytes, 0x10-0x11, LSB first
#define DS1683_REG_ETC_ALARM_LIMIT     0x12u   // 4 bytes, 0x12-0x15, LSB first
#define DS1683_REG_CONFIG              0x16u   // 1 byte
#define DS1683_REG_PWV                 0x1Au   // 4 bytes, 0x1A-0x1D, write-only

// Command register (0x00) bit field.
#define DS1683_COMMAND_CLR_ALM         0x01u   // write 1 to unlatch ALARM; always reads as 0

// Status register (0x01) bit fields.
#define DS1683_STATUS_ETC_AF           0x01u   // ETC register >= ETC Alarm Limit
#define DS1683_STATUS_EVENT_AF         0x02u   // Event Counter register >= Event Counter Alarm Limit
#define DS1683_STATUS_EVENT            0x04u   // live EVENT pin level (post glitch-filter)

// Configuration register (0x16) bit fields.
#define DS1683_CONFIG_ALRM_POL         0x01u   // 0 = ALARM active low, 1 = active high
#define DS1683_CONFIG_EVENT_ALRM_EN    0x02u
#define DS1683_CONFIG_ETC_ALRM_EN      0x04u

// The ETC register accumulates in 250ms ticks.
#define DS1683_ETC_TICKS_PER_SECOND    4u

// *****************************************************************************
// Section: Register Encode/Decode Helpers
// *****************************************************************************

static uint32_t DS1683_DecodeU32LE(const uint8_t raw[4])
{
    return ((uint32_t)raw[3] << 24) | ((uint32_t)raw[2] << 16) |
           ((uint32_t)raw[1] << 8)  |  (uint32_t)raw[0];
}

static uint16_t DS1683_DecodeU16LE(const uint8_t raw[2])
{
    return ((uint16_t)raw[1] << 8) | raw[0];
}

// *****************************************************************************
// Section: Interface Routines
// *****************************************************************************

bool DS1683_Verify(uint16_t address)
{
    uint8_t command;

    if (!I2C_ReadRegister(address, DS1683_REG_COMMAND, &command, 1))
    {
        return false;
    }

    // CLR ALM (bit 0) always reads as 0, and the rest of this register is
    // reserved with no user-writable state -- see the header's caveat.
    return (command == 0x00u);
}

bool DS1683_ReadElapsedSeconds(uint16_t address, uint32_t *seconds)
{
    uint8_t raw[4];

    if (!I2C_ReadRegister(address, DS1683_REG_ETC, raw, sizeof(raw)))
    {
        return false;
    }

    *seconds = DS1683_DecodeElapsedSecondsRaw(raw);
    return true;
}

bool DS1683_ReadEventCount(uint16_t address, uint16_t *count)
{
    uint8_t raw[2];

    if (!I2C_ReadRegister(address, DS1683_REG_EVENT_COUNTER, raw, sizeof(raw)))
    {
        return false;
    }

    *count = DS1683_DecodeU16LE(raw);
    return true;
}

bool DS1683_ReadStatus(uint16_t address, DS1683_STATUS *status)
{
    uint8_t raw;

    if (!I2C_ReadRegister(address, DS1683_REG_STATUS, &raw, 1))
    {
        return false;
    }

    status->etcAlarm     = (raw & DS1683_STATUS_ETC_AF)   != 0;
    status->eventAlarm   = (raw & DS1683_STATUS_EVENT_AF) != 0;
    status->eventPinHigh = (raw & DS1683_STATUS_EVENT)    != 0;
    return true;
}

bool DS1683_ClearAlarm(uint16_t address)
{
    uint8_t value = DS1683_COMMAND_CLR_ALM;

    return I2C_WriteRegister(address, DS1683_REG_COMMAND, &value, 1);
}

bool DS1683_QueueReadElapsedTime(uint16_t address, uint8_t raw[4],
                                 I2C_TRANSFER_CALLBACK callback, uintptr_t context)
{
    return I2C_QueueReadRegister(address, DS1683_REG_ETC, raw, 4, callback, context);
}

uint32_t DS1683_DecodeElapsedSecondsRaw(const uint8_t raw[4])
{
    return DS1683_DecodeU32LE(raw) / DS1683_ETC_TICKS_PER_SECOND;
}

void DS1683_PrintStatus(uint16_t address)
{
    uint8_t command;
    uint8_t config;
    uint8_t rawStatus;
    DS1683_STATUS status;
    uint32_t seconds;
    uint16_t eventCount;
    bool identified;

    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, BOLD_FONT);
    printf("    --- DS1683 (address 0x%02X) ---\n\r", address);

    if (!I2C_ReadRegister(address, DS1683_REG_COMMAND, &command, 1))
    {
        terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("    No response from device (I2C error: %d)\n\r", (int)I2C_ErrorGet());
        terminalTextAttributesReset();
        return;
    }

    identified = (command == 0x00u);
    terminalTextAttributes(identified ? GREEN_COLOR : RED_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("    Command register: 0x%02X (%s)\n\r", command,
           identified ? "as expected" : "unexpected -- not a DS1683?");

    if (I2C_ReadRegister(address, DS1683_REG_CONFIG, &config, 1))
    {
        terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("    Configuration register: 0x%02X (%s%s%s)\n\r", config,
               (config & DS1683_CONFIG_ETC_ALRM_EN)   ? "ETC_ALRM_EN "   : "",
               (config & DS1683_CONFIG_EVENT_ALRM_EN) ? "EVENT_ALRM_EN " : "",
               (config & DS1683_CONFIG_ALRM_POL)      ? "ALRM_POL_HIGH " : "");
        printf("        ETC alarm:      %s\n\r", (config & DS1683_CONFIG_ETC_ALRM_EN)   ? "enabled" : "disabled");
        printf("        Event alarm:    %s\n\r", (config & DS1683_CONFIG_EVENT_ALRM_EN) ? "enabled" : "disabled");
        printf("        Alarm polarity: active %s\n\r", (config & DS1683_CONFIG_ALRM_POL) ? "high" : "low");
    }

    if (I2C_ReadRegister(address, DS1683_REG_STATUS, &rawStatus, 1) &&
        DS1683_ReadStatus(address, &status))
    {
        bool anyAlarm = status.etcAlarm || status.eventAlarm;

        terminalTextAttributes(anyAlarm ? YELLOW_COLOR : GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("    Status register: 0x%02X (%s%s%s)\n\r", rawStatus,
               (rawStatus & DS1683_STATUS_ETC_AF)   ? "ETC_AF "   : "",
               (rawStatus & DS1683_STATUS_EVENT_AF) ? "EVENT_AF " : "",
               (rawStatus & DS1683_STATUS_EVENT)    ? "EVENT_HIGH " : "");
        printf("    ETC alarm: %s, Event alarm: %s, EVENT pin: %s\n\r",
               status.etcAlarm   ? "ACTIVE" : "clear",
               status.eventAlarm ? "ACTIVE" : "clear",
               status.eventPinHigh ? "high" : "low");
    }

    if (DS1683_ReadElapsedSeconds(address, &seconds))
    {
        uint32_t days  = seconds / 86400u;
        uint32_t hours = (seconds / 3600u) % 24u;
        uint32_t mins  = (seconds / 60u) % 60u;
        uint32_t secs  = seconds % 60u;

        terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("    Elapsed time: %lu s (%lud %02lu:%02lu:%02lu)\n\r",
               (unsigned long)seconds, (unsigned long)days,
               (unsigned long)hours, (unsigned long)mins, (unsigned long)secs);
    }

    if (DS1683_ReadEventCount(address, &eventCount))
    {
        printf("    Event count: %u\n\r", eventCount);
    }

    terminalTextAttributesReset();
}
