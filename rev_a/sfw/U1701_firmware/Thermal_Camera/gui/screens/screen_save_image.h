/*******************************************************************************
  Save-Image GUI Screen

  File Name:
    screen_save_image.h

  Summary:
    The prompt shown over a frozen thermal frame after the shutter is pressed:
    "Save this image?", with a two-choice footer.

  Description:
    Layout, over the still that application/still_capture.c has left on GLCD
    Layer 0 (the screen itself is transparent, so the captured image IS the
    background -- there is deliberately no full-screen panel here, unlike the
    FLIR error screen):

      +--------------------------------------------------+
      | Save Image                          <date/time>  |  shared header
      +--------------------------------------------------+
      |                                                  |
      |            [ Save this image? ]                  |  centered prompt
      |            [   <status line>  ]                  |  chip labels
      |                                                  |
      +--------------------------------------------------+
      | Save to SD                              Cancel   |  footer bar
      +--------------------------------------------------+

    The two footer entries are LABELS, not buttons: gui/lv_conf.h has
    LV_USE_BUTTON = 0 (the enabled widget set is base object, label and bar),
    and there is no input device to press them with anyway -- the panel's
    GT911 touch controller is not wired up. They stake out the hit targets and
    name the choices; still_capture.c saves unconditionally until touch lands.
    See still_capture.h for how the two are meant to be joined up.

    Built at init alongside every other screen but shown on demand, like
    gui/screens/flir_error_screen.c -- it is not part of the GUI_NextScreen()
    cycle.
*******************************************************************************/

#ifndef SCREEN_SAVE_IMAGE_H
#define SCREEN_SAVE_IMAGE_H

#include "gui/lvgl/lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// Builds the screen. Returns NULL on failure, like every other Create().
lv_obj_t *ScreenSaveImage_Create(void);

// Re-reads the clock and the capture's state/filename into the status line.
// Called on the 500ms GUI refresh while this screen is showing, and directly
// by still_capture.c the moment a save finishes. Safe to call on a screen
// whose creation failed.
void ScreenSaveImage_Refresh(void);

#ifdef __cplusplus
}
#endif

#endif /* SCREEN_SAVE_IMAGE_H */
