
#include "application/pushbuttons.h"

#include <stdio.h>

#include "core/32mzda_interrupt_control.h"
#include "core/device_control.h"
#include "gpio/pin_macros.h"
#include "usb_uart/terminal_control.h"
#include "sdhc/device_driver/sd_card.h"

// CP0 Count runs at SYSCLK/2, so this many ticks per millisecond. CP0 is
// used instead of GUI_GetTickMs() because the ISR needs a timestamp and
// GUI_GetTickMs() accumulates state (it is not reentrant). Count is 32
// bits and wraps roughly every 43 seconds, which the unsigned subtractions
// below ride through correctly.
#define PUSHBUTTON_TICKS_PER_MS     ((uint32_t)(SYSCLK_INT / 2u) / 1000u)

// Lockout-style debounce: an edge is acted on the instant it arrives, and
// then further edges on that same button are ignored for this long. The
// press is therefore reported with interrupt latency (microseconds), not
// after a settling delay -- the lockout only suppresses the contact bounce
// that follows. Keep it well under the ~150ms a fast double-tap takes so
// the second tap is never swallowed.
#define PUSHBUTTON_DEBOUNCE_MS      25u
#define PUSHBUTTON_DEBOUNCE_TICKS   (PUSHBUTTON_DEBOUNCE_MS * PUSHBUTTON_TICKS_PER_MS)

// Per-button debounce state. `level` is the last level the ISR saw, which
// is what makes edge direction recoverable in mismatch-mode CN (see
// pushbuttons.h); `reported` is the last level actually announced, which
// can lag `level` by one lockout when a button settles mid-lockout --
// pushbuttonsTasks() reconciles the two.
typedef struct {
    volatile bool level;
    volatile bool reported;
    volatile uint32_t lastAcceptedTicks;
    volatile uint8_t press_event;
    volatile uint8_t release_event;
} pushbutton_t;

static pushbutton_t shutter_button;
static pushbutton_t power_button;

// Last known level of the SD card-detect pin (RA0), tracked for the same
// mismatch-mode reason as the buttons above. RA0's CN lives in this ISR
// because Port A change-notice is a single shared vector -- the SD stack
// can't have its own Port A CN handler without conflicting with this one
// (this is why sd_card.c originally polled card-detect instead). On any
// edge, either direction, the ISR only sets sd_card_hotswap_event -- the
// debounce and the (slow, printf-heavy) mount/unmount work happen in
// SDFileIO_HotSwapTasks() from the main loop, never here at IPL3.
static volatile bool cardDetectLevel = false;

// Declared in pushbuttons.h -- see there for the single-consumer rule
volatile uint8_t shutter_button_press_event = 0;
volatile uint8_t shutter_button_capture_request = 0;
volatile uint8_t power_button_sleep_request = 0;

// Whether a press has been seen on each button since boot (or since that
// button's last completed gesture). This is what makes both gestures
// press-THEN-release rather than "any release".
//
// It matters most for POWER: the board wakes from sleep on the press edge and
// resets, and pushbuttonInit() below then seeds the button as held because
// the user's finger is still on the pad. The release that follows must not be
// read as a fresh gesture, or the board would drop straight back into sleep
// the moment it was woken. SHUTTER has no equivalent hazard but follows the
// same rule so the two behave alike.
static bool power_press_seen = false;
static bool shutter_press_seen = false;

// Accepts a settled level change for one button: latches the event for
// pushbuttonsTasks() and opens a fresh lockout. Callers must have already
// confirmed the lockout expired. Runs from both the ISR and (with the CN
// interrupt masked) the main loop. Returns true if this was a press, so
// the caller can raise the public shutter event only on a real press.
static inline bool pushbuttonAccept(pushbutton_t *button, bool level, uint32_t nowTicks) {

    button->lastAcceptedTicks = nowTicks;
    button->reported = level;

    if (level) button->press_event = 1;
    else button->release_event = 1;

    return level;

}

// Common ISR-side handling for one button: track the new level always (the
// mismatch-mode bookkeeping depends on it being current), but only raise an
// event if this button is out of its debounce lockout.
static inline bool pushbuttonEdge(pushbutton_t *button, bool level, uint32_t nowTicks) {

    if (level == button->level) return false;

    button->level = level;

    if ((uint32_t)(nowTicks - button->lastAcceptedTicks) < PUSHBUTTON_DEBOUNCE_TICKS) return false;

    return pushbuttonAccept(button, level, nowTicks);

}

static void pushbuttonInit(pushbutton_t *button, bool level) {

    button->level = level;
    button->reported = level;
    // Backdate so the very first real edge is accepted immediately rather
    // than falling inside a lockout that started at boot
    button->lastAcceptedTicks = _CP0_GET_COUNT() - PUSHBUTTON_DEBOUNCE_TICKS;
    button->press_event = 0;
    button->release_event = 0;

}

bool pushbuttonsInitialize(void) {

    // Disable while (re)configuring
    disableInterrupt(porta_input_change_interrupt);

    // Seed the "last known" state so the first real interrupt after this
    // doesn't misreport an already-held button as a fresh press
    pushbuttonInit(&shutter_button, (CAP_TOUCH_SHUTTER_PIN != 0));
    pushbuttonInit(&power_button, (CAP_TOUCH_POWER_PIN != 0));
    cardDetectLevel = (SD_CARD_DETECT_PIN != 0);

    shutter_button_press_event = 0;
    shutter_button_capture_request = 0;
    power_button_sleep_request = 0;
    power_press_seen = false;
    shutter_press_seen = false;

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

    uint32_t nowTicks = _CP0_GET_COUNT();

    // Reading RA9/RA10 (via these macros) is what clears Port A's CN
    // mismatch condition and re-arms it for the next change
    bool shutterNow = (CAP_TOUCH_SHUTTER_PIN != 0);
    bool powerNow = (CAP_TOUCH_POWER_PIN != 0);
    bool cardDetectNow = (SD_CARD_DETECT_PIN != 0);

    if (cardDetectNow != cardDetectLevel) {
        cardDetectLevel = cardDetectNow;
        sd_card_hotswap_event = 1;
    }

    // Latch the press for the main loop -- nothing that acts on it belongs
    // at IPL3. shutter_button_press_event is currently unconsumed; see
    // pushbuttons.h.
    if (pushbuttonEdge(&shutter_button, shutterNow, nowTicks)) shutter_button_press_event = 1;

    pushbuttonEdge(&power_button, powerNow, nowTicks);

    clearInterruptFlag(porta_input_change_interrupt);

}

// Re-reads the live pin in case the button's settling edge landed inside
// the debounce lockout and was therefore swallowed by pushbuttonEdge()
// (press accepted -> bounce -> genuine release all inside 25ms would
// otherwise leave this module believing the button is still held). The CN
// interrupt is masked across the read-modify-write so the ISR can't be
// updating the same fields. Masking cannot lose an edge: the mismatch
// condition still sets the CN interrupt flag while masked, and the pin
// value stored below is the one this read observed, so any change after it
// is still a mismatch the ISR will see when it runs.
static bool pushbuttonResync(pushbutton_t *button, bool live, uint32_t nowTicks) {

    if (live == button->reported) return false;

    if ((uint32_t)(nowTicks - button->lastAcceptedTicks) < PUSHBUTTON_DEBOUNCE_TICKS) return false;

    button->level = live;

    return pushbuttonAccept(button, live, nowTicks);

}

void pushbuttonsTasks(void) {

    bool shutterLive;
    bool powerLive;
    uint32_t nowTicks;

    disableInterrupt(porta_input_change_interrupt);

    nowTicks = _CP0_GET_COUNT();
    shutterLive = (CAP_TOUCH_SHUTTER_PIN != 0);
    powerLive = (CAP_TOUCH_POWER_PIN != 0);

    if (pushbuttonResync(&shutter_button, shutterLive, nowTicks)) shutter_button_press_event = 1;

    pushbuttonResync(&power_button, powerLive, nowTicks);

    enableInterrupt(porta_input_change_interrupt);

    // Clear each flag before printing: a genuine second transition arriving
    // during the (slow) printf below re-latches it and gets reported on the
    // next pass instead of being overwritten and lost.
    if (shutter_button.press_event) {
        shutter_button.press_event = 0;
        shutter_press_seen = true;
        terminalTextAttributes(MAGENTA_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("Shutter button pressed\r\n");
        terminalTextAttributesReset();
    }

    if (shutter_button.release_event) {
        shutter_button.release_event = 0;

        // Press-then-release completes the capture gesture. Only the flag is
        // raised here: the capture freezes the display, writes a PNG and
        // switches screens, none of which belongs in this module. main.c
        // acts on it.
        if (shutter_press_seen) {
            shutter_press_seen = false;
            shutter_button_capture_request = 1;
        }

        terminalTextAttributes(MAGENTA_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("Shutter button released\r\n");
        terminalTextAttributesReset();
    }

    if (power_button.press_event) {
        power_button.press_event = 0;
        power_press_seen = true;
        terminalTextAttributes(MAGENTA_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("Power button pressed\r\n");
        terminalTextAttributesReset();
    }

    if (power_button.release_event) {
        power_button.release_event = 0;

        // Press-then-release completes the sleep gesture. Only the flag is
        // raised here: enterLowPowerSleep() unmounts filesystems, waits on
        // I2C and never returns, none of which belongs inside the button
        // module's tasks function. main.c acts on it.
        if (power_press_seen) {
            power_press_seen = false;
            power_button_sleep_request = 1;
        }

        terminalTextAttributes(MAGENTA_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("Power button released\r\n");
        terminalTextAttributesReset();
    }

}

bool pushbuttonsShutterHeld(void) {

    return shutter_button.reported;

}

bool pushbuttonsPowerHeld(void) {

    return power_button.reported;

}
