
#include "application/pushbuttons.h"

#include <stdio.h>

#include "core/32mzda_interrupt_control.h"
#include "gpio/pin_macros.h"
#include "usb_uart/terminal_control.h"

// Last known level of each button, so the ISR can tell an actual press
// (low-to-high transition) from merely re-reading an already-held button
// when the OTHER pin is what changed -- see pushbuttons.h for why this is
// necessary in mismatch-mode CN.
static volatile bool shutterPressed = false;
static volatile bool powerPressed = false;

bool pushbuttonsInitialize(void) {

    // Disable while (re)configuring
    disableInterrupt(porta_input_change_interrupt);

    // Seed the "last known" state so the first real interrupt after this
    // doesn't misreport an already-held button as a fresh press
    shutterPressed = (CAP_TOUCH_SHUTTER_PIN != 0);
    powerPressed = (CAP_TOUCH_POWER_PIN != 0);

    // Legacy "mismatch" mode (EDGEDETECT = 0): the CNIEAx bits below fire
    // on any change (either direction) on that pin; edge direction is
    // recovered above by tracking each pin's last known level ourselves.
    CNCONAbits.EDGEDETECT = 0;
    CNCONAbits.ON = 1;

    // Enable change notice on the SHUTTER and POWER pins only -- every
    // other Port A pin is left disabled here so it can't trigger this ISR.
    // See pin_macros.h: RA9/RA10 are cross-mapped to match the (swapped)
    // board silkscreen, so RA9 = POWER and RA10 = SHUTTER here.
    CNENAbits.CNIEA9 = 1;    // POWER (CAP_TOUCH_POWER_PIN)
    CNENAbits.CNIEA10 = 1;   // SHUTTER (CAP_TOUCH_SHUTTER_PIN)

    setInterruptPriority(porta_input_change_interrupt, 3);
    setInterruptSubpriority(porta_input_change_interrupt, 0);

    clearInterruptFlag(porta_input_change_interrupt);
    enableInterrupt(porta_input_change_interrupt);

    return (CNCONAbits.ON == 1);

}

void __ISR(_CHANGE_NOTICE_A_VECTOR, IPL3SRS) portAChangeNoticeISR(void) {

    // Reading RA9/RA10 (via these macros) is what clears Port A's CN
    // mismatch condition and re-arms it for the next change
    bool shutterNow = (CAP_TOUCH_SHUTTER_PIN != 0);
    bool powerNow = (CAP_TOUCH_POWER_PIN != 0);

    if (shutterNow && !shutterPressed) {
        terminalTextAttributes(MAGENTA_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("Shutter button pressed\r\n");
        terminalTextAttributesReset();
    }

    if (powerNow && !powerPressed) {
        terminalTextAttributes(MAGENTA_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("Power button pressed\r\n");
        terminalTextAttributesReset();
    }

    shutterPressed = shutterNow;
    powerPressed = powerNow;

    clearInterruptFlag(porta_input_change_interrupt);

}
