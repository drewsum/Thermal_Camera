/*******************************************************************************
  LCD Brightness GUI Screen

  File Name:
    screen_brightness.h

  Summary:
    Sets the LCD backlight brightness with a slider, reached from the main
    menu.

  Description:
    Layout: the shared header (gui/screens/screen_common.c) with a back
    button returning to the menu, and a body panel holding the current
    percentage in 20pt, a full-width horizontal slider, and the range
    end-labels beneath it.

    The slider drives application/backlight_pwm.c directly on every
    LV_EVENT_VALUE_CHANGED -- that is, continuously while the finger is
    moving, not on release -- so the panel brightens and dims under the
    finger. BacklightPWM_SetBrightness() is two SFR writes, so doing it at
    the touch report rate is free.

    Refresh() re-reads BacklightPWM_GetBrightness() and moves the slider to
    match, which also picks up a brightness changed from somewhere else (the
    "LCD Brightness:" UART command) while this screen is showing -- the same
    arrangement the palette screen has with its UART command. It deliberately
    does nothing while the slider is being dragged, so the 500ms refresh
    can't fight the finger.

    Range: SCREEN_BRIGHTNESS_MIN_PERCENT to 100, NOT 0 to 100. 0% is a fully
    dark panel, and this is the one screen from which the user could not then
    see how to undo it -- the backlight is the only thing lighting the
    control that turns the backlight back up. The UART command still has the
    full 0-100 range for a deliberate blackout.

    Like every other screen here it is built once at boot and kept (see the
    screen table in gui/gui.c), so switching to it is a pointer swap.
*******************************************************************************/

#ifndef SCREEN_BRIGHTNESS_H
#define SCREEN_BRIGHTNESS_H

#include "gui/lvgl/lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// Lowest brightness the slider can reach. See the range note above: this is
// a usability floor, not a hardware one -- BacklightPWM_SetBrightness()
// accepts 0.
#define SCREEN_BRIGHTNESS_MIN_PERCENT   5

// Builds the LCD brightness screen. Returns the screen object, or NULL on
// failure.
lv_obj_t *ScreenBrightness_Create(void);

// Re-reads the live backlight setting and moves the slider and its readout
// to match, unless the slider is currently being dragged. Safe to call if
// Create() failed.
void ScreenBrightness_Refresh(void);

#ifdef __cplusplus
}
#endif

#endif /* SCREEN_BRIGHTNESS_H */
