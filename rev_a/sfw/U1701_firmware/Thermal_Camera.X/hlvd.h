/* ************************************************************************** */
/** High/Low-Voltage Detect (HLVD)

  @Company
    Marquette Senior Design E44

  @File Name
    hlvd.h

  @Summary
    Driver for the PIC32MZ2064DAR176 High/Low-Voltage Detect (HLVD) module

  @Description
    IMPORTANT: On this device, HLVD does not have a vectored interrupt. The
    interrupt controller (IFS/IEC/IPC) has no HLVD entry at all. A HLVD event
    only ever appears as:
      - HLVDCONbits.HLEVT, a live status bit that follows the comparator, and
      - RNMICONbits.LVD, a latched status bit set by hardware and cleared by
        software, described in the device's "Resets" chapter as part of the
        NMI control register.
    Despite the "NMI" naming, setting RNMICONbits.LVD does not redirect
    program flow, start a countdown, or force a reset (unlike the WDT/DMT
    timeout sources in the same register) -- it is a pure status flag. There
    is therefore no ISR for this module; it must be polled.
 */
/* ************************************************************************** */

#ifndef _HLVD_H    /* Guard against multiple inclusion */
#define _HLVD_H

#include <xc.h>

// HLVDCON.VDIR: selects which direction of VDD crossing generates an event
typedef enum {

    HLVD_DIRECTION_LOW_VOLTAGE  = 0,   // event when VDD falls to or below the trip point
    HLVD_DIRECTION_HIGH_VOLTAGE = 1    // event when VDD rises to or above the trip point

} hlvd_direction_t;

// HLVDCON.HLVDL<3:0>: selects one of 15 internal fixed trip points (0-14).
// Actual trip voltages are in the "Electrical Characteristics" chapter of
// the PIC32MZ-DA family data sheet and are not repeated here.
// 15 routes the comparator to the external LVDIN pin instead, which is not
// wired up on this board.
#define HLVD_TRIP_POINT_EXTERNAL_LVDIN  15

// This function disables the module, applies the requested trip point and
// direction, then re-enables the module. Per the HLVD setup procedure,
// settings may only be changed while the module is off.
// Band gap stabilization (and therefore a trustworthy HLEVT reading) is
// not instantaneous after this call -- poll hlvdIsReady() before trusting
// hlvdCheckEvent().
void hlvdInitialize(uint8_t trip_point, hlvd_direction_t direction);

// This function disables the HLVD module
void hlvdDisable(void);

// This function returns 1 if the band gap reference has stabilized
// (HLVDCONbits.BGVST), meaning HLEVT can be trusted, 0 otherwise
uint8_t hlvdIsReady(void);

// This function returns the live HLVD event status bit (HLEVT). This
// tracks the comparator in real time and clears itself if VDD moves back
// away from the trip point -- treat it as a level, not a latch.
uint8_t hlvdCheckEvent(void);

// This function reads the latched HLVD flag (RNMICONbits.LVD), clears it,
// and returns the value it had before clearing. See the file summary --
// this flag does not indicate a reset or NMI occurred, only that HLVD
// tripped at some point since it was last cleared.
uint8_t hlvdCheckAndClearLatchedEvent(void);

// This function prints the current HLVD configuration and status
void printHLVDStatus(void);

#endif /* _HLVD_H */

/* *****************************************************************************
 End of File
 */
