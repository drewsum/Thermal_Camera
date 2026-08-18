/*******************************************************************************
  Thermal Palette GUI Screen

  File Name:
    screen_palette.c

  Summary:
    See screen_palette.h.
*******************************************************************************/

#include "gui/screens/screen_palette.h"
#include "gui/screens/screen_common.h"

#include "gui/gui.h"
#include "gui/lvgl/lvgl.h"
#include "application/flir/flir_process.h"

// Each swatch's gradient has one stop per palette control point -- the same
// constraint the home screen's scale is under, checked here too because this
// screen builds its gradients independently.
#if LV_GRADIENT_MAX_STOPS < FLIR_PALETTE_MAX_CONTROL_POINTS
#error "LV_GRADIENT_MAX_STOPS (gui/lv_conf.h) must cover FLIR_PALETTE_MAX_CONTROL_POINTS"
#endif

// Body panel geometry, matching the menu and system screens.
#define SCREEN_PALETTE_PANEL_INSET_PX   6
#define SCREEN_PALETTE_PANEL_PAD_PX     6
#define SCREEN_PALETTE_ROW_HEIGHT_PX    SCREEN_TOUCH_TARGET_MIN_PX
#define SCREEN_PALETTE_ROW_GAP_PX       4

// Right-hand lane kept clear for the scrollbar the default theme draws
// inside the panel's right edge -- same reasoning as system_screen.c.
#define SCREEN_PALETTE_SCROLLBAR_LANE_PX 14

// Swatch strip, and the space reserved to its right for the check mark.
#define SCREEN_PALETTE_SWATCH_WIDTH_PX  86
#define SCREEN_PALETTE_SWATCH_HEIGHT_PX 14
#define SCREEN_PALETTE_CHECK_WIDTH_PX   20

static SCREEN_HEADER header;

// One check mark per palette; exactly one is visible at a time. Also doubles
// as the "did Create() finish" guard, like the other screens' value labels.
static lv_obj_t *check_labels[FLIR_PALETTE_COUNT] = { NULL };

// The gradient descriptors MUST outlive the style that references them --
// lv_obj_set_style_bg_grad() stores the pointer, it does not copy. Hence one
// static per row rather than a local in the build loop, which would leave
// every swatch pointing at a dead stack frame.
static lv_grad_dsc_t swatch_grads[FLIR_PALETTE_COUNT];

// Paints `swatch` with `palette`'s own control points, cold on the left and
// hot on the right. Built from FLIRProcess_GetPalettePoints() -- the same
// data the render LUT is built from -- so the preview cannot drift from the
// video. Unlike the home screen's scale, the control points are used in
// their natural order here: that one runs hot-at-top and has to reverse
// them, this one reads left to right.
static bool ScreenPaletteBuildSwatch(lv_obj_t *swatch, FLIR_PALETTE palette,
        lv_grad_dsc_t *grad)
{
    const FLIR_PaletteControlPoint *points;
    lv_color_t colors[FLIR_PALETTE_MAX_CONTROL_POINTS];
    uint8_t fracs[FLIR_PALETTE_MAX_CONTROL_POINTS];
    uint32_t count;
    uint32_t i;

    count = FLIRProcess_GetPalettePoints(palette, &points);

    if ((count < 2) || (count > FLIR_PALETTE_MAX_CONTROL_POINTS)) return false;

    for (i = 0; i < count; i++)
    {
        colors[i] = lv_color_make(points[i].r, points[i].g, points[i].b);
        fracs[i] = points[i].idx;
    }

    lv_grad_init_stops(grad, colors, NULL, fracs, (int)count);
    lv_grad_horizontal_init(grad);
    lv_obj_set_style_bg_grad(swatch, grad, LV_PART_MAIN);

    return true;
}

// One callback for every row; the palette arrives as the event's user data.
static void ScreenPaletteRowClicked(lv_event_t *event)
{
    uintptr_t palette = (uintptr_t)lv_event_get_user_data(event);

    if (palette >= (uintptr_t)FLIR_PALETTE_COUNT) return;

    // Rebuilds the render LUT. Cheap, and the next rendered frame picks it
    // up -- there is no need to stop or restart the video stream.
    FLIRProcess_SetPalette((FLIR_PALETTE)palette);

    // Move the check mark now rather than waiting for the 500ms refresh, so
    // the tap is acknowledged immediately.
    ScreenPalette_Refresh();
}

static void ScreenPaletteBackClicked(lv_event_t *event)
{
    (void)event;

    GUI_ShowScreen(GUI_SCREEN_MENU, GUI_NAV_BACK);
}

lv_obj_t *ScreenPalette_Create(void)
{
    lv_obj_t *screen = Screen_Create();
    lv_obj_t *panel;
    int32_t panel_width;
    int32_t row_width;
    uint32_t index;

    if (screen == NULL) return NULL;

    if (!Screen_CreateHeader(screen, "Thermal Palette", &header)) return NULL;

    if (!Screen_AddBackButton(&header, ScreenPaletteBackClicked, NULL)) return NULL;

    // --- Body panel -------------------------------------------------------
    panel = lv_obj_create(screen);
    if (panel == NULL) return NULL;

    // Plain pixel arithmetic, NOT LV_PCT() minus an inset -- LV_PCT()
    // returns an encoded special value, so subtracting from it silently
    // changes the percentage instead of insetting anything.
    panel_width = LV_HOR_RES - (2 * SCREEN_PALETTE_PANEL_INSET_PX);

    lv_obj_set_size(panel, panel_width,
            LV_VER_RES - (2 * SCREEN_BAR_HEIGHT_PX) - (2 * SCREEN_PALETTE_PANEL_INSET_PX));
    lv_obj_align(panel, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(panel, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(panel, SCREEN_BAR_OPACITY, LV_PART_MAIN);
    lv_obj_set_style_border_width(panel, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(panel, SCREEN_PALETTE_PANEL_PAD_PX, LV_PART_MAIN);
    lv_obj_set_style_pad_right(panel, SCREEN_PALETTE_SCROLLBAR_LANE_PX, LV_PART_MAIN);

    // Eight palettes at 32px a row overflow the ~144px the bars leave, so
    // this panel scrolls like the system screen's. Vertical only.
    lv_obj_set_scroll_dir(panel, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(panel, LV_SCROLLBAR_MODE_ON);

    row_width = panel_width - SCREEN_PALETTE_PANEL_PAD_PX - SCREEN_PALETTE_SCROLLBAR_LANE_PX;

    // --- Rows -------------------------------------------------------------
    // Driven by FLIR_PALETTE_COUNT and FLIRProcess_PaletteName(), so a
    // palette added to the driver shows up here automatically.
    for (index = 0; index < (uint32_t)FLIR_PALETTE_COUNT; index++)
    {
        int32_t y = (int32_t)(index * (SCREEN_PALETTE_ROW_HEIGHT_PX + SCREEN_PALETTE_ROW_GAP_PX));
        lv_obj_t *row;
        lv_obj_t *swatch;

        row = Screen_CreateButton(panel, LV_ALIGN_TOP_LEFT, 0, y,
                row_width, SCREEN_PALETTE_ROW_HEIGHT_PX,
                FLIRProcess_PaletteName((FLIR_PALETTE)index),
                ScreenPaletteRowClicked, (void *)(uintptr_t)index);

        if (row == NULL) return NULL;

        // lv_obj_get_child(row, 0) is the label Screen_CreateButton made,
        // which it centers; the name belongs on the left here.
        lv_obj_align(lv_obj_get_child(row, 0), LV_ALIGN_LEFT_MID, 0, 0);

        swatch = lv_obj_create(row);
        if (swatch == NULL) return NULL;

        lv_obj_set_size(swatch, SCREEN_PALETTE_SWATCH_WIDTH_PX, SCREEN_PALETTE_SWATCH_HEIGHT_PX);
        lv_obj_align(swatch, LV_ALIGN_RIGHT_MID, -SCREEN_PALETTE_CHECK_WIDTH_PX, 0);

        // Fully opaque, unlike the rest of this GUI's furniture: this is a
        // colour sample, and blending it with whatever the thermal video is
        // showing would make it preview a colour the palette never produces.
        lv_obj_set_style_bg_opa(swatch, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(swatch, 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(swatch, lv_color_hex(0x000000), LV_PART_MAIN);
        lv_obj_set_style_border_opa(swatch, LV_OPA_80, LV_PART_MAIN);
        lv_obj_set_style_radius(swatch, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(swatch, 0, LV_PART_MAIN);
        lv_obj_remove_flag(swatch, LV_OBJ_FLAG_SCROLLABLE);

        // Would otherwise swallow the press before the row sees it
        lv_obj_remove_flag(swatch, LV_OBJ_FLAG_CLICKABLE);

        if (!ScreenPaletteBuildSwatch(swatch, (FLIR_PALETTE)index, &swatch_grads[index]))
        {
            return NULL;
        }

        check_labels[index] = Screen_CreateLabel(row, &lv_font_montserrat_14,
                LV_ALIGN_RIGHT_MID, 0, 0, LV_SYMBOL_OK);

        if (check_labels[index] == NULL) return NULL;

        lv_obj_remove_flag(check_labels[index], LV_OBJ_FLAG_CLICKABLE);
    }

    ScreenPalette_Refresh();

    return screen;
}

void ScreenPalette_Refresh(void)
{
    FLIR_PALETTE active;
    uint32_t index;

    // ScreenPalette_Create() either finishes or leaves these NULL
    if (check_labels[0] == NULL) return;

    Screen_RefreshHeader(&header);

    active = FLIRProcess_GetPalette();

    for (index = 0; index < (uint32_t)FLIR_PALETTE_COUNT; index++)
    {
        // Hidden rather than deleted/recreated, so the row layout never
        // shifts as the selection moves
        if ((uint32_t)active == index)
        {
            lv_obj_remove_flag(check_labels[index], LV_OBJ_FLAG_HIDDEN);
        }
        else
        {
            lv_obj_add_flag(check_labels[index], LV_OBJ_FLAG_HIDDEN);
        }
    }
}
