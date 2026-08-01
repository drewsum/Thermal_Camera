/*******************************************************************************
  FLIR Error GUI Screen

  File Name:
    flir_error_screen.h

  Summary:
    Shown in place of the home screen when the FLIR Lepton fails to come up
    at boot -- there is no live thermal video to display behind the GUI, so
    this tells the operator that plainly instead of leaving Layer 0 black
    with no explanation.

  Description:
    Same top bar/clock furniture as every other screen, with a centered
    panel instead of a body:

      +--------------------------------------------------+
      | Thermal Camera                        2026-07-21 |
      |                                         14:32:07 |
      |                                                    |
      |              Thermal Camera Unavailable           |
      |                                                    |
      |             FLIR Lepton Boot Timeout               |
      |                                                    |
      |     See 'FLIR Status?' on the USB console for      |
      |                    details.                        |
      |                                                    |
      +--------------------------------------------------+

    The reason line reads whichever of the three flir_*_error/timeout flags
    (application/error_handler.h) is latched -- the same flags flir.c itself
    sets when FLIR_Tasks() drops to FLIR_STATE_FAULT -- so it never has to
    duplicate flir.c's own fault classification.

    gui.c owns when this screen is shown: it is built at GUI_Initialize() like
    every other screen, but only loaded on demand via GUI_ShowFlirErrorScreen()
    (main.c calls that when FLIR_WaitUntilReady() returns false), not cycled
    through by GUI_NextScreen().
*******************************************************************************/

#ifndef FLIR_ERROR_SCREEN_H
#define FLIR_ERROR_SCREEN_H

#include "gui/lvgl/lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// Builds the screen and returns it, without showing it -- gui.c owns which
// screen is loaded. Returns NULL if a widget couldn't be created.
lv_obj_t *FlirErrorScreen_Create(void);

// Re-reads the clock and the latched flir_* fault flag into the labels.
// Called from GUI_Tasks() every 500ms while this screen is the active one;
// safe to call more often, and before/without Create() having succeeded.
void FlirErrorScreen_Refresh(void);

#ifdef __cplusplus
}
#endif

#endif /* FLIR_ERROR_SCREEN_H */
