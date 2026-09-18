/*******************************************************************************
  Diagnostics GUI Screen

  File Name:
    screen_errors.c

  Summary:
    See screen_errors.h.
*******************************************************************************/

#include <stdio.h>
#include <string.h>

#include "gui/screens/screen_errors.h"
#include "gui/screens/screen_common.h"

#include "gui/gui.h"
#include "gui/lvgl/lvgl.h"

#include "application/error_handler.h"

// Body panel geometry, matching the system status screen so the two read as
// the same instrument.
#define SCREEN_ERRORS_PANEL_INSET_PX     6
#define SCREEN_ERRORS_PANEL_PAD_PX       6

// Right-hand lane kept clear for the scrollbar the default theme draws
// inside the panel's right edge -- see system_screen.c, same reasoning.
#define SCREEN_ERRORS_SCROLLBAR_LANE_PX  14

// The panel's content width, which the wrapping list label is sized to.
#define SCREEN_ERRORS_CONTENT_W_PX \
        (LV_HOR_RES - (2 * SCREEN_ERRORS_PANEL_INSET_PX) \
                - SCREEN_ERRORS_PANEL_PAD_PX - SCREEN_ERRORS_SCROLLBAR_LANE_PX)

// Summary line at the top of the panel, then the list below it
#define SCREEN_ERRORS_SUMMARY_Y_PX       0
#define SCREEN_ERRORS_LIST_Y_PX          24

// The action bar's vertical padding is trimmed the same way
// Screen_CreateHeader() trims the header's, so the button inside can be tall
// enough to aim at (36 - 2 = 34px) rather than the 20px the shared bar
// padding would leave.
#define SCREEN_ERRORS_ACTION_BAR_PAD_PX  1
#define SCREEN_ERRORS_CLEAR_BUTTON_W_PX  132
#define SCREEN_ERRORS_CLEAR_BUTTON_H_PX  (SCREEN_BAR_HEIGHT_PX - 2)

// The confirm prompt, sized and styled like the Saved Images delete prompt
// so the two questions look like the same question.
#define SCREEN_ERRORS_DIM_OPACITY        LV_OPA_70
#define SCREEN_ERRORS_DIALOG_W_PX        248
#define SCREEN_ERRORS_DIALOG_H_PX        116
#define SCREEN_ERRORS_DIALOG_BTN_W_PX    100
#define SCREEN_ERRORS_DIALOG_BTN_H_PX    30
#define SCREEN_ERRORS_DIALOG_BTN_X_PX    54
#define SCREEN_ERRORS_DIALOG_BTN_Y_PX    28

// Green for a clean board, red for a latched fault -- the same two colors
// system_screen.c gives its "Errors" row, so the summary here agrees with
// the row that sent the user to this screen.
#define SCREEN_ERRORS_OK_COLOR           0x30C030
#define SCREEN_ERRORS_FAULT_COLOR        0xE04040

// Longest flag name the list has to hold, plus the "- " prefix and the
// newline. The names come from error_handler_flag_names[]; the per-I2C-device
// ones ("MCP9804 (U1201) Configuration") are the long ones.
#define SCREEN_ERRORS_ENTRY_MAX_CHARS    48

// Worst case: every flag latched at once. Not hypothetical -- an unpowered
// FLIR module clamps SDA/SCL low (see flir.h), which fails every device on
// I2C1 and latches both generated flags for each of them in one go.
#define SCREEN_ERRORS_LIST_MAX_CHARS \
        (ERROR_HANDLER_NUM_FLAGS * SCREEN_ERRORS_ENTRY_MAX_CHARS)

static SCREEN_HEADER header;

static lv_obj_t *summary_label = NULL;
static lv_obj_t *list_label = NULL;
static lv_obj_t *confirm_overlay = NULL;

// Scratch the list text is built in before being compared against what the
// label already shows. Taken from the LVGL heap (DDR2) rather than being a
// static array, because at ~4KB it is worth keeping out of the 512KB of
// internal SRAM that the "Storage Usage?" report accounts for -- and the
// label's own copy of the string lives in that heap anyway.
static char *list_scratch = NULL;

// Counts the flags currently latched. The array covers the base flags and
// the two generated per I2C device alike (error_handler.h builds all of them
// from its X-macro lists), so this is every fault the firmware can record.
static uint32_t ScreenErrorsCountLatched(void)
{
    uint32_t count = 0;
    uint32_t index;

    for (index = 0; index < ERROR_HANDLER_NUM_FLAGS; index++)
    {
        if (error_handler.flag_array[index]) count++;
    }

    return count;
}

// Rebuilds the list text, then writes it only if it differs from what the
// label already holds.
//
// The comparison is the point: lv_label_set_text() invalidates
// unconditionally and reflows a wrapped multi-line label, so writing the
// same text twice a second would re-render this panel into DDR2 forever, in
// competition with the FLIR video path. Same principle as
// SystemScreenSetValue() and the UART live telemetry page.
static void ScreenErrorsRebuildList(uint32_t latched)
{
    size_t used = 0;
    uint32_t index;

    if ((list_label == NULL) || (list_scratch == NULL)) return;

    if (latched == 0)
    {
        // Empty, not a "nothing here" message: the summary line above
        // already says "No faults latched", and repeating it would read as
        // two different statements about the same thing.
        list_scratch[0] = '\0';
    }
    else
    {
        for (index = 0; index < ERROR_HANDLER_NUM_FLAGS; index++)
        {
            if (!error_handler.flag_array[index]) continue;

            // snprintf() returns what it WOULD have written, so advancing by
            // it unchecked walks the cursor past the end of the buffer. The
            // buffer is sized for every flag at once, so this cannot
            // truncate today -- the clamp is here so that a longer flag name
            // added later degrades into a cut-off list instead of a
            // buffer overrun.
            int written = snprintf(list_scratch + used,
                    SCREEN_ERRORS_LIST_MAX_CHARS - used,
                    "%s" LV_SYMBOL_BULLET " %s",
                    (used > 0) ? "\n" : "",
                    error_handler_flag_names[index]);

            if (written <= 0) break;

            used += (size_t)written;

            if (used >= (SCREEN_ERRORS_LIST_MAX_CHARS - 1u)) break;
        }
    }

    if (strcmp(lv_label_get_text(list_label), list_scratch) == 0) return;

    lv_label_set_text(list_label, list_scratch);
}

static void ScreenErrorsBackClicked(lv_event_t *event)
{
    (void)event;

    GUI_ShowScreen(GUI_SCREEN_MENU, GUI_NAV_BACK);
}

static void ScreenErrorsClearClicked(lv_event_t *event)
{
    (void)event;

    if (confirm_overlay == NULL) return;

    lv_obj_remove_flag(confirm_overlay, LV_OBJ_FLAG_HIDDEN);
}

static void ScreenErrorsConfirmCancelClicked(lv_event_t *event)
{
    (void)event;

    ScreenErrors_DismissPrompt();
}

static void ScreenErrorsConfirmClearClicked(lv_event_t *event)
{
    (void)event;

    // Throws away every latched flag. Note that this also drops the state
    // the error LEDs are driven from; updateErrorLEDs() runs from
    // heartbeatServices() and picks the cleared state up on its next pass.
    clearErrorHandler();

    ScreenErrors_DismissPrompt();

    // Redraw now rather than waiting up to 500ms for the next refresh -- the
    // list emptying out is the acknowledgement that the button did anything
    ScreenErrors_Refresh();
}

// The "clear all faults?" prompt. Structurally identical to the Saved Images
// delete prompt: a full-screen dim that absorbs stray taps, with a dialog on
// top of it, hidden until asked for.
static bool ScreenErrorsCreateConfirm(lv_obj_t *screen)
{
    lv_obj_t *dialog;
    lv_obj_t *clear_button;
    lv_obj_t *cancel_button;

    confirm_overlay = lv_obj_create(screen);
    if (confirm_overlay == NULL) return false;

    lv_obj_set_size(confirm_overlay, LV_HOR_RES, LV_VER_RES);
    lv_obj_align(confirm_overlay, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(confirm_overlay, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(confirm_overlay, SCREEN_ERRORS_DIM_OPACITY, LV_PART_MAIN);
    lv_obj_set_style_border_width(confirm_overlay, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(confirm_overlay, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(confirm_overlay, 0, LV_PART_MAIN);
    lv_obj_remove_flag(confirm_overlay, LV_OBJ_FLAG_SCROLLABLE);

    // Clickable with no callback, so a tap that misses both buttons is
    // absorbed here instead of reaching the Clear button under the dim
    lv_obj_add_flag(confirm_overlay, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_add_flag(confirm_overlay, LV_OBJ_FLAG_HIDDEN);

    dialog = lv_obj_create(confirm_overlay);
    if (dialog == NULL) return false;

    lv_obj_set_size(dialog, SCREEN_ERRORS_DIALOG_W_PX, SCREEN_ERRORS_DIALOG_H_PX);
    lv_obj_align(dialog, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(dialog, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(dialog, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(dialog, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(dialog, lv_color_hex(0xA0A0A0), LV_PART_MAIN);
    lv_obj_set_style_radius(dialog, 6, LV_PART_MAIN);
    lv_obj_remove_flag(dialog, LV_OBJ_FLAG_SCROLLABLE);

    if (Screen_CreateLabel(dialog, &lv_font_montserrat_14, LV_ALIGN_CENTER, 0, -34,
            "Clear all latched faults?") == NULL) return false;

    // Says what is actually lost. The faults themselves are not fixed by
    // this, and the ones that only latch during boot will not come back
    // until the next reset.
    if (Screen_CreateLabel(dialog, &lv_font_montserrat_14, LV_ALIGN_CENTER, 0, -12,
            "Diagnostic history is discarded") == NULL) return false;

    // Destructive answer on the left, the safe one on the right in the
    // ordinary chip styling -- matching the delete prompt
    clear_button = Screen_CreateButton(dialog, LV_ALIGN_CENTER,
            -SCREEN_ERRORS_DIALOG_BTN_X_PX, SCREEN_ERRORS_DIALOG_BTN_Y_PX,
            SCREEN_ERRORS_DIALOG_BTN_W_PX, SCREEN_ERRORS_DIALOG_BTN_H_PX,
            "Clear", ScreenErrorsConfirmClearClicked, NULL);

    cancel_button = Screen_CreateButton(dialog, LV_ALIGN_CENTER,
            SCREEN_ERRORS_DIALOG_BTN_X_PX, SCREEN_ERRORS_DIALOG_BTN_Y_PX,
            SCREEN_ERRORS_DIALOG_BTN_W_PX, SCREEN_ERRORS_DIALOG_BTN_H_PX,
            "Cancel", ScreenErrorsConfirmCancelClicked, NULL);

    if ((clear_button == NULL) || (cancel_button == NULL)) return false;

    lv_obj_set_style_border_color(clear_button, lv_color_hex(SCREEN_ERRORS_FAULT_COLOR),
            LV_PART_MAIN);
    lv_obj_set_style_border_opa(clear_button, LV_OPA_COVER, LV_PART_MAIN);

    return true;
}

lv_obj_t *ScreenErrors_Create(void)
{
    lv_obj_t *screen = Screen_Create();
    lv_obj_t *panel;
    lv_obj_t *action_bar;

    if (screen == NULL) return NULL;

    if (!Screen_CreateHeader(screen, "Diagnostics", &header)) return NULL;

    if (!Screen_AddBackButton(&header, ScreenErrorsBackClicked, NULL)) return NULL;

    list_scratch = lv_malloc(SCREEN_ERRORS_LIST_MAX_CHARS);
    if (list_scratch == NULL) return NULL;

    list_scratch[0] = '\0';

    // --- Body panel -------------------------------------------------------
    panel = lv_obj_create(screen);
    if (panel == NULL) return NULL;

    // Plain pixel arithmetic, NOT LV_PCT() minus an inset -- LV_PCT()
    // returns an encoded special value, so subtracting from it silently
    // changes the percentage instead of insetting anything.
    lv_obj_set_size(panel,
            LV_HOR_RES - (2 * SCREEN_ERRORS_PANEL_INSET_PX),
            LV_VER_RES - (2 * SCREEN_BAR_HEIGHT_PX) - (2 * SCREEN_ERRORS_PANEL_INSET_PX));

    // Centered, which leaves the bottom 42px of the panel clear for the
    // action bar below (the panel is sized against BOTH bars but only the
    // header is above it)
    lv_obj_align(panel, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(panel, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(panel, SCREEN_BAR_OPACITY, LV_PART_MAIN);
    lv_obj_set_style_border_width(panel, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(panel, SCREEN_ERRORS_PANEL_PAD_PX, LV_PART_MAIN);
    lv_obj_set_style_pad_right(panel, SCREEN_ERRORS_SCROLLBAR_LANE_PX, LV_PART_MAIN);

    // The list is as long as the number of latched faults, which can exceed
    // the panel, so it scrolls -- vertical only, so a slightly-off drag
    // cannot skew the text sideways.
    lv_obj_set_scroll_dir(panel, LV_DIR_VER);

    // ON rather than AUTO: AUTO only shows the bar during a scroll, which
    // leaves no hint that there is more below the fold.
    lv_obj_set_scrollbar_mode(panel, LV_SCROLLBAR_MODE_ON);

    summary_label = Screen_CreateLabel(panel, &lv_font_montserrat_14,
            LV_ALIGN_TOP_LEFT, 0, SCREEN_ERRORS_SUMMARY_Y_PX, "--");
    if (summary_label == NULL) return NULL;

    list_label = Screen_CreateLabel(panel, &lv_font_montserrat_14,
            LV_ALIGN_TOP_LEFT, 0, SCREEN_ERRORS_LIST_Y_PX, "");
    if (list_label == NULL) return NULL;

    // Wrapped to the panel's content width rather than running off the right
    // edge: the generated per-device names are long, and this is the one
    // screen whose whole job is that the text can be read.
    lv_obj_set_width(list_label, SCREEN_ERRORS_CONTENT_W_PX);
    lv_label_set_long_mode(list_label, LV_LABEL_LONG_WRAP);

    lv_obj_set_style_text_color(list_label, lv_color_hex(SCREEN_ERRORS_FAULT_COLOR),
            LV_PART_MAIN);

    // --- Action bar -------------------------------------------------------
    // Outside the panel, so the one control on this screen cannot scroll out
    // of reach behind a long fault list.
    action_bar = Screen_CreateBar(screen, LV_ALIGN_BOTTOM_MID);
    if (action_bar == NULL) return NULL;

    lv_obj_set_style_pad_top(action_bar, SCREEN_ERRORS_ACTION_BAR_PAD_PX, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(action_bar, SCREEN_ERRORS_ACTION_BAR_PAD_PX, LV_PART_MAIN);

    if (Screen_CreateButton(action_bar, LV_ALIGN_RIGHT_MID, 0, 0,
            SCREEN_ERRORS_CLEAR_BUTTON_W_PX, SCREEN_ERRORS_CLEAR_BUTTON_H_PX,
            "Clear Errors", ScreenErrorsClearClicked, NULL) == NULL) return NULL;

    // --- Confirm prompt ---------------------------------------------------
    // Last, so it is above the panel and the action bar both
    if (!ScreenErrorsCreateConfirm(screen)) return NULL;

    ScreenErrors_Refresh();

    return screen;
}

void ScreenErrors_Refresh(void)
{
    uint32_t latched;

    // ScreenErrors_Create() either finishes or leaves these NULL
    if ((summary_label == NULL) || (list_label == NULL)) return;

    Screen_RefreshHeader(&header);

    latched = ScreenErrorsCountLatched();

    if (latched == 0)
    {
        lv_label_set_text(summary_label, "No faults latched");
        lv_obj_set_style_text_color(summary_label, lv_color_hex(SCREEN_ERRORS_OK_COLOR),
                LV_PART_MAIN);
    }
    else
    {
        char text[40];

        // The denominator is worth showing: it says how much the firmware
        // actually watches, which is the context for "3 latched" meaning
        // three out of eighty-one rather than three out of three.
        snprintf(text, sizeof(text), "%lu of %lu faults latched",
                (unsigned long)latched, (unsigned long)ERROR_HANDLER_NUM_FLAGS);

        lv_label_set_text(summary_label, text);
        lv_obj_set_style_text_color(summary_label, lv_color_hex(SCREEN_ERRORS_FAULT_COLOR),
                LV_PART_MAIN);
    }

    ScreenErrorsRebuildList(latched);
}

void ScreenErrors_DismissPrompt(void)
{
    if (confirm_overlay == NULL) return;

    lv_obj_add_flag(confirm_overlay, LV_OBJ_FLAG_HIDDEN);
}
