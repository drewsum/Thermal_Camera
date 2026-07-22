/*******************************************************************************
  Shared Screen Furniture

  File Name:
    screen_common.h

  Summary:
    The look every GUI screen shares: a transparent screen background, the
    translucent bars, and the header with the clock in it.

  Description:
    Lives here rather than being copied into each screen so the screens
    can't drift apart visually, and so the one piece of live data every
    screen shows -- the RTCC clock in the top-right corner -- is read and
    formatted in exactly one place.

    Everything is deliberately built from the three widgets gui/lv_conf.h
    enables (base object, label, bar). If a screen needs another widget,
    turn it on there first and expect to pay for it in program flash.
*******************************************************************************/

#ifndef SCREEN_COMMON_H
#define SCREEN_COMMON_H

#include <stdbool.h>

#include "gui/lvgl/lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// Height of the top/bottom bars, and the inset of their contents. Shared so
// a screen's body can size itself against them (the usable middle is
// 240 - 2*SCREEN_BAR_HEIGHT_PX tall).
#define SCREEN_BAR_HEIGHT_PX     40
#define SCREEN_BAR_PADDING_PX    8

// How opaque the bars and panels are over the GLCD Layer 0 image. Enough to
// keep white text readable over an arbitrary photo, transparent enough that
// the hardware blend is visibly doing something.
#define SCREEN_BAR_OPACITY       LV_OPA_60

// A screen's header: the top bar, its title, and the two clock labels the
// refresh below rewrites. Screens keep one of these and otherwise don't
// touch its members.
typedef struct
{
    lv_obj_t *bar;
    lv_obj_t *title_label;
    lv_obj_t *date_label;
    lv_obj_t *time_label;
} SCREEN_HEADER;

// Creates a screen object (not attached to any display until loaded) with a
// fully transparent background and no padding, so GLCD Layer 0 shows
// through and full-width children really do span the panel. Returns NULL on
// failure.
lv_obj_t *Screen_Create(void);

// Creates one of the translucent full-width bars on `parent`, aligned to
// LV_ALIGN_TOP_MID or LV_ALIGN_BOTTOM_MID. Returns NULL on failure.
lv_obj_t *Screen_CreateBar(lv_obj_t *parent, lv_align_t align);

// Creates a white label on `parent` in `font`, aligned as given, showing
// `text`. Returns NULL on failure.
lv_obj_t *Screen_CreateLabel(lv_obj_t *parent, const lv_font_t *font,
        lv_align_t align, int32_t x_offset, int32_t y_offset, const char *text);

// Builds the top bar for `screen`: `title` on the left, date over time on
// the right. Fills in `header`. Returns false on failure.
bool Screen_CreateHeader(lv_obj_t *screen, const char *title, SCREEN_HEADER *header);

// Rewrites the header's date/time from rtcc_shadow (core/rtcc.h), which the
// RTCC interrupt maintains -- a plain struct read, no device access. Safe to
// call on a header whose creation failed.
void Screen_RefreshHeader(SCREEN_HEADER *header);

#ifdef __cplusplus
}
#endif

#endif /* SCREEN_COMMON_H */
