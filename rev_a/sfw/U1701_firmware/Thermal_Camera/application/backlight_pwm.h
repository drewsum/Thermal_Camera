/*******************************************************************************
  LCD Backlight PWM Driver

  File Name:
    backlight_pwm.h

  Summary:
    Drives the LCD backlight brightness via Output Compare 3 (OC3) in PWM
    mode, clocked from Timer 4, output on RPB5 (the BACKLIGHT_PWM net).

  Description:
    RPB5 is already mapped to OC3 via PPS in gpio/pic32mzda_gpio_setup.c
    (portBGPIOInitialize(): "RPB5Rbits.RPB5R = OC3_PPS_OUTPUT"), and RB5 is
    already configured TRIS_OUTPUT there too -- this driver only configures
    the Timer4/OC3 peripherals themselves, not the pin muxing. Once OC3 is
    enabled it drives RB5 directly; writing the pin's LATB bit no longer has
    any effect, so there is no plain digital "enable" pin macro for it.

    Timer/OC pairing -- READ THIS BEFORE CHANGING TIMERS: which timers an
    OC module can use is set by CFGCON.OCACLK, and on this board OCACLK is
    1, giving OC3 the choice of Timer4 (OCTSEL=0) or Timer5 (OCTSEL=1)
    only (PIC32MZ-DA datasheet DS60001565, Table 16-1's OCACLK=1 half).
    This driver uses Timer4 (OCTSEL=0). Two non-obvious details, learned
    during bring-up (2026-07-19, PWM output stuck low):

    1. OCACLK is a lock-protected CFGCON bit ("to change this bit, the
       unlock sequence must be performed", Register 41-9 Note 1).
       clockInitialize() (core/device_control.c) sets OCACLK=1 inside its
       deviceUnlock()/deviceLock() window, so that value is what sticks.

    2. core/heartbeat_timer.c used to write OCACLK=0 with no unlock -- a
       write the lock silently discards -- which made the source code
       MISREPRESENT the live register state: this driver's first version
       trusted it and clocked Timer3 (a valid OC3 source only under
       OCACLK=0), leaving OC3 listening to a never-started Timer5 and the
       backlight dark. That dead write has since been removed. The
       heartbeat LED's own PWM (OC4) never exposed the discrepancy because
       OC4's Timerx is Timer2 under BOTH OCACLK mappings.

    BacklightPWM_Initialize() therefore *verifies* OCACLK==1 at run time
    and fails loudly (red boot line via its reportInit() wrapper) rather
    than trusting any code comment, including this one.

    PWM frequency: Timer4 runs off PBCLK3 (12.5MHz, per the comments in
    core/heartbeat_timer.c/core/device_control.c) with a prescaler of 1 and
    PR4 = 1249, giving exactly 10.000kHz (12.5MHz / 1250) -- well above the
    flicker threshold and a conventional backlight PWM frequency.

    Duty cycle: OC3RS is computed as percent * (PR4+1) / 100, with 100%
    mapped to PR4+1 (a value the 0..PR4 counter can never reach, forcing
    the output permanently high) rather than PR4 itself, which would still
    produce one narrow low pulse per period -- the standard technique for
    true 100% duty in Output Compare PWM mode.
*******************************************************************************/

#ifndef BACKLIGHT_PWM_H
#define BACKLIGHT_PWM_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Configures Timer4 for a 10kHz period and OC3 for PWM mode sourced from
// it, with the backlight initially at 0% (off) -- call
// BacklightPWM_SetBrightness() afterward for any other starting level.
// Returns false if CFGCON.OCACLK != 1 (the OC3-to-Timer4 pairing this
// driver depends on -- see file header), or if Timer4/OC3 failed to start.
bool BacklightPWM_Initialize(void);

// Sets the backlight brightness, clamped to 0-100 (%). 0 is fully off; 100
// is fully on (continuous high output, not just a near-100% duty cycle).
void BacklightPWM_SetBrightness(uint8_t percent);

// Returns the brightness last set by BacklightPWM_SetBrightness() (or the
// BacklightPWM_Initialize() default of 0 if it hasn't been called yet).
uint8_t BacklightPWM_GetBrightness(void);

// Prints OC3/Timer4 peripheral state (enabled, period, compare value,
// OCACLK pairing, computed PWM frequency) and the current brightness
// percentage. Backs the "Peripheral Status? Backlight PWM" USB UART
// command.
void BacklightPWM_PrintStatus(void);

#ifdef __cplusplus
}
#endif

#endif /* BACKLIGHT_PWM_H */
