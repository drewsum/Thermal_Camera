/*******************************************************************************
  Thermal Palette GUI Screen

  File Name:
    screen_palette.h

  Summary:
    Picks the FLIR false-colour palette, reached from the main menu.

  Description:
    One tappable row per palette in FLIR_PALETTE (application/flir/
    flir_process.h), each showing the palette's name, a swatch painted from
    that palette's own control points, and a check mark on the active one.
    The list is built from FLIR_PALETTE_COUNT and FLIRProcess_PaletteName(),
    so a palette added to the driver appears here with no edit to this file.

    Selecting a row applies the palette immediately and stays on the screen,
    rather than navigating back: the panel is translucent, the change is
    visible in the live video behind it, and trying several in a row is the
    normal way this gets used. The home screen's palette scale picks the
    change up on its next refresh, which the back-navigation triggers
    anyway.

    The swatches are real gradients (lv_grad_dsc_t) built from the same
    FLIRProcess_GetPalettePoints() data the render LUT uses, so what is on
    screen cannot drift from what the video actually looks like -- the same
    approach as the home screen's vertical scale, turned on its side.
*******************************************************************************/

#ifndef SCREEN_PALETTE_H
#define SCREEN_PALETTE_H

#include "gui/lvgl/lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// Builds the palette screen. Returns the screen object, or NULL on failure.
lv_obj_t *ScreenPalette_Create(void);

// Re-reads the active palette and moves the check mark. Also catches a
// palette changed from somewhere else -- the "FLIR Palette:" UART command
// -- while this screen is showing. Safe to call if Create() failed.
void ScreenPalette_Refresh(void);

#ifdef __cplusplus
}
#endif

#endif /* SCREEN_PALETTE_H */
