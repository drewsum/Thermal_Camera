/*******************************************************************************
  GLT035320240IS1-CTP Panel Timing/Reset Driver

  File Name:
    glt035320240is1.c

  Summary:
    Panel-specific reset sequencing. See glt035320240is1.h for the timing
    derivation and layering rationale.
*******************************************************************************/

#include <xc.h>
#include <sys/kmem.h>

#include "glcd/device_driver/glt035320240is1.h"
#include "core/device_control.h"
#include "gpio/pin_macros.h"

#define GLT035320240IS1_TIMEOUT_TICKS(us) \
    ((uint32_t)(((uint64_t)SYSCLK_INT / 2u) * (us) / 1000000u))

// Calibrated microsecond delay via CP0 Count (increments at SYSCLK/2) --
// unlike softwareDelay() (core/device_control.c, a raw NOP-counting loop
// with no fixed relationship to real time), this holds regardless of
// compiler optimization level. Mirrors SD_Card_DelayUs()
// (sdhc/device_driver/sd_card.c) since the panel reset pulse width
// (Tasta, 40us min) is a real datasheet timing requirement, not a rough
// settling guess.
static void GLT035320240IS1_DelayUs(uint32_t us)
{
    uint32_t start = _CP0_GET_COUNT();
    uint32_t ticks = GLT035320240IS1_TIMEOUT_TICKS(us);
    while ((uint32_t)(_CP0_GET_COUNT() - start) < ticks)
    {
        // busy-wait
    }
}

void GLT035320240IS1_ResetPulse(void)
{
    LCD_ENABLE_PIN = LOW;
    GLT035320240IS1_DelayUs(GLT035320240IS1_RESET_PULSE_US);
    LCD_ENABLE_PIN = HIGH;
}
