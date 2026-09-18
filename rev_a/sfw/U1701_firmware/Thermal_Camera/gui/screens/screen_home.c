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

#include "gui/gui.h"
#include "gui/lvgl/lvgl.h"
#include "application/main.h"
#include "application/telemetry.h"
#include "application/image_legend.h"
#include "application/flir/flir_process.h"
#include "usb/device_driver/usb_msd.h"
#include "sdhc/sd_fileio.h"

// The scale's gradient has one stop per palette control point (see
// ScreenHomeUpdateScaleGradient()) -- gui/lv_conf.h must allow at least that
// many, or LVGL would silently drop the extras.
#if LV_GRADIENT_MAX_STOPS < FLIR_PALETTE_MAX_CONTROL_POINTS
#error "LV_GRADIENT_MAX_STOPS (gui/lv_conf.h) must cover FLIR_PALETTE_MAX_CONTROL_POINTS"
#endif

#define SCREEN_HOME_BATTERY_WIDTH_PX   48
#define SCREEN_HOME_BATTERY_HEIGHT_PX  12

// Wide enough for "Menu" at 14pt plus the chip's padding, and clear of the
// battery gauge on its left and the SD/USB chips on its right.
#define SCREEN_HOME_MENU_BUTTON_WIDTH_PX  60

// Palette scale geometry: a vertical strip in the middle band between the
// two bars (y 40..200), with a min/max label chip just above and below it.
// Defined in application/image_legend.h rather than here because the SAME
// legend is drawn a second way -- straight into the pixels of a saved image,
// which the GUI overlay never reaches. Aliased to local names so the layout
// code below still reads as this screen's own.
#define SCREEN_HOME_SCALE_WIDTH_PX     IMAGE_LEGEND_SCALE_WIDTH_PX
#define SCREEN_HOME_SCALE_X_PX         IMAGE_LEGEND_SCALE_X_PX
#define SCREEN_HOME_SCALE_TOP_Y_PX     IMAGE_LEGEND_SCALE_TOP_Y_PX
#define SCREEN_HOME_SCALE_HEIGHT_PX    IMAGE_LEGEND_SCALE_HEIGHT_PX
#define SCREEN_HOME_SCALE_LABEL_GAP_PX IMAGE_LEGEND_LABEL_GAP_PX

// Widgets whose contents change; everything else is built once and left
// alone. NULL until ScreenHome_Create() succeeds, which is what makes
// ScreenHome_Refresh() safe to call unconditionally.
static SCREEN_HEADER header;
static lv_obj_t *battery_bar     = NULL;
static lv_obj_t *battery_label   = NULL;
static lv_obj_t *sd_label        = NULL;
static lv_obj_t *usb_label       = NULL;
static lv_obj_t *menu_button     = NULL;

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

    lv_obj_set_style_text_font(label, IMAGE_LEGEND_FONT, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_color(label, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(label, SCREEN_BAR_OPACITY, LV_PART_MAIN);
    lv_obj_set_style_radius(label, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(label, IMAGE_LEGEND_LABEL_PAD_HOR_PX, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(label, IMAGE_LEGEND_LABEL_PAD_VER_PX, LV_PART_MAIN);
    lv_label_set_text(label, text);

    return label;
}

// Dims a status label to gray/translucent when `active` is false, full
// white when true -- shared by the SD-mounted and USB-enumerated chips so
// an absent card or an unplugged/unenumerated host both read as "off" at
// a glance rather than sitting there as a static label with no real
// meaning.
static void ScreenHomeSetStatusLabelActive(lv_obj_t *label, bool active)
{
    if (active)
    {
        lv_obj_set_style_text_color(label, lv_color_white(), LV_PART_MAIN);
        lv_obj_set_style_text_opa(label, LV_OPA_COVER, LV_PART_MAIN);
    }
    else
    {
        lv_obj_set_style_text_color(label, lv_color_hex(0x808080), LV_PART_MAIN);
        lv_obj_set_style_text_opa(label, LV_OPA_50, LV_PART_MAIN);
    }
}

// Positions one of the two temperature chips against the scale strip:
// centered on it and clamped to the panel by ImageLegend_ChipX(), then just
// above (`above`) or just below it.
//
// Called again on every refresh, not just at creation, because the chips are
// LV_SIZE_CONTENT and their text changes width as the temperature does.
// lv_obj_align_to() -- what this replaces -- is a ONE-SHOT that stores a
// fixed top-left, so a chip placed once stayed centered on the "--.- C"
// placeholder and grew rightwards from there forever after. The baked-in
// legend (application/image_legend.c) has no such history to inherit, and
// centering it on the real text put it visibly left of this one and off the
// edge of the frame. Both now compute the same position from the same text.
static void ScreenHomeAlignScaleLabel(lv_obj_t *label, bool above)
{
    int32_t width;
    int32_t height;
    int32_t y;

    // LV_SIZE_CONTENT is resolved by the layout pass, not by
    // lv_label_set_text(), so the width read below is last refresh's until
    // this call brings it up to date
    lv_obj_update_layout(label);

    width = lv_obj_get_width(label);
    height = lv_obj_get_height(label);

    y = above ? (SCREEN_HOME_SCALE_TOP_Y_PX - SCREEN_HOME_SCALE_LABEL_GAP_PX - height)
              : (SCREEN_HOME_SCALE_TOP_Y_PX + SCREEN_HOME_SCALE_HEIGHT_PX +
                 SCREEN_HOME_SCALE_LABEL_GAP_PX);

    // Absolute panel coordinates: the chips' parent is the screen, which
    // Screen_Create() leaves with no padding
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, ImageLegend_ChipX(width), y);
}

static void ScreenHomeMenuClicked(lv_event_t *event)
{
    (void)event;

    GUI_ShowScreen(GUI_SCREEN_MENU, GUI_NAV_FORWARD);
}

lv_obj_t *ScreenHome_Create(void)
{
    lv_obj_t *screen = Screen_Create();

    if (screen == NULL) return NULL;

    if (!Screen_CreateHeader(screen, PROJECT_NAME_STR, &header)) return NULL;

    // --- Bottom bar: battery state, SD card and USB host status -----------
    lv_obj_t *bottom_bar = Screen_CreateBar(screen, LV_ALIGN_BOTTOM_MID);
    if (bottom_bar == NULL) return NULL;

    battery_label = Screen_CreateLabel(bottom_bar, &lv_font_montserrat_14,
            LV_ALIGN_LEFT_MID, 0, 0, "---");

    if (battery_label == NULL) return NULL;

    battery_bar = lv_bar_create(bottom_bar);
    if (battery_bar == NULL) return NULL;

    lv_obj_set_size(battery_bar, SCREEN_HOME_BATTERY_WIDTH_PX, SCREEN_HOME_BATTERY_HEIGHT_PX);
    // Sits immediately right of the percentage label, which is ~34px wide at
    // this font -- offset by that plus a gap
    lv_obj_align(battery_bar, LV_ALIGN_LEFT_MID, 44, 0);
    lv_bar_set_range(battery_bar, 0, 100);
    lv_bar_set_value(battery_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(battery_bar, lv_color_hex(0x404040), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(battery_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(battery_bar, lv_color_hex(0x30C030), LV_PART_INDICATOR);

    usb_label = Screen_CreateLabel(bottom_bar, &lv_font_montserrat_14,
            LV_ALIGN_RIGHT_MID, 0, 0, "USB");
    // Sits just left of "USB" (~30px wide at this font) plus a gap
    sd_label = Screen_CreateLabel(bottom_bar, &lv_font_montserrat_14,
            LV_ALIGN_RIGHT_MID, -38, 0, "SD");

    if ((usb_label == NULL) || (sd_label == NULL)) return NULL;

    // Centered footer button, opening the main menu. Deliberately taller
    // than the bar's 20px content box (36px bar less its 8px padding): the
    // padding governs layout, not clipping, so a full-height tap target
    // still sits inside the bar's bounds instead of being sized to the text.
    menu_button = Screen_CreateButton(bottom_bar, LV_ALIGN_CENTER, 0, 0,
            SCREEN_HOME_MENU_BUTTON_WIDTH_PX, SCREEN_TOUCH_TARGET_MIN_PX,
            "Menu", ScreenHomeMenuClicked, NULL);
    if (menu_button == NULL) return NULL;

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

    ScreenHomeAlignScaleLabel(scale_max_label, true);
    ScreenHomeAlignScaleLabel(scale_min_label, false);

    ScreenHome_Refresh();

    return screen;
}

void ScreenHome_Refresh(void)
{
    char text[32];
    float minCelsius;
    float maxCelsius;

    // ScreenHome_Create() either finishes or leaves these NULL
    if ((battery_bar == NULL) || (battery_label == NULL) || (sd_label == NULL) ||
        (usb_label == NULL) || (scale_gradient == NULL) || (scale_max_label == NULL) ||
        (scale_min_label == NULL)) return;

    Screen_RefreshHeader(&header);

    ScreenHomeSetStatusLabelActive(sd_label, SDFileIO_IsMounted());
    // usb_msd_media_owned_by_host (not USB_IsConfigured()) -- with no VBUS
    // sensing on rev A, a cable unplug only ever shows up as a suspend
    // event, and usb.c's suspend handler doesn't drop usb_device_state out
    // of CONFIGURED (only a bus reset does, which happens on the next
    // reattach). USB_IsConfigured() would therefore stay "active" through
    // an unplug and only dim/rebrighten on the following replug's
    // reset->reconfigure sequence. usb_msd_media_owned_by_host is cleared
    // by USB_MSD_DetachHook() on that same suspend event, so it actually
    // tracks "a host currently has the media."
    ScreenHomeSetStatusLabelActive(usb_label, usb_msd_media_owned_by_host != 0);

    if (telemetry.battery.present)
    {
        int32_t percent = (int32_t)telemetry.battery.state_of_charge;

        if (percent < 0) percent = 0;
        if (percent > 100) percent = 100;

        lv_bar_set_value(battery_bar, percent, LV_ANIM_OFF);
        lv_obj_remove_flag(battery_bar, LV_OBJ_FLAG_HIDDEN);

        lv_obj_set_style_text_color(battery_label, lv_color_white(), LV_PART_MAIN);
        snprintf(text, sizeof(text), "%ld%%", (long)percent);
        lv_label_set_text(battery_label, text);
    }
    else
    {
        // No cell installed (latched at boot in main.c) -- an empty gauge
        // would read as "flat battery", so hide it entirely
        lv_obj_add_flag(battery_bar, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_text_color(battery_label, lv_color_hex(0xFFA500), LV_PART_MAIN);
        lv_label_set_text(battery_label, "No Battery");
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

        // The text just changed width, so the chips have to be placed again
        ScreenHomeAlignScaleLabel(scale_max_label, true);
        ScreenHomeAlignScaleLabel(scale_min_label, false);
    }
}
