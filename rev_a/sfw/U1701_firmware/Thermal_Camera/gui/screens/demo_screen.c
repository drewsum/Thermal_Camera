/*******************************************************************************
  Demo GUI Screen

  File Name:
    demo_screen.c

  Summary:
    See demo_screen.h for the layout and the transparency rationale.
*******************************************************************************/

#include <stdio.h>

#include "gui/screens/demo_screen.h"

#include "gui/lvgl/lvgl.h"
#include "application/main.h"
#include "application/telemetry.h"
#include "core/rtcc.h"

// Bar geometry. The panel is 320x240 (glcd.h); these are sized so the
// middle ~160px of the screen is untouched transparent overlay.
#define DEMO_SCREEN_BAR_HEIGHT_PX      40
#define DEMO_SCREEN_BAR_PADDING_PX     8
#define DEMO_SCREEN_BATTERY_WIDTH_PX   48
#define DEMO_SCREEN_BATTERY_HEIGHT_PX  12

// How opaque the two bars are over Layer 0. Enough to keep white text
// readable over an arbitrary image, transparent enough to prove the
// hardware blend is really happening.
#define DEMO_SCREEN_BAR_OPACITY        LV_OPA_60

// Widgets whose contents change; everything else is built once and left
// alone. NULL until DemoScreen_Create() succeeds, which is what makes
// DemoScreen_Refresh() safe to call unconditionally.
static lv_obj_t *date_label      = NULL;
static lv_obj_t *time_label      = NULL;
static lv_obj_t *ambient_label   = NULL;
static lv_obj_t *battery_bar     = NULL;
static lv_obj_t *battery_label   = NULL;

// Builds one of the two translucent bars: full width, square corners, no
// border, black at DEMO_SCREEN_BAR_OPACITY, with its children laid out by
// explicit alignment rather than a layout engine.
static lv_obj_t *DemoScreen_CreateBar(lv_obj_t *parent, lv_align_t align)
{
    lv_obj_t *bar = lv_obj_create(parent);

    if (bar == NULL) return NULL;

    lv_obj_set_size(bar, LV_PCT(100), DEMO_SCREEN_BAR_HEIGHT_PX);
    lv_obj_align(bar, align, 0, 0);

    lv_obj_set_style_bg_color(bar, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar, DEMO_SCREEN_BAR_OPACITY, LV_PART_MAIN);
    lv_obj_set_style_border_width(bar, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(bar, DEMO_SCREEN_BAR_PADDING_PX, LV_PART_MAIN);

    // No scrolling: these are static containers, and a stray drag on a
    // future touch build shouldn't be able to shift them
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    return bar;
}

static lv_obj_t *DemoScreen_CreateLabel(lv_obj_t *parent, const lv_font_t *font,
        lv_align_t align, int32_t x_offset, int32_t y_offset, const char *text)
{
    lv_obj_t *label = lv_label_create(parent);

    if (label == NULL) return NULL;

    lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, lv_color_white(), LV_PART_MAIN);
    lv_label_set_text(label, text);
    lv_obj_align(label, align, x_offset, y_offset);

    return label;
}

bool DemoScreen_Create(void)
{
    lv_obj_t *screen = lv_screen_active();

    if (screen == NULL) return false;

    // Fully transparent background: this is an overlay layer, so "no
    // background" means Layer 0 shows through untouched. Without this the
    // theme's opaque screen background would hide the PNG entirely.
    lv_obj_set_style_bg_opa(screen, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    // The default theme gives the screen padding, which LV_PCT(100) and
    // LV_ALIGN_* below both measure against -- the bars would end up inset
    // from the panel edges rather than spanning it
    lv_obj_set_style_pad_all(screen, 0, LV_PART_MAIN);

    // --- Top bar: project name on the left, date/time on the right --------
    lv_obj_t *top_bar = DemoScreen_CreateBar(screen, LV_ALIGN_TOP_MID);
    if (top_bar == NULL) return false;

    if (DemoScreen_CreateLabel(top_bar, &lv_font_montserrat_14,
            LV_ALIGN_LEFT_MID, 0, 0, PROJECT_NAME_STR) == NULL) return false;

    date_label = DemoScreen_CreateLabel(top_bar, &lv_font_montserrat_14,
            LV_ALIGN_TOP_RIGHT, 0, 0, "-------10");
    time_label = DemoScreen_CreateLabel(top_bar, &lv_font_montserrat_14,
            LV_ALIGN_BOTTOM_RIGHT, 0, 0, "--:--:--");

    if ((date_label == NULL) || (time_label == NULL)) return false;

    // --- Bottom bar: ambient temperature and battery state ----------------
    lv_obj_t *bottom_bar = DemoScreen_CreateBar(screen, LV_ALIGN_BOTTOM_MID);
    if (bottom_bar == NULL) return false;

    ambient_label = DemoScreen_CreateLabel(bottom_bar, &lv_font_montserrat_14,
            LV_ALIGN_LEFT_MID, 0, 0, "Amb --.- C");
    battery_label = DemoScreen_CreateLabel(bottom_bar, &lv_font_montserrat_14,
            LV_ALIGN_RIGHT_MID, 0, 0, "---");

    if ((ambient_label == NULL) || (battery_label == NULL)) return false;

    battery_bar = lv_bar_create(bottom_bar);
    if (battery_bar == NULL) return false;

    lv_obj_set_size(battery_bar, DEMO_SCREEN_BATTERY_WIDTH_PX, DEMO_SCREEN_BATTERY_HEIGHT_PX);
    // Sits immediately left of the percentage label, which is ~34px wide at
    // this font -- offset by that plus a gap
    lv_obj_align(battery_bar, LV_ALIGN_RIGHT_MID, -44, 0);
    lv_bar_set_range(battery_bar, 0, 100);
    lv_bar_set_value(battery_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(battery_bar, lv_color_hex(0x404040), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(battery_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(battery_bar, lv_color_hex(0x30C030), LV_PART_INDICATOR);

    DemoScreen_Refresh();

    return true;
}

void DemoScreen_Refresh(void)
{
    char text[32];

    // DemoScreen_Create() either finishes or leaves these NULL
    if ((date_label == NULL) || (time_label == NULL) ||
        (ambient_label == NULL) || (battery_bar == NULL) || (battery_label == NULL)) return;

    // rtcc_shadow is maintained by the RTCC ISR (core/rtcc.c) in plain
    // binary -- no BCD conversion or register reads needed here. Its year
    // field already includes the 2000.
    snprintf(text, sizeof(text), "%04u-%02u-%02u",
            (unsigned int)rtcc_shadow.year,
            (unsigned int)rtcc_shadow.month,
            (unsigned int)rtcc_shadow.day);
    lv_label_set_text(date_label, text);

    snprintf(text, sizeof(text), "%02u:%02u:%02u",
            (unsigned int)rtcc_shadow.hours,
            (unsigned int)rtcc_shadow.minutes,
            (unsigned int)rtcc_shadow.seconds);
    lv_label_set_text(time_label, text);

    // snprintf() rather than lv_label_set_text_fmt(): LVGL's built-in
    // sprintf (LV_USE_STDLIB_SPRINTF = LV_STDLIB_BUILTIN) has no floating
    // point conversions, and XC32's does.
    snprintf(text, sizeof(text), "Amb %.1f C", telemetry.ambient_temperature);
    lv_label_set_text(ambient_label, text);

    if (telemetry.battery.present)
    {
        int32_t percent = (int32_t)telemetry.battery.state_of_charge;

        if (percent < 0) percent = 0;
        if (percent > 100) percent = 100;

        lv_bar_set_value(battery_bar, percent, LV_ANIM_OFF);
        lv_obj_remove_flag(battery_bar, LV_OBJ_FLAG_HIDDEN);

        snprintf(text, sizeof(text), "%ld%%", (long)percent);
        lv_label_set_text(battery_label, text);
    }
    else
    {
        // No cell installed (latched at boot in main.c) -- an empty gauge
        // would read as "flat battery", so hide it entirely
        lv_obj_add_flag(battery_bar, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(battery_label, "USB");
    }
}
