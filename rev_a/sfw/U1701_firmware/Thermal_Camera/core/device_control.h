/* ************************************************************************** */
/** Descriptive File Name

  @Company
    Company Name

  @File Name
    filename.h

  @Summary
    Brief description of the file.

  @Description
    Describe the purpose of this file.
 */
/* ************************************************************************** */

#ifndef _DEVICE_CONTROL_H    /* Guard against multiple inclusion */
#define _DEVICE_CONTROL_H

#include <xc.h>
#include <stdbool.h>

#include "core/32mzda_interrupt_control.h"

// Hardcoded Clock Setting Integers, in Hertz
#define SYSCLK_INT          200000000

// GLCD pixel clock divider, applied to REFCLKO5 (SYSCLK undivided, 200MHz --
// see REFCLK5Initialize()) inside the GLCD peripheral:
//     GCLK = SYSCLK / GLCD_PIXEL_CLOCK_DIVIDER = 200MHz / 32 = 6.25MHz
// The GLT035320240IS1-CTP panel's DCLK spec is 5-8MHz (typ 6MHz); an even
// divider gives a 50% duty cycle (odd values give 60/40 per DS60001565).
// Defined here so every REFCLK/pixel-clock setting lives with the clock
// code; the register write itself (GLCDCLKCON.CLKDIV, a GLCD SFR) has to
// happen in glcd/glcd.c's GLCD_Initialize().
#define GLCD_PIXEL_CLOCK_DIVIDER    32

// logic level macros
#define HIGH        1
#define LOW         0

// Unlock system function
// This function unlocks the device so that device parameters can be changed
// and the microcontroller can be reset
void deviceUnlock(void);


// Lock system function
// This function re-locks the system so that important device parameters may 
// not be changed
void deviceLock(void);

// Reset device function
// This function resets the microcontroller
void deviceReset(void);

// This function is a software delay that simply counts loops while decrementing
// the argument
void softwareDelay(uint32_t inputDelay);

// This function initializes the system clocks
bool clockInitialize(void);

// This function returns a formatted string of a given clock setting from an integer
char * stringFromClockSetting(uint32_t clock_integer);

// This function unlocks peripheral pin select
// THIS CAN ONLY BE CALLED ONCE PER DEVICE RESET!!!
void PPSUnlock(void);

// This function locks peripheral pin select
void PPSLock(void);

// This function unlocks peripheral module disable
void PMDUnlock(void);

// This function locks peripheral module disable
void PMDLock(void);

// This function returns a string containing the device's serial number
char * getStringSerialNumber(void);

// This function returns a 32 bit device ID
uint32_t getDeviceID(void);

// This function returns a string with the part number of the device from the device ID
char * getDeviceIDString(uint32_t device_ID);

// This function returns an 8 bit revision ID
uint8_t getRevisionID(void);

// This function returns a string with the revision ID
char * getRevisionIDString(uint8_t revision_ID);

// This function prints clock settings, requires a given input sysclk
void printClockStatus(uint32_t input_sysclk);

// This function initializes the random number generator
void RNGInitialize(void);

// this function prints status for the passed timer. Pass timer 1-9
void printTimerStatus(uint8_t timer_number);

// this function prints status of all DMA channels
void printDMAStatus(void);

#endif /* _DEVICE_CONTROL_H */

/* *****************************************************************************
 End of File
 */