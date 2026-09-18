/* ************************************************************************** */
/** Power Saving

  @Company
 Marquette Senior Design E44 2018-2019

  @File Name
    power_saving.h

  @Summary
 Gives APIs for disabling unused peripherals and entering/exiting sleep mode, etc

 */
/* ************************************************************************** */

#ifndef _POWER_SAVING_H    /* Guard against multiple inclusion */
#define _POWER_SAVING_H

#include <xc.h>
#include <stdbool.h>

#include "core/device_control.h"

// These are macros needed for defining ISRs, included in XC32
#include <sys/attribs.h>

// This function disables unused peripherals on startup for power savings
// THIS FUNCTION CAN ONLY BE CALLED ONCE DUE TO PMD LOCKOUT AFTER ONE WRITE SESSION
bool PMDInitialize(void);

// Puts the board into its lowest-power state and executes WAIT, halting the
// core clock. Backing call for the "Sleep" USB UART command and for the
// POWER button's press-then-release gesture (application/pushbuttons.c
// raises power_button_sleep_request; main.c calls this).
//
// The point of this mode is the BQ27441: Impedance Track can only update
// Qmax from an open-circuit voltage reading, which needs the cell genuinely
// relaxed, and the cell cannot just be disconnected because the gauge shares
// its contacts and would lose power too. So the board quiesces everything
// else instead -- storage unmounted and the SD rail switched off, display
// and touch panel in reset with the backlight at 0%, indicator LEDs and the
// PGOOD bank off, the Lepton and its +1.2V/+2.8V rails down, and every I2C
// device that has a low-power mode shut down except the gauge and the
// elapsed-time recorder. The watchdog is stopped (it would otherwise reset
// the board roughly every 34 seconds of sleep) and every interrupt source is
// masked except Port A change-notice, which is itself narrowed to the POWER
// button's pin -- that button is the only thing that can wake the board.
//
// Note this only removes the board's own load. A connected charger keeps
// the cell loaded, so USB/DC has to come out for the cell to actually rest.
//
// Does not return: pressing POWER wakes the core, and the wake path is a
// software reset (the drivers' state no longer matches the powered-down
// hardware, so the board reinitializes from scratch rather than unwinding
// the shutdown sequence).
void enterLowPowerSleep(void);

// This function prints the status of PMD settings
void printPMDStatus(void);

#endif /* _POWER_SAVING_H */

/* *****************************************************************************
 End of File
 */
