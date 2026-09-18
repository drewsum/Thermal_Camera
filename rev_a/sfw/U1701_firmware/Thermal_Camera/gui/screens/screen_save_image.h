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
      | < | Save Image                      <date/time>  |  header + back
      +--------------------------------------------------+
      |                                                  |
      |            [ Save this image? ]                  |  centered prompt
      |            [   <status line>  ]                  |  chip labels
      |                                                  |
      +--------------------------------------------------+
      | [Save to SD]                          [ Cancel ] |  footer buttons
      +--------------------------------------------------+

    The footer entries are tappable: Save commits the write
    (StillCapture_ConfirmSave()), Cancel discards the still and returns to
    live video (StillCapture_Resume()). The back button in the header is a
    second route to Cancel -- identical action.

    They are built by Screen_CreateButton() from base objects rather than
    lv_button, because gui/lv_conf.h leaves LV_USE_BUTTON = 0 to save flash;
    base objects are clickable in LVGL v9 regardless.

    The controls follow the capture state (see ScreenSaveImage_Refresh()):
    both choices while the prompt is live, neither while the encode runs, and
    a single "Done" once the outcome is known -- "Cancel" after a completed
    write would imply an undo that does not exist.

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
