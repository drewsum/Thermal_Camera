
#include "application/pushbuttons.h"

#include <stdio.h>

#include "core/32mzda_interrupt_control.h"
#include "gpio/pin_macros.h"
#include "usb_uart/terminal_control.h"
#include "sdhc/device_driver/sd_card.h"

// Last known level of each button, so the ISR can tell an actual press
// (low-to-high transition) from merely re-reading an already-held button
// when the OTHER pin is what changed -- see pushbuttons.h for why this is
// necessary in mismatch-mode CN.
static volatile bool shutterPressed = false;
static volatile bool powerPressed = false;

// Last known level of the SD card-detect pin (RA0), tracked for the same
// mismatch-mode reason as the buttons above. RA0's CN lives in this ISR
// because Port A change-notice is a single shared vector -- the SD stack
// can't have its own Port A CN handler without conflicting with this one
// (this is why sd_card.c originally polled card-detect instead). On any
// edge, either direction, the ISR only sets sd_card_hotswap_event -- the
// debounce and the (slow, printf-heavy) mount/unmount work happen in
// SDFileIO_HotSwapTasks() from the main loop, never here at IPL3.
static volatile bool cardDetectLevel = false;

bool pushbuttonsInitialize(void) {

    // Disable while (re)configuring
    disableInterrupt(porta_input_change_interrupt);

    // Seed the "last known" state so the first real interrupt after this
    // doesn't misreport an already-held button as a fresh press
    shutterPressed = (CAP_TOUCH_SHUTTER_PIN != 0);
    powerPressed = (CAP_TOUCH_POWER_PIN != 0);
    cardDetectLevel = (SD_CARD_DETECT_PIN != 0);

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
    CNENAbits.CNIEA0 = 1;    // SD card detect (SD_CARD_DETECT_PIN) -- see cardDetectLevel

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
    bool cardDetectNow = (SD_CARD_DETECT_PIN != 0);

    if (cardDetectNow != cardDetectLevel) {
        cardDetectLevel = cardDetectNow;
        sd_card_hotswap_event = 1;
    }

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
