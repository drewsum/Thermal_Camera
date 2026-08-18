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

    // The date/time stack two 14pt lines (16px line height each, 32px
    // total) in the bar's right corner, but Screen_CreateBar's shared
    // vertical padding only leaves 20px of content height for them
    // (SCREEN_BAR_HEIGHT_PX 36 - 2*SCREEN_BAR_PADDING_PX 8) -- too little,
    // which is why they used to overlap. Tightened here rather than in
    // Screen_CreateBar itself so single-line bars elsewhere (the home
    // screen's footer) keep the more generous shared padding. 1px top/bottom
    // leaves 34px of content, just 2px more than the two lines need.
    lv_obj_set_style_pad_top(header->bar, 1, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(header->bar, 1, LV_PART_MAIN);

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

lv_obj_t *Screen_CreateButton(lv_obj_t *parent, lv_align_t align,
        int32_t x_offset, int32_t y_offset, int32_t w, int32_t h,
        const char *text, lv_event_cb_t cb, void *user_data)
{
    lv_obj_t *button = lv_obj_create(parent);
    lv_obj_t *label;

    if (button == NULL) return NULL;

    if ((w > 0) && (h > 0)) lv_obj_set_size(button, w, h);
    else lv_obj_set_size(button, LV_SIZE_CONTENT, LV_SIZE_CONTENT);

    lv_obj_align(button, align, x_offset, y_offset);

    // Slightly lighter than the bars it sits on, so a control reads as a
    // control rather than as more furniture
    lv_obj_set_style_bg_color(button, lv_color_hex(0x303030), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(button, SCREEN_BAR_OPACITY, LV_PART_MAIN);
    lv_obj_set_style_border_width(button, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(button, lv_color_hex(0xA0A0A0), LV_PART_MAIN);
    lv_obj_set_style_border_opa(button, LV_OPA_50, LV_PART_MAIN);
    lv_obj_set_style_radius(button, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(button, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(button, 2, LV_PART_MAIN);

    // Touch feedback. Without this a tap gives no acknowledgement at all
    // until the screen changes, which reads as a dropped press.
    lv_obj_set_style_bg_color(button, lv_color_hex(0x0060C0), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(button, LV_OPA_80, LV_STATE_PRESSED);

    lv_obj_remove_flag(button, LV_OBJ_FLAG_SCROLLABLE);

    label = Screen_CreateLabel(button, &lv_font_montserrat_14,
            LV_ALIGN_CENTER, 0, 0, text);

    if (label == NULL) return NULL;

    // The label would otherwise swallow the press before it reaches the
    // chip -- children are hit-tested first, and labels are clickable in
    // LVGL v9 like every other object.
    lv_obj_remove_flag(label, LV_OBJ_FLAG_CLICKABLE);

    if (cb != NULL) lv_obj_add_event_cb(button, cb, LV_EVENT_CLICKED, user_data);

    return button;
}

bool Screen_AddBackButton(SCREEN_HEADER *header, lv_event_cb_t cb, void *user_data)
{
    lv_obj_t *back;

    if ((header->bar == NULL) || (header->title_label == NULL)) return false;

    // Fills the bar's content height (the header trims its vertical padding
    // to 1px for the date/time stack, so this is 34px -- above the touch
    // target floor without spilling out of the bar).
    back = Screen_CreateButton(header->bar, LV_ALIGN_LEFT_MID, 0, 0,
            SCREEN_BACK_BUTTON_WIDTH_PX, SCREEN_BAR_HEIGHT_PX - 2,
            LV_SYMBOL_LEFT, cb, user_data);

    if (back == NULL) return false;

    // Move the title clear of the button. Screen_CreateHeader() aligned it
    // to LV_ALIGN_LEFT_MID with no offset, so this is the same alignment
    // with the button's width plus a gap.
    lv_obj_align(header->title_label, LV_ALIGN_LEFT_MID,
            SCREEN_BACK_BUTTON_WIDTH_PX + SCREEN_BACK_BUTTON_GAP_PX, 0);

    return true;
}

void Screen_RefreshHeader(SCREEN_HEADER *header)
{
    char text[16];

    if ((header->date_label == NULL) || (header->time_label == NULL)) return;

    // rtcc_shadow is maintained by the RTCC interrupt (core/rtcc.c) in plain
    // binary -- no BCD conversion or register reads needed here. Its year
    // field already includes the 2000. American format (MM-DD-YYYY) to
    // match how this instrument's operators read a date.
    snprintf(text, sizeof(text), "%02u-%02u-%04u",
            (unsigned int)rtcc_shadow.month,
            (unsigned int)rtcc_shadow.day,
            (unsigned int)rtcc_shadow.year);
    lv_label_set_text(header->date_label, text);

    snprintf(text, sizeof(text), "%02u:%02u:%02u",
            (unsigned int)rtcc_shadow.hours,
            (unsigned int)rtcc_shadow.minutes,
            (unsigned int)rtcc_shadow.seconds);
    lv_label_set_text(header->time_label, text);
}
