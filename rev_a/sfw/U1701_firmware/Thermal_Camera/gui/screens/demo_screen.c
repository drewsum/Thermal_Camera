/*******************************************************************************
  Demo GUI Screen

  File Name:
    demo_screen.c

  Summary:
    See demo_screen.h for the layout and the transparency rationale. The
    screen background, bars, labels and clock all come from
    gui/screens/screen_common.c so this and system_screen.c stay consistent.
*******************************************************************************/

#include <stdio.h>

#include "gui/screens/demo_screen.h"
#include "gui/screens/screen_common.h"

#include "gui/lvgl/lvgl.h"
#include "application/main.h"
#include "application/telemetry.h"

#define DEMO_SCREEN_BATTERY_WIDTH_PX   48
#define DEMO_SCREEN_BATTERY_HEIGHT_PX  12

// Widgets whose contents change; everything else is built once and left
// alone. NULL until DemoScreen_Create() succeeds, which is what makes
// DemoScreen_Refresh() safe to call unconditionally.
static SCREEN_HEADER header;
static lv_obj_t *ambient_label   = NULL;
static lv_obj_t *battery_bar     = NULL;
static lv_obj_t *battery_label   = NULL;

lv_obj_t *DemoScreen_Create(void)
{
    lv_obj_t *screen = Screen_Create();

    if (screen == NULL) return NULL;

    if (!Screen_CreateHeader(screen, PROJECT_NAME_STR, &header)) return NULL;

    // --- Bottom bar: ambient temperature and battery state ----------------
    lv_obj_t *bottom_bar = Screen_CreateBar(screen, LV_ALIGN_BOTTOM_MID);
    if (bottom_bar == NULL) return NULL;

    ambient_label = Screen_CreateLabel(bottom_bar, &lv_font_montserrat_14,
            LV_ALIGN_LEFT_MID, 0, 0, "Amb --.- C");
    battery_label = Screen_CreateLabel(bottom_bar, &lv_font_montserrat_14,
            LV_ALIGN_RIGHT_MID, 0, 0, "---");

    if ((ambient_label == NULL) || (battery_label == NULL)) return NULL;

    battery_bar = lv_bar_create(bottom_bar);
    if (battery_bar == NULL) return NULL;

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

    return screen;
}

void DemoScreen_Refresh(void)
{
    char text[32];

    // DemoScreen_Create() either finishes or leaves these NULL
    if ((ambient_label == NULL) || (battery_bar == NULL) || (battery_label == NULL)) return;

    Screen_RefreshHeader(&header);

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
