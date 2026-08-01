/* ************************************************************************** */
/** Pushbutton Interrupt Handling

  @File Name
    pushbuttons.h

  @Summary
    Change-notification interrupt handling for the Power and Shutter
    pushbuttons.

  @Description
    Both buttons are wired to Port A (SHUTTER on RA9, POWER on RA10 --
    pin_macros.h) and are active high. This uses Port A's change-
    notification (CN) interrupt, which is shared across all 16 Port A pins;
    only RA9/RA10 have CN enabled here. Mismatch-mode CN (the mode used
    below) only reports "something on this port changed" -- not which pin
    or which direction -- so the ISR tracks each pin's own last known level
    itself to report presses (low-to-high transitions) without misfiring on
    an already-held button when the OTHER one is what changed.
 */
/* ************************************************************************** */

#ifndef _PUSHBUTTONS_H
#define _PUSHBUTTONS_H

#include <stdbool.h>
#include <stdint.h>
#include <xc.h>
#include <sys/attribs.h>

// Set by the ISR below on each SHUTTER press (low-to-high edge), for
// main-loop code to act on -- the ISR runs at IPL3 and must not do the
// work itself. Currently unconsumed (the shutter button no longer drives
// GUI screen switching -- that will eventually be capacitive-touch driven
// instead). Whoever adds a consumer should clear it there: it's meant to
// have only one, since a second reader would silently steal presses.
extern volatile uint8_t shutter_button_press_event;

// Enables Port A's change-notification interrupt for the SHUTTER (RA9) and
// POWER (RA10) pins. Must run after gpioInitialize() (which configures
// RA9/RA10 as digital inputs) and interruptControllerInitialize().
bool pushbuttonsInitialize(void);

// Port A change-notification interrupt service routine. Fires on any
// change on RA9 (SHUTTER) or RA10 (POWER); prints a message when either
// transitions low-to-high (a press).
void __ISR(_CHANGE_NOTICE_A_VECTOR, IPL3SRS) portAChangeNoticeISR(void);

#endif /* _PUSHBUTTONS_H */

/* *****************************************************************************
 End of File
 */
