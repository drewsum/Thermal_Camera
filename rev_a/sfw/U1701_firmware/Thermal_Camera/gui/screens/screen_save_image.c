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

#include "gui/lvgl/lvgl.h"
#include "application/still_capture.h"
#include "sdhc/sd_fileio.h"

// The prompt sits above center so it clears the middle of the captured image
// -- the point of the screen is that the user can still see what they took.
#define SCREEN_SAVE_IMAGE_PROMPT_Y_PX   -22
#define SCREEN_SAVE_IMAGE_STATUS_Y_PX    12

// Green once the file is on the card, amber while nothing has been written
// yet, red when the write was attempted and failed.
#define SCREEN_SAVE_IMAGE_OK_COLOR      0x30C030
#define SCREEN_SAVE_IMAGE_BUSY_COLOR    0xFFA500
#define SCREEN_SAVE_IMAGE_FAIL_COLOR    0xE04040

static SCREEN_HEADER header;
static lv_obj_t *prompt_label = NULL;
static lv_obj_t *status_label = NULL;

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

lv_obj_t *ScreenSaveImage_Create(void)
{
    lv_obj_t *screen = Screen_Create();
    lv_obj_t *bottom_bar;
    lv_obj_t *save_label;
    lv_obj_t *cancel_label;

    if (screen == NULL) return NULL;

    if (!Screen_CreateHeader(screen, "Save Image", &header)) return NULL;

    // --- Prompt, over the frozen capture -----------------------------------
    prompt_label = ScreenSaveImageCreateChipLabel(screen, &lv_font_montserrat_20,
            SCREEN_SAVE_IMAGE_PROMPT_Y_PX, "Save this image?");
    if (prompt_label == NULL) return NULL;

    status_label = ScreenSaveImageCreateChipLabel(screen, &lv_font_montserrat_14,
            SCREEN_SAVE_IMAGE_STATUS_Y_PX, "--");
    if (status_label == NULL) return NULL;

    // --- Footer: the two choices -------------------------------------------
    bottom_bar = Screen_CreateBar(screen, LV_ALIGN_BOTTOM_MID);
    if (bottom_bar == NULL) return NULL;

    save_label = Screen_CreateLabel(bottom_bar, &lv_font_montserrat_14,
            LV_ALIGN_LEFT_MID, 0, 0, "Save to SD");
    cancel_label = Screen_CreateLabel(bottom_bar, &lv_font_montserrat_14,
            LV_ALIGN_RIGHT_MID, 0, 0, "Cancel");

    if ((save_label == NULL) || (cancel_label == NULL)) return NULL;

    ScreenSaveImage_Refresh();

    return screen;
}

void ScreenSaveImage_Refresh(void)
{
    char text[48];
    uint32_t color;

    // ScreenSaveImage_Create() either finishes or leaves this NULL
    if (status_label == NULL) return;

    Screen_RefreshHeader(&header);

    switch (StillCapture_GetState())
    {
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
