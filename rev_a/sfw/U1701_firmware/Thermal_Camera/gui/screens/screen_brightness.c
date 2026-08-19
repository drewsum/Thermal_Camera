/*******************************************************************************
  LCD Brightness GUI Screen

  File Name:
    screen_brightness.c

  Summary:
    See screen_brightness.h.
*******************************************************************************/

#include <stdio.h>

#include "gui/screens/screen_brightness.h"
#include "gui/screens/screen_common.h"

#include "gui/gui.h"
#include "gui/lvgl/lvgl.h"
#include "application/backlight_pwm.h"

// gui/lv_conf.h keeps the widget set trimmed to what the screens actually
// use, and this is the only screen that needs a slider. Catch it being
// turned back off here rather than as a pile of implicit-declaration
// warnings and an undefined reference at link time.
#if !LV_USE_SLIDER
#error "LV_USE_SLIDER (gui/lv_conf.h) must be 1 for gui/screens/screen_brightness.c"
#endif

// Body panel geometry, matching the menu and palette screens so the three
// read as the same instrument.
#define SCREEN_BRIGHTNESS_PANEL_INSET_PX    6
#define SCREEN_BRIGHTNESS_PANEL_PAD_PX      6

// Slider track. Height is the track only -- the knob is drawn taller than
// this (see the knob styling below), which is what makes it read as a handle
// sitting ON the track rather than a segment of it.
#define SCREEN_BRIGHTNESS_TRACK_HEIGHT_PX   20

// Knob, expressed as the deltas lv_slider applies to its natural size. For a
// horizontal slider the knob starts as a square of the track height
// (SCREEN_BRIGHTNESS_TRACK_HEIGHT_PX on a side), and LV_PART_KNOB's paddings
// grow or -- negative -- shrink it from there. 20-2*4 = 12 wide by 20+2*5 =
// 30 tall: a portrait rectangle that reads as a grab handle, and 30px tall
// clears the touch target floor on the axis the finger has to land on.
#define SCREEN_BRIGHTNESS_KNOB_PAD_HOR_PX   (-4)
#define SCREEN_BRIGHTNESS_KNOB_PAD_VER_PX   5

// How far the knob sticks out past each end of the track at the ends of its
// travel. lv_slider centers the knob on the end of the filled indicator, so
// half of it hangs off -- half of the square it starts as (the track height),
// less what the negative horizontal padding took back. The track has to be
// inset from the panel by this much or the knob is clipped at either end.
#define SCREEN_BRIGHTNESS_KNOB_OVERHANG_PX  \
        ((SCREEN_BRIGHTNESS_TRACK_HEIGHT_PX / 2) + SCREEN_BRIGHTNESS_KNOB_PAD_HOR_PX)

// Vertical placement inside the panel, relative to its center. The panel is
// 240 - 2*36 (bars) - 2*6 (inset) = 156 tall, less 2*6 of padding, so the
// content box runs from -72 to +72 and these four rows -- caption, readout,
// slider, end-labels -- fit it with room at both ends.
#define SCREEN_BRIGHTNESS_CAPTION_Y_PX      (-56)
#define SCREEN_BRIGHTNESS_READOUT_Y_PX      (-32)
#define SCREEN_BRIGHTNESS_SLIDER_Y_PX       10

// Gap between the bottom of the track and the end-labels under it. Larger
// than it looks: the knob overhangs the track by
// SCREEN_BRIGHTNESS_KNOB_PAD_VER_PX, so this is the clearance from the knob,
// not from the track.
#define SCREEN_BRIGHTNESS_END_LABEL_GAP_PX  10

// Colors, all of them already in use elsewhere in this GUI. The track is
// deliberately NOT in this list: it has no fill at all (see the styling
// below), so its only color is the outline.
//   indicator 0x303030  Screen_CreateButton()'s pressed fill
//   knob      0xD0D0D0  a light neutral, going to white while held
//   outline   0xA0A0A0  Screen_CreateButton()'s border color
//   dim text  0x808080  the home screen's secondary labels
#define SCREEN_BRIGHTNESS_INDICATOR_COLOR   0x303030
#define SCREEN_BRIGHTNESS_KNOB_COLOR        0xD0D0D0
#define SCREEN_BRIGHTNESS_KNOB_PRESS_COLOR  0xFFFFFF
#define SCREEN_BRIGHTNESS_OUTLINE_COLOR     0xA0A0A0
#define SCREEN_BRIGHTNESS_DIM_TEXT_COLOR    0x808080

// An empty style transition, used below to cancel the one the default theme
// puts on the slider knob.
//
// TRAP: the obvious way to do that -- lv_obj_set_style_transition(obj, NULL,
// selector) -- is a NULL dereference, not a removal. It ADDS the
// LV_STYLE_TRANSITION property to the local style with a NULL value, and
// lv_obj.c's update_obj_state() reads it back on the next state change with
//
//     if(lv_style_get_prop_inlined(..., LV_STYLE_TRANSITION, &v) != FOUND) continue;
//     const lv_style_transition_dsc_t * tr = v.ptr;
//     for(j = 0; tr->props[j] != 0 ...)          <-- lv_obj.c:943, tr is NULL
//
// The property IS found, so the guard above does not fire. Touching the
// slider then took a TLB Refill exception with BadVAddr 0x00000000.
//
// A descriptor whose property list is just the terminator is the working
// equivalent: the loop above reads props[0], finds 0, and adds nothing. The
// list must have static storage -- lv_style_transition_dsc_t keeps the
// pointer (const lv_style_prop_t *props), it does not copy the array.
static const lv_style_prop_t screen_brightness_no_props[] = { 0 };

static const lv_style_transition_dsc_t screen_brightness_no_transition =
{
    .props = screen_brightness_no_props,
    .user_data = NULL,
    .path_xcb = NULL,
    .time = 0,
    .delay = 0,
};

static SCREEN_HEADER header;

// Both NULL until Create() finishes, which is what Refresh() checks -- the
// same "did the screen get built" guard the other screens use.
static lv_obj_t *brightness_slider = NULL;
static lv_obj_t *value_label = NULL;

// Writes `percent` into the readout above the slider. Split out because the
// value arrives from two directions: the finger (the event callback) and the
// 500ms refresh picking up a UART-driven change.
static void ScreenBrightnessSetReadout(uint8_t percent)
{
    char text[8];

    if (value_label == NULL) return;

    snprintf(text, sizeof(text), "%u%%", (unsigned int)percent);
    lv_label_set_text(value_label, text);
}

// LV_EVENT_VALUE_CHANGED: fired on every touch report while the finger is
// moving, not just on release, so the backlight tracks the knob. Applying it
// live is the whole point of a brightness control -- the user judges the
// result by eye, not by the number -- and BacklightPWM_SetBrightness() is
// two SFR writes, so doing it at the touch report rate costs nothing.
static void ScreenBrightnessValueChanged(lv_event_t *event)
{
    lv_obj_t *slider = (lv_obj_t *)lv_event_get_target(event);
    int32_t value;

    if (slider == NULL) return;

    value = lv_slider_get_value(slider);

    // lv_slider_set_range() already clamps to this, but the value goes
    // straight into a hardware duty cycle -- keep the clamp local so a
    // future range change cannot drive the panel dark from here.
    if (value < SCREEN_BRIGHTNESS_MIN_PERCENT) value = SCREEN_BRIGHTNESS_MIN_PERCENT;
    if (value > 100) value = 100;

    BacklightPWM_SetBrightness((uint8_t)value);
    ScreenBrightnessSetReadout((uint8_t)value);
}

static void ScreenBrightnessBackClicked(lv_event_t *event)
{
    (void)event;

    GUI_ShowScreen(GUI_SCREEN_MENU, GUI_NAV_BACK);
}

// Applies the whole slider look. Kept out of Create() because it is a solid
// block of style calls and none of it can fail -- Create() stays readable as
// the sequence of things that can.
//
// Every part is restyled rather than accepting the default theme's, which
// draws a pill-shaped track with a round knob in the theme's own blue. Local
// styles beat theme styles in LVGL, so nothing has to be removed first.
static void ScreenBrightnessStyleSlider(lv_obj_t *slider)
{
    // The track and its fill are Screen_CreateButton()'s two states, split
    // across the length of the control: the empty part is a chip at rest and
    // the filled part is the same chip held down. That is the whole visual
    // idea here -- the slider reads as a button being pressed from the left.

    // --- Track (LV_PART_MAIN), i.e. the unfilled part ----------------------
    // No fill at all, exactly like a chip at rest, so the panel -- and the
    // Layer 0 image through it -- shows behind the empty part of the travel.
    // The border is then the ONLY thing marking where the track ends, so it
    // cannot be dropped without the remaining travel becoming invisible.
    lv_obj_set_style_bg_opa(slider, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_radius(slider, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(slider, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(slider, lv_color_hex(SCREEN_BRIGHTNESS_OUTLINE_COLOR), LV_PART_MAIN);
    lv_obj_set_style_border_opa(slider, LV_OPA_50, LV_PART_MAIN);

    // The indicator is inset by the track's padding, so zero it -- otherwise
    // the fill stops short of the ends and 100% does not look full
    lv_obj_set_style_pad_all(slider, 0, LV_PART_MAIN);

    // --- Indicator (the filled part) --------------------------------------
    // The pressed half of the pair: the same 0x303030 at LV_OPA_COVER that
    // Screen_CreateButton() fills a held chip with, opaque for the same
    // reason it gives -- at anything less the fill blends with whatever
    // Layer 0 is showing underneath, so the level would only actually BE
    // 0x303030 over a black scene and would wash out over a hot one.
    lv_obj_set_style_bg_color(slider, lv_color_hex(SCREEN_BRIGHTNESS_INDICATOR_COLOR), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(slider, 0, LV_PART_INDICATOR);

    // --- Knob -------------------------------------------------------------
    // radius 0 is what makes it rectangular; the theme's knob style is a
    // circle (styles.circle, i.e. LV_RADIUS_CIRCLE).
    lv_obj_set_style_radius(slider, 0, LV_PART_KNOB);
    lv_obj_set_style_bg_color(slider, lv_color_hex(SCREEN_BRIGHTNESS_KNOB_COLOR), LV_PART_KNOB);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_border_width(slider, 1, LV_PART_KNOB);
    lv_obj_set_style_border_color(slider, lv_color_black(), LV_PART_KNOB);
    lv_obj_set_style_border_opa(slider, LV_OPA_80, LV_PART_KNOB);

    // Negative horizontal padding narrows the knob from the square
    // lv_slider gives it; positive vertical padding makes it overhang the
    // track. lv_slider reports the larger of the two through
    // LV_EVENT_REFR_EXT_DRAW_SIZE, so the overhang is not clipped.
    lv_obj_set_style_pad_hor(slider, SCREEN_BRIGHTNESS_KNOB_PAD_HOR_PX, LV_PART_KNOB);
    lv_obj_set_style_pad_ver(slider, SCREEN_BRIGHTNESS_KNOB_PAD_VER_PX, LV_PART_KNOB);

    // Held: brighter, same rectangle. The default theme grows the knob on
    // press (styles.grow, a transform_width/height of a few px) and animates
    // it there and back, which on a hard-edged knob reads as the rectangle
    // changing shape under the finger. Zeroing the transform in the pressed
    // state overrides that; the color change is the press feedback instead,
    // matching Screen_CreateButton()'s fill-while-held.
    lv_obj_set_style_bg_color(slider, lv_color_hex(SCREEN_BRIGHTNESS_KNOB_PRESS_COLOR),
            LV_PART_KNOB | LV_STATE_PRESSED);
    lv_obj_set_style_transform_width(slider, 0, LV_PART_KNOB | LV_STATE_PRESSED);
    lv_obj_set_style_transform_height(slider, 0, LV_PART_KNOB | LV_STATE_PRESSED);

    // And drop the theme's state transitions on the knob outright, so the
    // color snaps rather than fading in over ~90ms. Every other control in
    // this GUI changes state instantly (Screen_CreateButton() sets its
    // pressed fill with no transition), and the animation the theme starts
    // here is heap-allocated work per press for an effect nothing else has.
    //
    // Both selectors have to be overridden -- the theme puts
    // styles.transition_delayed on LV_PART_KNOB and styles.transition_normal
    // on LV_PART_KNOB | LV_STATE_PRESSED. NOT with NULL: see the trap note by
    // screen_brightness_no_transition.
    lv_obj_set_style_transition(slider, &screen_brightness_no_transition, LV_PART_KNOB);
    lv_obj_set_style_transition(slider, &screen_brightness_no_transition,
            LV_PART_KNOB | LV_STATE_PRESSED);
}

lv_obj_t *ScreenBrightness_Create(void)
{
    lv_obj_t *screen = Screen_Create();
    lv_obj_t *panel;
    lv_obj_t *caption;
    lv_obj_t *min_label;
    lv_obj_t *max_label;
    char end_text[8];
    int32_t panel_width;
    int32_t slider_width;

    if (screen == NULL) return NULL;

    if (!Screen_CreateHeader(screen, "LCD Brightness", &header)) return NULL;

    if (!Screen_AddBackButton(&header, ScreenBrightnessBackClicked, NULL)) return NULL;

    // --- Body panel -------------------------------------------------------
    panel = lv_obj_create(screen);
    if (panel == NULL) return NULL;

    // Plain pixel arithmetic, NOT LV_PCT() minus an inset -- LV_PCT()
    // returns an encoded special value, so subtracting from it silently
    // changes the percentage instead of insetting anything.
    panel_width = LV_HOR_RES - (2 * SCREEN_BRIGHTNESS_PANEL_INSET_PX);

    lv_obj_set_size(panel, panel_width,
            LV_VER_RES - (2 * SCREEN_BAR_HEIGHT_PX) - (2 * SCREEN_BRIGHTNESS_PANEL_INSET_PX));
    lv_obj_align(panel, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(panel, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(panel, SCREEN_BAR_OPACITY, LV_PART_MAIN);
    lv_obj_set_style_border_width(panel, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(panel, SCREEN_BRIGHTNESS_PANEL_PAD_PX, LV_PART_MAIN);

    // Everything fits, so nothing here scrolls -- and a scrollable parent
    // would steal the horizontal drag the slider needs, turning a slow drag
    // into a scroll gesture and leaving the knob behind.
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    // --- Readout ----------------------------------------------------------
    caption = Screen_CreateLabel(panel, &lv_font_montserrat_14,
            LV_ALIGN_CENTER, 0, SCREEN_BRIGHTNESS_CAPTION_Y_PX, "Backlight");
    if (caption == NULL) return NULL;

    lv_obj_set_style_text_color(caption, lv_color_hex(SCREEN_BRIGHTNESS_DIM_TEXT_COLOR),
            LV_PART_MAIN);

    // Placeholder sized like the widest real value, so the label is laid out
    // at its final width before the first refresh and the centered readout
    // does not shift as the number changes digit count
    value_label = Screen_CreateLabel(panel, &lv_font_montserrat_20,
            LV_ALIGN_CENTER, 0, SCREEN_BRIGHTNESS_READOUT_Y_PX, "100%");
    if (value_label == NULL) return NULL;

    // --- Slider -----------------------------------------------------------
    brightness_slider = lv_slider_create(panel);
    if (brightness_slider == NULL)
    {
        // Leave the "screen built" guard clear so Refresh() stays a no-op
        value_label = NULL;
        return NULL;
    }

    // Inset from the panel's content box by the knob overhang at both ends,
    // for the reason given where that constant is defined
    slider_width = panel_width - (2 * SCREEN_BRIGHTNESS_PANEL_PAD_PX)
            - (2 * SCREEN_BRIGHTNESS_KNOB_OVERHANG_PX);

    lv_obj_set_size(brightness_slider, slider_width, SCREEN_BRIGHTNESS_TRACK_HEIGHT_PX);
    lv_obj_align(brightness_slider, LV_ALIGN_CENTER, 0, SCREEN_BRIGHTNESS_SLIDER_Y_PX);

    // Range before value: lv_slider_set_value() clamps to the current range,
    // and the default range only happens to be 0-100
    lv_slider_set_range(brightness_slider, SCREEN_BRIGHTNESS_MIN_PERCENT, 100);

    ScreenBrightnessStyleSlider(brightness_slider);

    lv_obj_add_event_cb(brightness_slider, ScreenBrightnessValueChanged,
            LV_EVENT_VALUE_CHANGED, NULL);

    // --- Range end-labels --------------------------------------------------
    snprintf(end_text, sizeof(end_text), "%u%%", (unsigned int)SCREEN_BRIGHTNESS_MIN_PERCENT);

    min_label = Screen_CreateLabel(panel, &lv_font_montserrat_14,
            LV_ALIGN_CENTER, 0, 0, end_text);
    max_label = Screen_CreateLabel(panel, &lv_font_montserrat_14,
            LV_ALIGN_CENTER, 0, 0, "100%");

    if ((min_label == NULL) || (max_label == NULL))
    {
        value_label = NULL;
        brightness_slider = NULL;
        return NULL;
    }

    lv_obj_set_style_text_color(min_label, lv_color_hex(SCREEN_BRIGHTNESS_DIM_TEXT_COLOR),
            LV_PART_MAIN);
    lv_obj_set_style_text_color(max_label, lv_color_hex(SCREEN_BRIGHTNESS_DIM_TEXT_COLOR),
            LV_PART_MAIN);

    // Aligned to the slider rather than the panel, so they stay under the
    // ends of the track if its width or inset ever changes
    lv_obj_align_to(min_label, brightness_slider, LV_ALIGN_OUT_BOTTOM_LEFT, 0,
            SCREEN_BRIGHTNESS_END_LABEL_GAP_PX);
    lv_obj_align_to(max_label, brightness_slider, LV_ALIGN_OUT_BOTTOM_RIGHT, 0,
            SCREEN_BRIGHTNESS_END_LABEL_GAP_PX);

    ScreenBrightness_Refresh();

    return screen;
}

void ScreenBrightness_Refresh(void)
{
    uint8_t percent;

    // ScreenBrightness_Create() either finishes or leaves these NULL
    if ((brightness_slider == NULL) || (value_label == NULL)) return;

    Screen_RefreshHeader(&header);

    // Do not move the knob out from under the finger. The value being
    // dragged to is already applied and already in the readout, so there is
    // nothing for this to add mid-drag -- and re-setting the value would
    // fight the drag every 500ms.
    if (lv_obj_has_state(brightness_slider, LV_STATE_PRESSED)) return;

    percent = BacklightPWM_GetBrightness();

    // The range floor is a usability limit, not a hardware one, so the live
    // value can legitimately sit below it -- 0% during the sleep sequence, or
    // anything the "LCD Brightness:" UART command was given. Show the true
    // number, and park the knob at the end of its own travel.
    ScreenBrightnessSetReadout(percent);

    if (percent < SCREEN_BRIGHTNESS_MIN_PERCENT) percent = SCREEN_BRIGHTNESS_MIN_PERCENT;
    if (percent > 100) percent = 100;

    lv_slider_set_value(brightness_slider, (int32_t)percent, LV_ANIM_OFF);
}
