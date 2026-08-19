/* ************************************************************************** */
/** Thermal Legend Renderer

  @File Name
    image_legend.h

  @Summary
    Draws the palette legend -- the gradient scale strip and its two
    temperature labels -- into an RGB888 frame buffer, so a saved PNG can
    carry the same legend the live view shows.

  @Description
    The legend the user sees on the home screen lives on GLCD Layer 1, the
    ARGB8888 LVGL overlay the controller blends over the video. Saved images
    come off Layer 0, so the legend was never in them -- which is why this
    module exists.

    Copying the Layer 1 pixels instead would have been shorter, but it only
    works when the home screen happens to be the one on the panel: the
    shutter is live on every screen, so a capture taken with the menu or the
    palette picker up would bake THAT into the image. This redraws the legend
    from its sources (the active palette's LUT and the AGC window, both from
    flir_process.c) so what lands in the file does not depend on which screen
    the user was looking at.

    The geometry, opacity and font below are the single definition of where
    the legend goes: gui/screens/screen_home.c builds its LVGL widgets from
    these same macros, so the drawn copy and the live one cannot drift apart.
    Everything is reproduced -- the 60% fill alpha, the 1px outline, the
    chips behind the labels, the font's kerning and antialiasing -- so the
    baked legend matches the panel rather than approximating it.

    Two consequences worth knowing:

      - The blends produce colors that are not in the 256-entry palette, so
        an image saved with the legend on will usually NOT palettize. lodepng
        falls back to RGB888 (see application/image_saver.h), which makes the
        file a few times larger and the encode somewhat longer. That is the
        price of the legend looking like the legend.

      - Drawing is clipped to the bounding box declared below as well as to
        the buffer. application/still_capture.c restores exactly that box
        from the untouched video buffer when the user clears the "Include
        legend" checkbox, so a pixel painted outside it could never be taken
        back off again.
 */
/* ************************************************************************** */

#ifndef IMAGE_LEGEND_H
#define IMAGE_LEGEND_H

#include <stdint.h>

// For SCREEN_BAR_HEIGHT_PX and SCREEN_BAR_OPACITY: the legend is positioned
// below the header bar and painted at the same alpha as the rest of the
// screen furniture, so both belong to that header rather than being restated
// here. Pulls in LVGL, which this module needs anyway for the label font.
#include "gui/screens/screen_common.h"

#ifdef __cplusplus
extern "C" {
#endif

// Palette scale geometry: a vertical strip in the middle band between the
// two bars, with a temperature chip just above and below it. Shared with
// gui/screens/screen_home.c, which builds the live version from these.
#define IMAGE_LEGEND_SCALE_WIDTH_PX     16
#define IMAGE_LEGEND_SCALE_X_PX         10
#define IMAGE_LEGEND_SCALE_TOP_Y_PX     (SCREEN_BAR_HEIGHT_PX + 24)
#define IMAGE_LEGEND_SCALE_HEIGHT_PX    112
#define IMAGE_LEGEND_LABEL_GAP_PX       2

// The label chips: the font, and the padding between the text and the edge
// of the translucent backing behind it.
#define IMAGE_LEGEND_FONT               (&lv_font_montserrat_14)
#define IMAGE_LEGEND_LABEL_PAD_HOR_PX   3
#define IMAGE_LEGEND_LABEL_PAD_VER_PX   1

// Every pixel ImageLegend_DrawRGB888() may touch, in frame-buffer
// coordinates. Wider than the strip because the chips are centered on it and
// are wider than it is (they hang off both sides, and off the left edge of
// the screen), and taller by more than a chip at each end.
//
// This is the contract with application/still_capture.c: restoring this box
// from the original image removes the legend completely.
#define IMAGE_LEGEND_BOX_X_PX           0
#define IMAGE_LEGEND_BOX_WIDTH_PX       72
#define IMAGE_LEGEND_BOX_Y_PX           (IMAGE_LEGEND_SCALE_TOP_Y_PX - 24)
#define IMAGE_LEGEND_BOX_HEIGHT_PX      (IMAGE_LEGEND_SCALE_HEIGHT_PX + 48)

// Left edge of a label chip `chipWidth` pixels wide: centered on the scale
// strip, then pulled back onto the panel if that would hang it off the left
// edge -- which it does for every real temperature, the chips being three
// times the width of the strip they sit over.
//
// Shared with gui/screens/screen_home.c, which positions the live chips with
// it. That is the whole point of it being a function: LVGL's lv_obj_align_to()
// is a ONE-SHOT, so a chip aligned at creation is centered on the placeholder
// text it was created with and then grows rightwards from that fixed x as the
// temperature is written into it. Centering the drawn copy on its REAL text
// therefore put it several pixels left of the live one, far enough to lose a
// digit off the edge of the frame. Both call this instead.
int32_t ImageLegend_ChipX(int32_t chipWidth);

// Draws the legend into `width` x `height` packed RGB888 pixels at `rgb888`,
// blending over what is already there exactly as the GLCD Controller blends
// Layer 1 over Layer 0. The buffer may be uncached DDR2 (the still capture's
// frozen image is).
//
// Reads the palette and the AGC window as they are RIGHT NOW, so call it
// while the frame it is decorating is still the current one -- with a still
// held, VoSPI is stopped and neither can move, which is what makes the
// legend on a saved image agree with the image under it.
//
// No-op on a NULL buffer, or on one too small to hold the box above.
void ImageLegend_DrawRGB888(void *rgb888, uint32_t width, uint32_t height);

#ifdef __cplusplus
}
#endif

#endif /* IMAGE_LEGEND_H */

/* *****************************************************************************
 End of File
 */
