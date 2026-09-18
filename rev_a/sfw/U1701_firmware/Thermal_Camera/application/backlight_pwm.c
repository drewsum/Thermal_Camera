/*******************************************************************************
  LCD Backlight PWM Driver

  File Name:
    backlight_pwm.c

  Summary:
    Timer4/OC3 PWM driver for the LCD backlight. See backlight_pwm.h for
    the OCACLK/timer-pairing story and duty-cycle formula.
*******************************************************************************/

#include <xc.h>
#include <stdio.h>

#include "application/backlight_pwm.h"
#include "usb_uart/terminal_control.h"

// PBCLK3 frequency -- see core/heartbeat_timer.c, which already clocks
// Timer1/Timer2 from this same peripheral bus and documents it the same
// way (no shared header constant for it exists in this codebase).
#define BACKLIGHT_PWM_PBCLK3_HZ     12500000UL

// PR4 for exactly 10.000kHz: PBCLK3 / (prescaler=1 * (PR4+1))
#define BACKLIGHT_PWM_PR4_VALUE     1249u

static uint8_t backlight_brightness_percent = 0;

bool BacklightPWM_Initialize(void)
{
    // This driver's OC3-to-Timer4 pairing only exists when CFGCON.OCACLK=1
    // (set, under unlock, by clockInitialize()). Verify the live register
    // rather than trusting code elsewhere -- a lock-protected write that
    // silently failed is exactly how the first version of this driver
    // ended up clocking a timer OC3 couldn't see (backlight_pwm.h).
    if (CFGCONbits.OCACLK != 1)
    {
        return false;
    }

    // Timer4: free-running period source for OC3. SIDL/TGATE/TCS choices
    // match Timer1/Timer2's already-established configuration in
    // core/heartbeat_timer.c. Deliberately NOT started yet (ON left 0) --
    // see the note below on start order.
    T4CONbits.ON = 0;
    T4CONbits.SIDL = 1;
    T4CONbits.TGATE = 0;
    T4CONbits.T32 = 0;         // independent 16-bit timer, not a T4/T5 32-bit pair
    T4CONbits.TCKPS = 0b000;   // prescaler 1:1
    T4CONbits.TCS = 0;         // PBCLK3
    TMR4 = 0x0000;
    PR4 = BACKLIGHT_PWM_PR4_VALUE;

    // OC3: PWM mode (fault pin disabled), sourced from Timer4 (OCTSEL=0 =
    // Timerx; under OCACLK=1, OC3's Timerx is Timer4 -- see
    // backlight_pwm.h). SIDL=0 keeps the backlight lit if the CPU idles,
    // matching OC4's choice for the heartbeat LED.
    OC3CONbits.ON = 0;
    OC3CONbits.SIDL = 0;
    OC3CONbits.OCTSEL = 0;
    OC3CONbits.OC32 = 0;
    OC3RS = 0;
    OC3R = 0;
    OC3CONbits.OCM = 0b110;
    OC3CONbits.ON = 1;

    // Start Timer4 LAST, after OC3 is already enabled -- matches
    // core/heartbeat_timer.c's proven-working order for OC4/Timer2
    // (OC4CONbits.ON=1 there also precedes "Start timer 2").
    T4CONbits.ON = 1;

    backlight_brightness_percent = 0;

    return (T4CONbits.ON && OC3CONbits.ON);
}

void BacklightPWM_SetBrightness(uint8_t percent)
{
    if (percent > 100u) percent = 100u;

    // 100% maps to PR4+1 (unreachable by the 0..PR4 counter, forcing the
    // output permanently high) rather than PR4 itself -- see file header.
    OC3RS = (percent == 100u)
            ? (uint16_t)(BACKLIGHT_PWM_PR4_VALUE + 1u)
            : (uint16_t)(((uint32_t)percent * (BACKLIGHT_PWM_PR4_VALUE + 1u)) / 100u);

    backlight_brightness_percent = percent;
}

uint8_t BacklightPWM_GetBrightness(void)
{
    return backlight_brightness_percent;
}

// 16-bit synchronous timer (Timer2-9) TCKPS<2:0> prescaler encoding, per
// the device datasheet -- same mapping device_control.c's generic
// printTimerStatus() decodes via a switch statement.
static uint32_t BacklightPWM_DecodePrescaler(void)
{
    static const uint16_t prescaler_values[8] = { 1, 2, 4, 8, 16, 32, 64, 256 };
    return prescaler_values[T4CONbits.TCKPS & 0x7u];
}

void BacklightPWM_PrintStatus(void)
{
    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, BOLD_FONT);
    printf("    --- LCD Backlight PWM ---\n\r");

    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("    CFGCON.OCACLK: %u (%s timer map; OC3 pairs with %s)\n\r",
            (unsigned int)CFGCONbits.OCACLK,
            CFGCONbits.OCACLK ? "alternate" : "default",
            CFGCONbits.OCACLK ? "Timer4/Timer5" : "Timer2/Timer3");
    printf("    Timer4: %s, PR4=%u, TMR4=%u\n\r",
            T4CONbits.ON ? "running" : "STOPPED", (unsigned int)PR4, (unsigned int)TMR4);
    printf("    OC3: %s, OCM=0b%u%u%u, OCTSEL=%u (%s), OC3RS=%u\n\r",
            OC3CONbits.ON ? "running" : "STOPPED",
            (unsigned int)((OC3CONbits.OCM >> 2) & 1u),
            (unsigned int)((OC3CONbits.OCM >> 1) & 1u),
            (unsigned int)(OC3CONbits.OCM & 1u),
            (unsigned int)OC3CONbits.OCTSEL, OC3CONbits.OCTSEL ? "Timery" : "Timerx",
            (unsigned int)OC3RS);

    if (T4CONbits.ON && (PR4 > 0))
    {
        uint32_t pwm_hz = BACKLIGHT_PWM_PBCLK3_HZ / (BacklightPWM_DecodePrescaler() * ((uint32_t)PR4 + 1u));
        printf("    Computed PWM Frequency: %lu Hz\n\r", (unsigned long)pwm_hz);
    }

    printf("    Brightness: %u%%\n\r", (unsigned int)backlight_brightness_percent);

    terminalTextAttributesReset();
}
