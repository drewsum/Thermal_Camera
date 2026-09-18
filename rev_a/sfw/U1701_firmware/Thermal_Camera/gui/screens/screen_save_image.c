/*******************************************************************************
  Save-Image GUI Screen

  File Name:
    screen_save_image.c

  Summary:
    See screen_save_image.h for the layout and for why the footer entries are
    labels rather than buttons.
*******************************************************************************/

#include <stdio.h>

#include "gui/screens/screen_save_image.h"
#include "gui/screens/screen_common.h"

#include "gui/gui.h"
#include "gui/lvgl/lvgl.h"
#include "application/still_capture.h"
#include "sdhc/sd_fileio.h"

// The prompt sits above center so it clears the middle of the captured image
// -- the point of the screen is that the user can still see what they took.
#define SCREEN_SAVE_IMAGE_PROMPT_Y_PX   -22
#define SCREEN_SAVE_IMAGE_STATUS_Y_PX    12

// The legend checkbox sits under the status line, in the clear band above
// the footer bar. Its left edge is well right of the legend itself (which
// the frozen image now carries at x 0..72), so the control never covers the
// thing it is about.
#define SCREEN_SAVE_IMAGE_LEGEND_Y_PX    48
#define SCREEN_SAVE_IMAGE_LEGEND_W_PX    170

// Green once the file is on the card, amber while nothing has been written
// yet, red when the write was attempted and failed.
#define SCREEN_SAVE_IMAGE_OK_COLOR      0x30C030
#define SCREEN_SAVE_IMAGE_BUSY_COLOR    0xFFA500
#define SCREEN_SAVE_IMAGE_FAIL_COLOR    0xE04040

// Footer button geometry. Sized to the bar's full height rather than to the
// text, so both are comfortable tap targets -- see Screen_CreateButton().
#define SCREEN_SAVE_IMAGE_SAVE_BUTTON_W_PX    104
#define SCREEN_SAVE_IMAGE_CANCEL_BUTTON_W_PX   80

static SCREEN_HEADER header;
static lv_obj_t *prompt_label = NULL;
static lv_obj_t *status_label = NULL;
static lv_obj_t *save_button = NULL;
static lv_obj_t *cancel_button = NULL;
static lv_obj_t *legend_checkbox = NULL;

// A label with its own translucent backing chip, so text stays readable over
// an arbitrary thermal scene. Same treatment (and the same reason) as the
// home screen's readout chips, which is why the two look alike.
static lv_obj_t *ScreenSaveImageCreateChipLabel(lv_obj_t *parent, const lv_font_t *font,
        int32_t y_offset, const char *text)
{
    lv_obj_t *label = lv_label_create(parent);

    if (label == NULL) return NULL;

    lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_color(label, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(label, SCREEN_BAR_OPACITY, LV_PART_MAIN);
    lv_obj_set_style_radius(label, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(label, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(label, 3, LV_PART_MAIN);
    lv_label_set_text(label, text);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, y_offset);

    return label;
}

// Turns the baked-in palette legend on or off. still_capture.c repaints the
// held image, so the panel behind this screen updates under the user's
// finger and the prompt stays a true preview of the file.
//
// Screen_CreateCheckbox() has already flipped the state by the time this
// runs, so the widget is the source of truth here rather than the module.
static void ScreenSaveImageLegendToggled(lv_event_t *event)
{
    (void)event;

    StillCapture_SetSaveLegend(Screen_IsCheckboxChecked(legend_checkbox));
}

// Commits the held frame to the card. The encode runs a few main-loop passes
// later, so this returns straight away and the status line carries the
// progress -- the screen deliberately stays up rather than dismissing itself,
// so the outcome (filename, or the reason it failed) is actually readable.
static void ScreenSaveImageSaveClicked(lv_event_t *event)
{
    (void)event;

    StillCapture_ConfirmSave();
}

// Declines the prompt, or dismisses the screen once a save has finished --
// the same action either way: drop the still, restart the video, go home.
// Wired to the footer "Cancel"/"Done" button AND to the header's back button.
static void ScreenSaveImageCancelClicked(lv_event_t *event)
{
    (void)event;

    StillCapture_Resume();

    // Resume() restarts the video but deliberately leaves the screen alone,
    // so the navigation back to the live view belongs here
    GUI_ShowScreen(GUI_SCREEN_HOME, GUI_NAV_BACK);
}

lv_obj_t *ScreenSaveImage_Create(void)
{
    lv_obj_t *screen = Screen_Create();
    lv_obj_t *bottom_bar;

    if (screen == NULL) return NULL;

    if (!Screen_CreateHeader(screen, "Save Image", &header)) return NULL;

    // Back is a second way to decline, per the usual "back gets me out of
    // here" expectation -- identical to Cancel
    if (!Screen_AddBackButton(&header, ScreenSaveImageCancelClicked, NULL)) return NULL;

    // --- Prompt, over the frozen capture -----------------------------------
    prompt_label = ScreenSaveImageCreateChipLabel(screen, &lv_font_montserrat_20,
            SCREEN_SAVE_IMAGE_PROMPT_Y_PX, "Save this image?");
    if (prompt_label == NULL) return NULL;

    status_label = ScreenSaveImageCreateChipLabel(screen, &lv_font_montserrat_14,
            SCREEN_SAVE_IMAGE_STATUS_Y_PX, "--");
    if (status_label == NULL) return NULL;

    legend_checkbox = Screen_CreateCheckbox(screen, LV_ALIGN_CENTER, 0,
            SCREEN_SAVE_IMAGE_LEGEND_Y_PX, SCREEN_SAVE_IMAGE_LEGEND_W_PX,
            SCREEN_TOUCH_TARGET_MIN_PX, "Include legend",
            StillCapture_GetSaveLegend(), ScreenSaveImageLegendToggled, NULL);
    if (legend_checkbox == NULL) return NULL;

    // --- Footer: the two choices -------------------------------------------
    bottom_bar = Screen_CreateBar(screen, LV_ALIGN_BOTTOM_MID);
    if (bottom_bar == NULL) return NULL;

    save_button = Screen_CreateButton(bottom_bar, LV_ALIGN_LEFT_MID, 0, 0,
            SCREEN_SAVE_IMAGE_SAVE_BUTTON_W_PX, SCREEN_TOUCH_TARGET_MIN_PX,
            "Save to SD", ScreenSaveImageSaveClicked, NULL);

    cancel_button = Screen_CreateButton(bottom_bar, LV_ALIGN_RIGHT_MID, 0, 0,
            SCREEN_SAVE_IMAGE_CANCEL_BUTTON_W_PX, SCREEN_TOUCH_TARGET_MIN_PX,
            "Cancel", ScreenSaveImageCancelClicked, NULL);

    if ((save_button == NULL) || (cancel_button == NULL)) return NULL;

    ScreenSaveImage_Refresh();

    return screen;
}

// Shows or hides `object` in one call -- the visibility of three of this
// screen's widgets is driven entirely by the capture state below.
static void ScreenSaveImageSetVisible(lv_obj_t *object, bool visible)
{
    if (object == NULL) return;

    if (visible) lv_obj_remove_flag(object, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(object, LV_OBJ_FLAG_HIDDEN);
}

void ScreenSaveImage_Refresh(void)
{
    char text[48];
    uint32_t color;
    STILL_CAPTURE_STATE state = StillCapture_GetState();

    // ScreenSaveImage_Create() either finishes or leaves this NULL
    if (status_label == NULL) return;

    Screen_RefreshHeader(&header);

    // The controls track the state:
    //  - PROMPTING: the actual question -- both choices offered.
    //  - SAVING:    the choice is made and the write is committed, so
    //               offering it again would be a lie; the encode also blocks
    //               the main loop, so a tap could not be serviced anyway.
    //  - SAVED/FAILED: nothing left to decide. "Cancel" would read as
    //               "undo", which this cannot do, so it becomes "Done" --
    //               same action (drop the still, resume live video), honest
    //               label.
    ScreenSaveImageSetVisible(prompt_label, state == STILL_CAPTURE_PROMPTING);
    ScreenSaveImageSetVisible(save_button,  state == STILL_CAPTURE_PROMPTING);
    ScreenSaveImageSetVisible(cancel_button, state != STILL_CAPTURE_SAVING);

    // Same rule as the Save button: the legend is only still a choice while
    // the question is open. Its state is pulled from still_capture.c rather
    // than left as the user last set it -- every capture resets that to
    // "included", and this is where the checkbox picks the reset up.
    ScreenSaveImageSetVisible(legend_checkbox, state == STILL_CAPTURE_PROMPTING);
    Screen_SetCheckboxChecked(legend_checkbox, StillCapture_GetSaveLegend());

    Screen_SetButtonText(cancel_button,
            (state == STILL_CAPTURE_PROMPTING) ? "Cancel" : "Done");

    switch (state)
    {
        case STILL_CAPTURE_PROMPTING:
            // The prompt label above is carrying the question, so the status
            // line names where it would go rather than repeating it
            snprintf(text, sizeof(text), "%s",
                    SDFileIO_IsMounted() ? "SD card ready"
                                         : "No SD card -- cannot save");
            color = SDFileIO_IsMounted() ? SCREEN_SAVE_IMAGE_OK_COLOR
                                         : SCREEN_SAVE_IMAGE_FAIL_COLOR;
            break;

        case STILL_CAPTURE_SAVED:
            snprintf(text, sizeof(text), "Saved to SD: %s", StillCapture_GetSavedName());
            color = SCREEN_SAVE_IMAGE_OK_COLOR;
            break;

        case STILL_CAPTURE_FAILED:
            // The two ways this lands are worth telling apart on the panel:
            // no card in the slot is the user's to fix, anything else needs
            // the console message image_saver.c already printed.
            snprintf(text, sizeof(text), "%s",
                    SDFileIO_IsMounted() ? "Save failed -- see USB console"
                                         : "No SD card -- not saved");
            color = SCREEN_SAVE_IMAGE_FAIL_COLOR;
            break;

        case STILL_CAPTURE_SAVING:
            snprintf(text, sizeof(text), "Saving to SD card...");
            color = SCREEN_SAVE_IMAGE_BUSY_COLOR;
            break;

        case STILL_CAPTURE_IDLE:
        default:
            // Only reachable before the first capture (the screen is built at
            // boot like every other one) -- it is never shown in this state.
            snprintf(text, sizeof(text), "No image captured");
            color = SCREEN_SAVE_IMAGE_BUSY_COLOR;
            break;
    }

    lv_label_set_text(status_label, text);
    lv_obj_set_style_text_color(status_label, lv_color_hex(color), LV_PART_MAIN);
}
