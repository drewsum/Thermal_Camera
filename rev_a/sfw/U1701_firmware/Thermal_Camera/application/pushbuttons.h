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

    The ISR does nothing but latch edges. It must not print: printf() from
    here reaches the UART through _mon_putc(), whose buffer bookkeeping is
    only guarded against the TX DMA interrupt (IPL1), not against being
    preempted by this ISR at IPL3. A press landing inside usbUartTxDmaISR()
    -- between its memset() of the TX buffer and its reset of
    usb_uart_tx_buffer_head -- had its characters appended and then
    immediately thrown away, which is why button messages used to show up
    only sometimes and looked like a multi-second debounce. All printing
    now happens in pushbuttonsTasks() from the main loop.
 */
/* ************************************************************************** */

#ifndef _PUSHBUTTONS_H
#define _PUSHBUTTONS_H

#include <stdbool.h>
#include <stdint.h>
#include <xc.h>
#include <sys/attribs.h>

// Set by the debounce logic below on each accepted SHUTTER press (low-to-
// high edge), for main-loop code to act on. Currently unconsumed (the
// shutter button no longer drives GUI screen switching -- that will
// eventually be capacitive-touch driven instead). Whoever adds a consumer
// should clear it there: it's meant to have only one, since a second
// reader would silently steal presses.
extern volatile uint8_t shutter_button_press_event;

// Enables Port A's change-notification interrupt for the SHUTTER (RA9) and
// POWER (RA10) pins. Must run after gpioInitialize() (which configures
// RA9/RA10 as digital inputs) and interruptControllerInitialize().
bool pushbuttonsInitialize(void);

// Prints the button transitions the ISR latched, and re-syncs each button
// against its live pin level in case its settling edge landed inside the
// debounce lockout. Call every pass of the main loop; it costs two flag
// checks and a PORTA read when nothing has happened.
void pushbuttonsTasks(void);

// Live debounced state of each button, for anything that needs "is it held
// right now" rather than the edge events above.
bool pushbuttonsShutterHeld(void);
bool pushbuttonsPowerHeld(void);

// Port A change-notification interrupt service routine. Fires on any
// change on RA9 (SHUTTER), RA10 (POWER) or RA0 (SD card detect), and only
// latches what changed -- see the file header for why it prints nothing.
void __ISR(_CHANGE_NOTICE_A_VECTOR, IPL3SRS) portAChangeNoticeISR(void);

#endif /* _PUSHBUTTONS_H */

/* *****************************************************************************
 End of File
 */
