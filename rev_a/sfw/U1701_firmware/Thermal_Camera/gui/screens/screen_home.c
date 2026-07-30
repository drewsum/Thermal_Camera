/*******************************************************************************
  Home GUI Screen

  File Name:
    screen_home.c

  Summary:
    See screen_home.h for the layout and the transparency rationale. The
    screen background, bars, labels and clock all come from
    gui/screens/screen_common.c so this and system_screen.c stay consistent.
*******************************************************************************/

#include <stdio.h>

#include "gui/screens/screen_home.h"
#include "gui/screens/screen_common.h"

#include "gui/lvgl/lvgl.h"
#include "application/main.h"
#include "application/telemetry.h"
#include "application/flir/flir_process.h"

// The scale's gradient has one stop per palette control point (see
// ScreenHomeUpdateScaleGradient()) -- gui/lv_conf.h must allow at least that
// many, or LVGL would silently drop the extras.
#if LV_GRADIENT_MAX_STOPS < FLIR_PALETTE_MAX_CONTROL_POINTS
#error "LV_GRADIENT_MAX_STOPS (gui/lv_conf.h) must cover FLIR_PALETTE_MAX_CONTROL_POINTS"
#endif

#define SCREEN_HOME_BATTERY_WIDTH_PX   48
#define SCREEN_HOME_BATTERY_HEIGHT_PX  12

// Palette scale geometry: a vertical strip in the middle band between the
// two bars (y 40..200), with a min/max label chip just above and below it.
#define SCREEN_HOME_SCALE_WIDTH_PX     16
#define SCREEN_HOME_SCALE_X_PX         10
#define SCREEN_HOME_SCALE_TOP_Y_PX     (SCREEN_BAR_HEIGHT_PX + 24)
#define SCREEN_HOME_SCALE_HEIGHT_PX    112
#define SCREEN_HOME_SCALE_LABEL_GAP_PX 2

// Widgets whose contents change; everything else is built once and left
// alone. NULL until ScreenHome_Create() succeeds, which is what makes
// ScreenHome_Refresh() safe to call unconditionally.
static SCREEN_HEADER header;
static lv_obj_t *ambient_label   = NULL;
static lv_obj_t *battery_bar     = NULL;
static lv_obj_t *battery_label   = NULL;

static lv_obj_t *scale_gradient   = NULL;
static lv_obj_t *scale_max_label  = NULL;
static lv_obj_t *scale_min_label  = NULL;

// Rebuilt only when the active palette changes (see
// ScreenHomeUpdateScaleGradient()), not on every 500ms refresh.
static lv_grad_dsc_t scale_grad;
static FLIR_PALETTE scale_grad_palette = FLIR_PALETTE_COUNT;   // sentinel: no real palette, forces the first build

// Paints scale_gradient's background from the active palette's control
// points, so it always matches the palette LUT exactly instead of being a
// hand-picked approximation. Only touches the style (and so only costs a
// redraw) when the palette actually changed since the last call.
static void ScreenHomeUpdateScaleGradient(void)
{
    FLIR_PALETTE palette = FLIRProcess_GetPalette();
    const FLIR_PaletteControlPoint *points;
    lv_color_t colors[FLIR_PALETTE_MAX_CONTROL_POINTS];
    uint8_t fracs[FLIR_PALETTE_MAX_CONTROL_POINTS];
    uint32_t count;
    uint32_t i;

    if (palette == scale_grad_palette) return;

    count = FLIRProcess_GetPalettePoints(palette, &points);
    if ((count < 2) || (count > FLIR_PALETTE_MAX_CONTROL_POINTS)) return;

    // Control points run cold (idx 0) -> hot (idx 255), but the scale shows
    // hot at the top to match the max/min labels above and below it, and
    // LV_GRAD_DIR_VER paints stop 0 at the top -- so both the stop order and
    // each idx's position are reversed here.
    for (i = 0; i < count; i++)
    {
        const FLIR_PaletteControlPoint *p = &points[count - 1u - i];

        colors[i] = lv_color_make(p->r, p->g, p->b);
        fracs[i] = (uint8_t)(255u - p->idx);
    }

    lv_grad_init_stops(&scale_grad, colors, NULL, fracs, (int)count);
    lv_grad_vertical_init(&scale_grad);
    lv_obj_set_style_bg_grad(scale_gradient, &scale_grad, LV_PART_MAIN);

    scale_grad_palette = palette;
}

// A small label with a translucent black backing chip (matching the header/
// footer bars) so the text stays legible over an arbitrary thermal scene.
static lv_obj_t *ScreenHomeCreateChipLabel(lv_obj_t *parent, const char *text)
{
    lv_obj_t *label = lv_label_create(parent);

    if (label == NULL) return NULL;

    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_color(label, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(label, SCREEN_BAR_OPACITY, LV_PART_MAIN);
    lv_obj_set_style_radius(label, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(label, 3, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(label, 1, LV_PART_MAIN);
    lv_label_set_text(label, text);

    return label;
}

lv_obj_t *ScreenHome_Create(void)
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

    lv_obj_set_size(battery_bar, SCREEN_HOME_BATTERY_WIDTH_PX, SCREEN_HOME_BATTERY_HEIGHT_PX);
    // Sits immediately left of the percentage label, which is ~34px wide at
    // this font -- offset by that plus a gap
    lv_obj_align(battery_bar, LV_ALIGN_RIGHT_MID, -44, 0);
    lv_bar_set_range(battery_bar, 0, 100);
    lv_bar_set_value(battery_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(battery_bar, lv_color_hex(0x404040), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(battery_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(battery_bar, lv_color_hex(0x30C030), LV_PART_INDICATOR);

    // --- Left side: FLIR palette scale -------------------------------------
    scale_gradient = lv_obj_create(screen);
    if (scale_gradient == NULL) return NULL;

    lv_obj_set_size(scale_gradient, SCREEN_HOME_SCALE_WIDTH_PX, SCREEN_HOME_SCALE_HEIGHT_PX);
    lv_obj_align(scale_gradient, LV_ALIGN_TOP_LEFT, SCREEN_HOME_SCALE_X_PX, SCREEN_HOME_SCALE_TOP_Y_PX);
    // Same alpha as the header/footer bars: Layer 1 is an ARGB8888 overlay
    // the GLCD hardware blends over Layer 0 per pixel, so any bg_opa short
    // of LV_OPA_COVER lets the live thermal video show through the scale
    // too, not just around it.
    lv_obj_set_style_bg_opa(scale_gradient, SCREEN_BAR_OPACITY, LV_PART_MAIN);
    lv_obj_set_style_border_width(scale_gradient, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(scale_gradient, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_border_opa(scale_gradient, LV_OPA_80, LV_PART_MAIN);
    lv_obj_set_style_radius(scale_gradient, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(scale_gradient, 0, LV_PART_MAIN);
    lv_obj_remove_flag(scale_gradient, LV_OBJ_FLAG_SCROLLABLE);

    scale_max_label = ScreenHomeCreateChipLabel(screen, "--.- C");
    scale_min_label = ScreenHomeCreateChipLabel(screen, "--.- C");
    if ((scale_max_label == NULL) || (scale_min_label == NULL)) return NULL;

    lv_obj_align_to(scale_max_label, scale_gradient, LV_ALIGN_OUT_TOP_MID, 0, -SCREEN_HOME_SCALE_LABEL_GAP_PX);
    lv_obj_align_to(scale_min_label, scale_gradient, LV_ALIGN_OUT_BOTTOM_MID, 0, SCREEN_HOME_SCALE_LABEL_GAP_PX);

    ScreenHome_Refresh();

    return screen;
}

void ScreenHome_Refresh(void)
{
    char text[32];
    float minCelsius;
    float maxCelsius;

    // ScreenHome_Create() either finishes or leaves these NULL
    if ((ambient_label == NULL) || (battery_bar == NULL) || (battery_label == NULL) ||
        (scale_gradient == NULL) || (scale_max_label == NULL) || (scale_min_label == NULL)) return;

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

    ScreenHomeUpdateScaleGradient();

    // Leaves the "--.- C" placeholder up until the first FLIR frame is
    // rendered -- before that the AGC window is still its boot default,
    // which converts to nonsense degrees rather than "unknown"
    if (FLIRProcess_GetAGCWindowCelsius(&minCelsius, &maxCelsius))
    {
        snprintf(text, sizeof(text), "%.1f C", maxCelsius);
        lv_label_set_text(scale_max_label, text);

        snprintf(text, sizeof(text), "%.1f C", minCelsius);
        lv_label_set_text(scale_min_label, text);
    }
}
