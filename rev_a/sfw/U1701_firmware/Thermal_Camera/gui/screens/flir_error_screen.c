/*******************************************************************************
  FLIR Error GUI Screen

  File Name:
    flir_error_screen.c

  Summary:
    See flir_error_screen.h for the layout.
*******************************************************************************/

#include "gui/screens/flir_error_screen.h"
#include "gui/screens/screen_common.h"

#include "gui/gui.h"
#include "gui/lvgl/lvgl.h"
#include "application/error_handler.h"

// Body panel geometry: fills the gap the two bars leave, inset slightly so
// the panel edge reads as a distinct surface over the Layer 0 image (which,
// on this screen, is whatever was last rendered -- normally nothing, since
// the Lepton that would draw to it is the thing that failed).
#define FLIR_ERROR_SCREEN_PANEL_INSET_PX   6
#define FLIR_ERROR_SCREEN_PANEL_PAD_PX     6
#define FLIR_ERROR_SCREEN_ROW_GAP_PX       6

static SCREEN_HEADER header;
static lv_obj_t *headline_label = NULL;
static lv_obj_t *reason_label = NULL;

// Bounds a label to the panel's content width and wraps+centers within it,
// rather than leaving it at its default LV_SIZE_CONTENT width (which grows
// to fit the text unbounded and just gets clipped at the panel edge once the
// text is wider than the panel -- the cause of the cut-off bottom line this
// replaced).
static void FlirErrorScreen_MakeLabelWrap(lv_obj_t *label)
{
    lv_obj_set_width(label, LV_PCT(100));
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
}

lv_obj_t *FlirErrorScreen_Create(void)
{
    lv_obj_t *screen = Screen_Create();
    lv_obj_t *panel;

    if (screen == NULL) return NULL;

    if (!Screen_CreateHeader(screen, "Thermal Camera", &header)) return NULL;

    // --- Body panel -------------------------------------------------------
    panel = lv_obj_create(screen);
    if (panel == NULL) return NULL;

    // Plain pixel sizes, NOT LV_PCT(100) minus an inset -- see system_screen.c
    // for why that combination silently does the wrong thing.
    lv_obj_set_size(panel,
            LV_HOR_RES - (2 * FLIR_ERROR_SCREEN_PANEL_INSET_PX),
            LV_VER_RES - (2 * SCREEN_BAR_HEIGHT_PX) - (2 * FLIR_ERROR_SCREEN_PANEL_INSET_PX));
    lv_obj_align(panel, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(panel, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(panel, SCREEN_BAR_OPACITY, LV_PART_MAIN);
    lv_obj_set_style_border_width(panel, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(panel, FLIR_ERROR_SCREEN_PANEL_PAD_PX, LV_PART_MAIN);
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    // Flex column, centered on both axes: the reason string's length varies
    // (see FlirErrorScreen_Refresh()) and a fixed set of y-offsets, like the
    // other screens use, silently let a longer string's wrapped second line
    // run past the panel and get clipped. Flex stacks each label under the
    // one before it, however many lines it wraps to, so nothing overlaps.
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(panel, FLIR_ERROR_SCREEN_ROW_GAP_PX, LV_PART_MAIN);

    headline_label = Screen_CreateLabel(panel, &lv_font_montserrat_20,
            LV_ALIGN_CENTER, 0, 0, "Thermal Video Unavailable");
    if (headline_label == NULL) return NULL;
    lv_obj_set_style_text_color(headline_label, lv_color_hex(0xE04040), LV_PART_MAIN);
    FlirErrorScreen_MakeLabelWrap(headline_label);

    reason_label = Screen_CreateLabel(panel, &lv_font_montserrat_14,
            LV_ALIGN_CENTER, 0, 0, "--");
    if (reason_label == NULL) return NULL;
    FlirErrorScreen_MakeLabelWrap(reason_label);

    lv_obj_t *hint_label = Screen_CreateLabel(panel, &lv_font_montserrat_14, LV_ALIGN_CENTER, 0, 0,
            "Call 'FLIR Status?' on the USB console for details.");
    if (hint_label == NULL) return NULL;
    FlirErrorScreen_MakeLabelWrap(hint_label);

    FlirErrorScreen_Refresh();

    return screen;
}

void FlirErrorScreen_Refresh(void)
{
    // FlirErrorScreen_Create() either finishes or leaves this NULL
    if (reason_label == NULL) return;

    Screen_RefreshHeader(&header);

    // The three flir_* fault flags (error_handler.h) are mutually exclusive
    // in practice -- flir.c's state machine latches exactly one before
    // dropping to FLIR_STATE_FAULT -- checked in the order flir.c's own
    // bring-up sequence can set them.
    if (error_handler.flags.flir_rail_pgood_timeout)
    {
        lv_label_set_text(reason_label, "FLIR Lepton Rail PGOOD Timeout");
    }
    else if (error_handler.flags.flir_boot_timeout)
    {
        lv_label_set_text(reason_label, "FLIR Lepton Boot Timeout");
    }
    else if (error_handler.flags.flir_cci_error)
    {
        lv_label_set_text(reason_label, "FLIR Lepton CCI Configuration Failed");
    }
    else
    {
        lv_label_set_text(reason_label, "Unknown Fault");
    }
}
