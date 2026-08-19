/*******************************************************************************
  USB GUI Screen

  File Name:
    screen_usb.c

  Summary:
    See screen_usb.h.
*******************************************************************************/

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "gui/screens/screen_usb.h"
#include "gui/screens/screen_common.h"

#include "gui/gui.h"
#include "gui/lvgl/lvgl.h"

#include "usb/usb.h"
#include "usb/device_driver/usb_msd.h"
#include "sdhc/sd_fileio.h"
#include "spi/flash_fileio.h"

// Body panel geometry, matching system_screen.c and screen_sd_card.c so the
// three read as the same instrument -- the content is taller than the panel,
// so it scrolls.
#define USB_SCREEN_PANEL_INSET_PX      6
#define USB_SCREEN_PANEL_PAD_PX        6
#define USB_SCREEN_ROW_HEIGHT_PX       18
#define USB_SCREEN_SECTION_HEIGHT_PX   20
#define USB_SCREEN_SECTION_GAP_PX      6

// Right-hand lane kept clear for the scrollbar the default theme draws
// inside the panel's right edge -- see system_screen.c, same reasoning.
#define USB_SCREEN_SCROLLBAR_LANE_PX   14

// Longest value string this screen formats, plus room to grow
#define USB_SCREEN_VALUE_MAX_CHARS     32

// The action bar's vertical padding is trimmed the same way
// Screen_CreateHeader() trims the header's, so the button inside can be tall
// enough to aim at (36 - 2 = 34px) rather than the 20px the shared bar
// padding would leave.
#define USB_SCREEN_ACTION_BAR_PAD_PX   1
#define USB_SCREEN_ACTION_BUTTON_W_PX  110
#define USB_SCREEN_ACTION_BUTTON_H_PX  (SCREEN_BAR_HEIGHT_PX - 2)

// *****************************************************************************
// Section: Row list
// *****************************************************************************
// Every row on the screen, top to bottom, in one list -- the same X-macro
// idiom as system_screen.c and screen_sd_card.c. SECTION() is a heading with
// no value; VALUE() is a name/value pair whose enum tag names the label the
// refresh writes into.
//
// Between them these carry the parts of "USB Status?" that describe what the
// device is doing, as opposed to the raw USBCSR/USBE1CSR register dumps that
// command also prints -- those are a debugger's view and belong on a
// terminal, not on a 320x240 panel.
#define USB_SCREEN_ROW_LIST(SECTION, VALUE)                        \
    SECTION("Bus")                                                 \
    VALUE(STATE,          "State")                                 \
    VALUE(SPEED,          "Speed")                                 \
    VALUE(MAX_PACKET,     "Bulk Max Packet")                       \
    SECTION("Mass Storage")                                        \
    VALUE(MEDIA_OWNER,    "Media Owner")                           \
    VALUE(LUN_SD,         "LUN 0 (microSD)")                       \
    VALUE(LUN_FLASH,      "LUN 1 (SPI Flash)")                     \
    SECTION("Bus Events")                                          \
    VALUE(BUS_RESETS,     "Bus Resets")                            \
    VALUE(SUSPENDS,       "Suspends")                              \
    VALUE(RESUMES,        "Resumes")                               \
    VALUE(DISCONNECTS,    "Disconnects")                           \
    VALUE(VBUS_ERRORS,    "VBUS Errors")                           \
    VALUE(SETUP_PACKETS,  "SETUP Packets")                         \
    VALUE(EP0_STALLS,     "EP0 Stalls")

// The value rows only -- this is what indexes value_labels[] below
#define USB_ROW_ENUM_SECTION(label)
#define USB_ROW_ENUM_VALUE(name, label)   USB_ROW_##name,

typedef enum
{
    USB_SCREEN_ROW_LIST(USB_ROW_ENUM_SECTION, USB_ROW_ENUM_VALUE)
    USB_ROW_COUNT
} USB_SCREEN_ROW;

// Every row in display order, sections included, so the build loop can lay
// them out in one pass. `value_row` is meaningless when is_section is true.
typedef struct
{
    bool is_section;
    const char *label;
    USB_SCREEN_ROW value_row;
} USB_SCREEN_ROW_DEF;

#define USB_ROW_DEF_SECTION(label)        { true,  label, 0 },
#define USB_ROW_DEF_VALUE(name, label)    { false, label, USB_ROW_##name },

static const USB_SCREEN_ROW_DEF usb_rows[] =
{
    USB_SCREEN_ROW_LIST(USB_ROW_DEF_SECTION, USB_ROW_DEF_VALUE)
};

#define USB_SCREEN_ROW_TOTAL  (sizeof(usb_rows) / sizeof(usb_rows[0]))

static SCREEN_HEADER header;
static lv_obj_t *value_labels[USB_ROW_COUNT] = { NULL };
static lv_obj_t *action_button = NULL;

// Writes a row's value, but only when the text actually changed --
// lv_label_set_text() invalidates unconditionally, and a blind rewrite would
// re-render every row of the panel twice a second into DDR2, in competition
// with the FLIR video path. Same helper (and same reasoning) as
// system_screen.c's.
static void ScreenUSBSetValue(USB_SCREEN_ROW row, const char *text)
{
    lv_obj_t *label = value_labels[row];

    if (label == NULL) return;
    if (strcmp(lv_label_get_text(label), text) == 0) return;

    lv_label_set_text(label, text);
}

static void ScreenUSBSetValueFmt(USB_SCREEN_ROW row, const char *format, ...)
{
    char text[USB_SCREEN_VALUE_MAX_CHARS];
    va_list args;

    va_start(args, format);
    vsnprintf(text, sizeof(text), format, args);
    va_end(args);

    ScreenUSBSetValue(row, text);
}

static void ScreenUSBBackClicked(lv_event_t *event)
{
    (void)event;

    GUI_ShowScreen(GUI_SCREEN_MENU, GUI_NAV_BACK);
}

// One button for both directions, dispatching on the state it is labelled
// from. Reading usb_device_state again here rather than trusting the label
// keeps the two from disagreeing if the bus changed between the last refresh
// and the tap.
static void ScreenUSBActionClicked(lv_event_t *event)
{
    (void)event;

    if (usb_device_state == USB_STATE_DETACHED)
    {
        USB_Attach();
    }
    else
    {
        // Soft disconnect: the host sees an unplug, and the SD/flash volumes
        // come back under local control (USB_MSD_DetachHook() remounts them)
        USB_Detach();
    }

    // Re-label and re-read now rather than waiting up to 500ms -- the button
    // changing to its opposite is the acknowledgement that the tap landed
    ScreenUSB_Refresh();
}

lv_obj_t *ScreenUSB_Create(void)
{
    lv_obj_t *screen = Screen_Create();
    lv_obj_t *panel;
    lv_obj_t *action_bar;
    uint32_t index;
    int32_t y = 0;

    if (screen == NULL) return NULL;

    if (!Screen_CreateHeader(screen, "USB", &header)) return NULL;

    if (!Screen_AddBackButton(&header, ScreenUSBBackClicked, NULL)) return NULL;

    // --- Body panel -------------------------------------------------------
    panel = lv_obj_create(screen);
    if (panel == NULL) return NULL;

    // Plain pixel arithmetic, NOT LV_PCT() minus an inset -- LV_PCT()
    // returns an encoded special value, so subtracting from it silently
    // changes the percentage instead of insetting anything.
    lv_obj_set_size(panel,
            LV_HOR_RES - (2 * USB_SCREEN_PANEL_INSET_PX),
            LV_VER_RES - (2 * SCREEN_BAR_HEIGHT_PX) - (2 * USB_SCREEN_PANEL_INSET_PX));

    // Centered, which leaves the bottom 42px clear for the action bar below
    // (the panel is sized against BOTH bars but only the header is above it)
    lv_obj_align(panel, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(panel, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(panel, SCREEN_BAR_OPACITY, LV_PART_MAIN);
    lv_obj_set_style_border_width(panel, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(panel, USB_SCREEN_PANEL_PAD_PX, LV_PART_MAIN);
    lv_obj_set_style_pad_right(panel, USB_SCREEN_SCROLLBAR_LANE_PX, LV_PART_MAIN);

    // Vertical only: the value labels are right-aligned, and letting the
    // panel scroll horizontally would make a slightly-off vertical drag skew
    // the whole list sideways.
    lv_obj_set_scroll_dir(panel, LV_DIR_VER);

    // ON rather than AUTO: AUTO only shows the bar during a scroll, which
    // leaves no hint that there is more below the fold.
    lv_obj_set_scrollbar_mode(panel, LV_SCROLLBAR_MODE_ON);

    // --- Rows -------------------------------------------------------------
    // Name on the left, value on the right, with section headings between
    // groups. Explicit y-offsets rather than a layout engine: the row set is
    // fixed at build time and this keeps flex out of the flash budget.
    for (index = 0; index < USB_SCREEN_ROW_TOTAL; index++)
    {
        const USB_SCREEN_ROW_DEF *row = &usb_rows[index];

        if (row->is_section)
        {
            lv_obj_t *section;

            if (index != 0) y += USB_SCREEN_SECTION_GAP_PX;

            section = Screen_CreateLabel(panel, &lv_font_montserrat_14,
                    LV_ALIGN_TOP_LEFT, 0, y, row->label);

            if (section == NULL) return NULL;

            // The same tint system_screen.c gives its headings, so the
            // grouping reads identically across the two screens
            lv_obj_set_style_text_color(section, lv_color_hex(0x60C0FF), LV_PART_MAIN);

            y += USB_SCREEN_SECTION_HEIGHT_PX;
        }
        else
        {
            if (Screen_CreateLabel(panel, &lv_font_montserrat_14,
                    LV_ALIGN_TOP_LEFT, 0, y, row->label) == NULL) return NULL;

            value_labels[row->value_row] = Screen_CreateLabel(panel,
                    &lv_font_montserrat_14, LV_ALIGN_TOP_RIGHT, 0, y, "--");

            if (value_labels[row->value_row] == NULL) return NULL;

            y += USB_SCREEN_ROW_HEIGHT_PX;
        }
    }

    // --- Action bar -------------------------------------------------------
    // Outside the panel, so the one control on this screen cannot scroll out
    // of reach behind the counter rows.
    action_bar = Screen_CreateBar(screen, LV_ALIGN_BOTTOM_MID);
    if (action_bar == NULL) return NULL;

    lv_obj_set_style_pad_top(action_bar, USB_SCREEN_ACTION_BAR_PAD_PX, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(action_bar, USB_SCREEN_ACTION_BAR_PAD_PX, LV_PART_MAIN);

    // Labelled from the live state by the first refresh below
    action_button = Screen_CreateButton(action_bar, LV_ALIGN_RIGHT_MID, 0, 0,
            USB_SCREEN_ACTION_BUTTON_W_PX, USB_SCREEN_ACTION_BUTTON_H_PX,
            "Detach", ScreenUSBActionClicked, NULL);

    if (action_button == NULL) return NULL;

    ScreenUSB_Refresh();

    return screen;
}

void ScreenUSB_Refresh(void)
{
    usb_device_state_t state;

    // ScreenUSB_Create() either finishes or leaves these NULL
    if ((value_labels[USB_ROW_STATE] == NULL) || (action_button == NULL)) return;

    Screen_RefreshHeader(&header);

    // --- Bus --------------------------------------------------------------
    // Snapshotted once: this drives both the state row and the button label,
    // and the ISR can advance it between two reads
    state = usb_device_state;

    ScreenUSBSetValue(USB_ROW_STATE, USB_GetStateString());

    // Only meaningful once the host has actually reset the bus and
    // negotiated -- before that the module reports whatever it powered up
    // requesting, which is not what it will end up running at
    if (state == USB_STATE_DETACHED)
    {
        ScreenUSBSetValue(USB_ROW_SPEED, "--");
    }
    else
    {
        // USB_IsHighSpeed() rather than a direct USBCSR0 read: that register's
        // interrupt-flag field is clear-on-read, and the accessor behind this
        // call latches anything it swallows for USB_Tasks(). See screen_usb.h.
        ScreenUSBSetValue(USB_ROW_SPEED,
                USB_IsHighSpeed() ? "High Speed (480 Mbps)" : "Full Speed (12 Mbps)");
    }

    ScreenUSBSetValueFmt(USB_ROW_MAX_PACKET, "%u bytes",
            (unsigned int)USB_GetBulkMaxPacket());

    // --- Mass storage -----------------------------------------------------
    // Which side owns the media is the single most useful fact on this
    // screen: it is why a file written from the camera may not be visible to
    // the host, and vice versa.
    ScreenUSBSetValue(USB_ROW_MEDIA_OWNER,
            usb_msd_media_owned_by_host ? "USB host" : "camera");

    // The two LUNs report whether FatFs currently has them mounted locally.
    // While the host owns the media both are handed off, so "in use by host"
    // is the honest answer rather than "not mounted" -- the same distinction
    // screen_sd_card.c draws for its volume rows.
    if (usb_msd_media_owned_by_host)
    {
        ScreenUSBSetValue(USB_ROW_LUN_SD, "in use by host");
        ScreenUSBSetValue(USB_ROW_LUN_FLASH, "in use by host");
    }
    else
    {
        ScreenUSBSetValue(USB_ROW_LUN_SD,
                SDFileIO_IsMounted() ? "mounted locally" : "not mounted");
        ScreenUSBSetValue(USB_ROW_LUN_FLASH,
                FlashFileIO_IsMounted() ? "mounted locally" : "not mounted");
    }

    // --- Bus events -------------------------------------------------------
    // Free-running since reset (usb_counters is only ever cleared by one),
    // so these are a history rather than a state: a climbing disconnect or
    // VBUS error count with a stable state row is a cable or hub problem.
    ScreenUSBSetValueFmt(USB_ROW_BUS_RESETS,    "%lu", (unsigned long)usb_counters.bus_resets);
    ScreenUSBSetValueFmt(USB_ROW_SUSPENDS,      "%lu", (unsigned long)usb_counters.suspends);
    ScreenUSBSetValueFmt(USB_ROW_RESUMES,       "%lu", (unsigned long)usb_counters.resumes);
    ScreenUSBSetValueFmt(USB_ROW_DISCONNECTS,   "%lu", (unsigned long)usb_counters.disconnects);
    ScreenUSBSetValueFmt(USB_ROW_VBUS_ERRORS,   "%lu", (unsigned long)usb_counters.vbus_errors);
    ScreenUSBSetValueFmt(USB_ROW_SETUP_PACKETS, "%lu", (unsigned long)usb_counters.setup_packets);
    ScreenUSBSetValueFmt(USB_ROW_EP0_STALLS,    "%lu", (unsigned long)usb_counters.ep0_stalls);

    // --- Action button ----------------------------------------------------
    // Names what the tap will DO, not what the state is. Screen_SetButtonText()
    // is a no-op when the text is unchanged in effect (lv_label_set_text on
    // the same string still invalidates, so guard it here).
    {
        const char *action = (state == USB_STATE_DETACHED) ? "Attach" : "Detach";
        lv_obj_t *label = lv_obj_get_child(action_button, 0);

        if ((label != NULL) && (strcmp(lv_label_get_text(label), action) != 0))
        {
            Screen_SetButtonText(action_button, action);
        }
    }
}
