/*******************************************************************************
  FLIR Lepton CCI (Command and Control Interface) Driver

  File Name:
    flir_cci.c

  Summary:
    I2C control-plane driver for the FLIR Lepton 3.5. See flir_cci.h for the
    register layout and the command/status handshake this implements.
*******************************************************************************/

#include "application/flir/flir_cci.h"
#include "i2c/i2c_master.h"
#include "core/device_control.h"
#include "core/watchdog_timer.h"
#include "usb_uart/terminal_control.h"

#include <xc.h>
#include <stdio.h>

// The CCI busy bit can stay set for a while on commands that touch the shutter
// or flash; the SDK's own default command timeout is generous. This bound is
// on CP0 Count (SYSCLK/2) -- ~500ms -- so a wedged handshake fails the call
// instead of hanging the main loop. The watchdog is kicked inside the wait.
#define FLIR_CCI_BUSY_TIMEOUT_MS    500u

// Kelvin-to-Celsius offset; the Lepton reports temperature in centi-Kelvin.
#define FLIR_CCI_KELVIN_OFFSET_C    273.15f

bool FLIR_CCI_ReadReg16(uint16_t reg, uint16_t *value)
{
    uint8_t addr[2] = { (uint8_t)(reg >> 8), (uint8_t)(reg & 0xFFu) };
    uint8_t rx[2];

    if (value == NULL)
    {
        return false;
    }

    if (!I2C_WriteRead(FLIR_CCI_I2C_ADDRESS, addr, 2, rx, 2))
    {
        return false;
    }

    // Big-endian on the wire (MSB first).
    *value = (uint16_t)(((uint16_t)rx[0] << 8) | rx[1]);
    return true;
}

bool FLIR_CCI_WriteReg16(uint16_t reg, uint16_t value)
{
    uint8_t buffer[4] = {
        (uint8_t)(reg >> 8), (uint8_t)(reg & 0xFFu),
        (uint8_t)(value >> 8), (uint8_t)(value & 0xFFu)
    };

    return I2C_Write(FLIR_CCI_I2C_ADDRESS, buffer, 4);
}

bool FLIR_CCI_ReadStatus(uint16_t *status)
{
    return FLIR_CCI_ReadReg16(FLIR_CCI_REG_STATUS, status);
}

bool FLIR_CCI_WaitIdle(int8_t *resultCode)
{
    uint32_t start = _CP0_GET_COUNT();
    // CP0 Count runs at SYSCLK/2.
    uint32_t timeoutTicks = ((uint32_t)(SYSCLK_INT / 2u) / 1000u) * FLIR_CCI_BUSY_TIMEOUT_MS;
    uint16_t status;

    for (;;)
    {
        if (!FLIR_CCI_ReadStatus(&status))
        {
            return false;
        }

        if ((status & FLIR_CCI_STATUS_BUSY_MASK) == 0)
        {
            // Bits 15:8 are a signed result code (LEP_RESULT).
            if (resultCode != NULL)
            {
                *resultCode = (int8_t)(status >> FLIR_CCI_STATUS_ERROR_SHIFT);
            }
            return true;
        }

        // A long command must not trip the watchdog while we spin here.
        kickTheDog();

        if ((_CP0_GET_COUNT() - start) > timeoutTicks)
        {
            return false;
        }
    }
}

// Writes the DATA LENGTH register (word count) shared by GET/SET/RUN framing.
static bool FLIR_CCI_WriteDataLength(uint16_t wordCount)
{
    return FLIR_CCI_WriteReg16(FLIR_CCI_REG_DATA_LENGTH, wordCount);
}

bool FLIR_CCI_GetAttribute(uint16_t commandBase, uint16_t *data, uint16_t wordCount)
{
    int8_t result = 0;
    uint16_t i;

    if ((data == NULL) || (wordCount == 0) || (wordCount > 16))
    {
        return false;
    }

    // For a GET the DATA LENGTH tells the camera how many words we expect
    // back, then COMMAND kicks it off.
    if (!FLIR_CCI_WriteDataLength(wordCount))
    {
        return false;
    }
    if (!FLIR_CCI_WriteReg16(FLIR_CCI_REG_COMMAND, commandBase | FLIR_CCI_TYPE_GET))
    {
        return false;
    }
    if (!FLIR_CCI_WaitIdle(&result) || (result != 0))
    {
        return false;
    }

    // Result words are consecutive 16-bit registers from DATA 0.
    for (i = 0; i < wordCount; i++)
    {
        if (!FLIR_CCI_ReadReg16((uint16_t)(FLIR_CCI_REG_DATA_0 + (i * 2u)), &data[i]))
        {
            return false;
        }
    }

    return true;
}

bool FLIR_CCI_SetAttribute(uint16_t commandBase, const uint16_t *data, uint16_t wordCount)
{
    int8_t result = 0;
    uint16_t i;

    if ((data == NULL) || (wordCount == 0) || (wordCount > 16))
    {
        return false;
    }

    // Load the parameter words, then the length, then trigger.
    for (i = 0; i < wordCount; i++)
    {
        if (!FLIR_CCI_WriteReg16((uint16_t)(FLIR_CCI_REG_DATA_0 + (i * 2u)), data[i]))
        {
            return false;
        }
    }
    if (!FLIR_CCI_WriteDataLength(wordCount))
    {
        return false;
    }
    if (!FLIR_CCI_WriteReg16(FLIR_CCI_REG_COMMAND, commandBase | FLIR_CCI_TYPE_SET))
    {
        return false;
    }

    return FLIR_CCI_WaitIdle(&result) && (result == 0);
}

bool FLIR_CCI_RunCommand(uint16_t commandBase)
{
    int8_t result = 0;

    // RUN commands used here take no parameters.
    if (!FLIR_CCI_WriteDataLength(0))
    {
        return false;
    }
    if (!FLIR_CCI_WriteReg16(FLIR_CCI_REG_COMMAND, commandBase | FLIR_CCI_TYPE_RUN))
    {
        return false;
    }

    return FLIR_CCI_WaitIdle(&result) && (result == 0);
}

bool FLIR_CCI_Verify(void)
{
    uint16_t status;

    // Presence only: an ACKed STATUS read is enough. Booted-ness is a
    // separate check (FLIR_CCI_IsBooted) since the camera ACKs well before it
    // finishes booting.
    return FLIR_CCI_ReadStatus(&status);
}

bool FLIR_CCI_IsBooted(void)
{
    uint16_t status;

    if (!FLIR_CCI_ReadStatus(&status))
    {
        return false;
    }

    return ((status & FLIR_CCI_STATUS_BOOT_STATUS_MASK) != 0) &&
           ((status & FLIR_CCI_STATUS_BUSY_MASK) == 0);
}

bool FLIR_CCI_ConfigureRaw14Video(void)
{
    uint16_t disable[2] = { 0x0000u, 0x0000u };

    // AGC off: with AGC disabled the Lepton emits linear RAW14 pixels rather
    // than its own 8-bit contrast-mapped output. The MCU does AGC in
    // flir_process.c so it keeps the underlying radiometric values.
    if (!FLIR_CCI_SetAttribute(FLIR_CCI_CMD_AGC_ENABLE, disable, 2))
    {
        return false;
    }

    // Telemetry off: keeps every VoSPI frame a clean 160x120 image with no
    // extra telemetry rows to skip. (Enable later as a footer if the
    // housekeeping data becomes useful.)
    if (!FLIR_CCI_SetAttribute(FLIR_CCI_CMD_SYS_TELEMETRY_ENABLE, disable, 2))
    {
        return false;
    }

    return true;
}

bool FLIR_CCI_ReadTemperatures(float *fpaCelsius, float *auxCelsius)
{
    uint16_t raw;

    if (fpaCelsius != NULL)
    {
        if (!FLIR_CCI_GetAttribute(FLIR_CCI_CMD_SYS_FPA_TEMP_K, &raw, 1))
        {
            return false;
        }
        // Reported in centi-Kelvin.
        *fpaCelsius = ((float)raw / 100.0f) - FLIR_CCI_KELVIN_OFFSET_C;
    }

    if (auxCelsius != NULL)
    {
        if (!FLIR_CCI_GetAttribute(FLIR_CCI_CMD_SYS_AUX_TEMP_K, &raw, 1))
        {
            return false;
        }
        *auxCelsius = ((float)raw / 100.0f) - FLIR_CCI_KELVIN_OFFSET_C;
    }

    return true;
}

void FLIR_CCI_PrintStatus(void)
{
    uint16_t status;
    float fpaC = 0.0f, auxC = 0.0f;

    if (!FLIR_CCI_ReadStatus(&status))
    {
        terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("    CCI unreachable (no I2C ACK -- sensor powered down?)\n\r");
        terminalTextAttributesReset();
        return;
    }

    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("    CCI STATUS: 0x%04X (boot %s, %s, result %d)\n\r",
           (unsigned int)status,
           (status & FLIR_CCI_STATUS_BOOT_STATUS_MASK) ? "complete" : "pending",
           (status & FLIR_CCI_STATUS_BUSY_MASK) ? "busy" : "idle",
           (int)(int8_t)(status >> FLIR_CCI_STATUS_ERROR_SHIFT));

    if (FLIR_CCI_ReadTemperatures(&fpaC, &auxC))
    {
        printf("    FPA Temp: %.2f C    AUX (housing) Temp: %.2f C\n\r",
               (double)fpaC, (double)auxC);
    }
    else
    {
        terminalTextAttributes(YELLOW_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("    Temperatures unavailable (camera still booting?)\n\r");
    }

    terminalTextAttributesReset();
}
