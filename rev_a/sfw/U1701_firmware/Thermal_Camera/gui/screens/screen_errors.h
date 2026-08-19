/*******************************************************************************
  Diagnostics GUI Screen

  File Name:
    screen_errors.h

  Summary:
    The panel-side equivalent of the "Error Status?" and "Clear Errors" USB
    UART commands: which faults are latched, and the one control that clears
    them.

  Description:
    Reached from the main menu. Same furniture as the other screens, plus a
    bottom action bar -- the panel scrolls, and a control that scrolls off
    the list is a control the user cannot find:

      +--------------------------------------------------+
      | < | Diagnostics                       08-18-2026 |
      |                                         14:32:07 |
      | +----------------------------------------------+ |
      | | 3 of 81 faults latched                       | |
      | |                                              | |
      | | - FLIR Lepton VoSPI Desync                   | |
      | | - MCP9804 (U1201) I2C                        | |
      | | - BQ27441 (U1601) Configuration              | |
      | +----------------------------------------------+ |
      |                                  [ Clear Errors ]|
      +--------------------------------------------------+

    WHY THIS SCREEN EXISTS. The system status screen already counts the
    latched flags, but a count does not say WHICH, and error_handler is
    declared __attribute__((persistent)) -- the flags survive a reset, so
    until now the only way to read them out or clear them was to attach a
    serial terminal. This is the whole of that workflow on the panel.

    WHAT IT READS. error_handler.flag_array[] and error_handler_flag_names[]
    (application/error_handler.h) are parallel arrays built from the same
    X-macro lists, so this screen is a walk over an existing table rather
    than a hand-maintained list that could drift from the flags. That covers
    both the base flags and the two generated per I2C device.

    ONE LABEL, NOT A ROW PER FLAG. There are ERROR_HANDLER_NUM_FLAGS of them
    (81 at the time of writing, and it grows with every I2C device), while
    the number actually latched is normally zero and realistically a handful.
    Building a row per flag would put ~81 LVGL objects on the splash fast
    path (GUI_Initialize() runs there -- see main.c) to keep almost all of
    them hidden. Instead the list is a single multi-line label whose text is
    rebuilt only when the set of latched flags changes, which is also what
    keeps the 500ms refresh from re-rendering the panel over the FLIR video
    path for no reason.

    CLEARING. clearErrorHandler() throws away every latched fault, which is
    diagnostic history that cannot be recovered, so it goes behind the same
    confirm overlay the Saved Images screen uses for a delete. The flags
    clear immediately; note that some of them (the "_init_error" ones) only
    ever get set during boot, so a cleared flag does not come back until the
    next reset even if the underlying fault is still there -- which is
    exactly the "Clear Errors then Reset" sequence the UART console has
    always needed.
*******************************************************************************/

#ifndef SCREEN_ERRORS_H
#define SCREEN_ERRORS_H

#include "gui/lvgl/lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// Builds the diagnostics screen. Returns the screen object, or NULL on
// failure.
lv_obj_t *ScreenErrors_Create(void);

// Re-reads the error handler and rewrites the list if the set of latched
// flags has changed. Safe to call if Create() failed.
void ScreenErrors_Refresh(void);

// Closes the "clear all faults?" prompt if it is open. gui.c calls this from
// every screen change, for the same reason it dismisses the saved-image
// viewer: a question left on screen when the shutter button navigates away
// should not still be waiting when the screen is next opened. No-op when the
// prompt is closed.
void ScreenErrors_DismissPrompt(void);

#ifdef __cplusplus
}
#endif

#endif /* SCREEN_ERRORS_H */
