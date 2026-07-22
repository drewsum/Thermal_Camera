/*******************************************************************************
  Shared Screen Furniture

  File Name:
    screen_common.c

  Summary:
    See screen_common.h.
*******************************************************************************/

#include <stdio.h>

#include "gui/screens/screen_common.h"

#include "core/rtcc.h"

lv_obj_t *Screen_Create(void)
{
    // Parent NULL = a screen object, which exists independently of the
    // display until lv_screen_load() shows it
    lv_obj_t *screen = lv_obj_create(NULL);

    if (screen == NULL) return NULL;

    // Fully transparent: this is an overlay layer, so "no background" means
    // the Layer 0 image shows through untouched. Without it the theme's
    // opaque screen background would hide the image entirely.
    lv_obj_set_style_bg_opa(screen, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(screen, 0, LV_PART_MAIN);

    // The default theme gives the screen padding, which LV_PCT(100) and
    // LV_ALIGN_* both measure against -- bars would end up inset from the
    // panel edges rather than spanning it
    lv_obj_set_style_pad_all(screen, 0, LV_PART_MAIN);

    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    return screen;
}

lv_obj_t *Screen_CreateBar(lv_obj_t *parent, lv_align_t align)
{
    lv_obj_t *bar = lv_obj_create(parent);

    if (bar == NULL) return NULL;

    lv_obj_set_size(bar, LV_PCT(100), SCREEN_BAR_HEIGHT_PX);
    lv_obj_align(bar, align, 0, 0);

    lv_obj_set_style_bg_color(bar, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar, SCREEN_BAR_OPACITY, LV_PART_MAIN);
    lv_obj_set_style_border_width(bar, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(bar, SCREEN_BAR_PADDING_PX, LV_PART_MAIN);

    // No scrolling: these are static containers, and a stray drag on a
    // future touch build shouldn't be able to shift them
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    return bar;
}

lv_obj_t *Screen_CreateLabel(lv_obj_t *parent, const lv_font_t *font,
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

bool Screen_CreateHeader(lv_obj_t *screen, const char *title, SCREEN_HEADER *header)
{
    header->bar = Screen_CreateBar(screen, LV_ALIGN_TOP_MID);
    if (header->bar == NULL) return false;

    header->title_label = Screen_CreateLabel(header->bar, &lv_font_montserrat_14,
            LV_ALIGN_LEFT_MID, 0, 0, title);

    // Placeholder text sized like the real thing, so the labels are laid
    // out at their final width before the first refresh
    header->date_label = Screen_CreateLabel(header->bar, &lv_font_montserrat_14,
            LV_ALIGN_TOP_RIGHT, 0, 0, "----------");
    header->time_label = Screen_CreateLabel(header->bar, &lv_font_montserrat_14,
            LV_ALIGN_BOTTOM_RIGHT, 0, 0, "--:--:--");

    if ((header->title_label == NULL) || (header->date_label == NULL) ||
        (header->time_label == NULL)) return false;

    Screen_RefreshHeader(header);

    return true;
}

void Screen_RefreshHeader(SCREEN_HEADER *header)
{
    char text[16];

    if ((header->date_label == NULL) || (header->time_label == NULL)) return;

    // rtcc_shadow is maintained by the RTCC interrupt (core/rtcc.c) in plain
    // binary -- no BCD conversion or register reads needed here. Its year
    // field already includes the 2000.
    snprintf(text, sizeof(text), "%04u-%02u-%02u",
            (unsigned int)rtcc_shadow.year,
            (unsigned int)rtcc_shadow.month,
            (unsigned int)rtcc_shadow.day);
    lv_label_set_text(header->date_label, text);

    snprintf(text, sizeof(text), "%02u:%02u:%02u",
            (unsigned int)rtcc_shadow.hours,
            (unsigned int)rtcc_shadow.minutes,
            (unsigned int)rtcc_shadow.seconds);
    lv_label_set_text(header->time_label, text);
}
