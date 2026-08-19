/*******************************************************************************
  SD Card Info GUI Screen

  File Name:
    screen_sd_card.c

  Summary:
    See screen_sd_card.h for the layout and for what each card state shows.
*******************************************************************************/

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "gui/screens/screen_sd_card.h"
#include "gui/screens/screen_common.h"

#include "gui/gui.h"
#include "gui/lvgl/lvgl.h"
#include "sdhc/device_driver/sd_card.h"
#include "sdhc/sd_fileio.h"
#include "usb/device_driver/usb_msd.h"

// Body panel geometry, matching system_screen.c so the two read as the same
// instrument -- the content is taller than the panel, so it scrolls.
#define SD_SCREEN_PANEL_INSET_PX      6
#define SD_SCREEN_PANEL_PAD_PX        6
#define SD_SCREEN_ROW_HEIGHT_PX       18
#define SD_SCREEN_SECTION_HEIGHT_PX   20
#define SD_SCREEN_SECTION_GAP_PX      6
#define SD_SCREEN_USED_BAR_WIDTH      70
#define SD_SCREEN_USED_BAR_HEIGHT     10

// Right-hand lane kept clear for the scrollbar the default theme draws
// inside the panel's right edge -- see system_screen.c, same reasoning.
#define SD_SCREEN_SCROLLBAR_LANE_PX   14

// Longest value string this screen formats, plus room to grow
#define SD_SCREEN_VALUE_MAX_CHARS     32

// Buffer sizes for the FatFs strings, matching what the "SD Card Info?"
// USB UART command passes to SDFileIO_GetVolumeInfo()
#define SD_SCREEN_FS_TYPE_CHARS       8
#define SD_SCREEN_LABEL_CHARS         16

// How often the volume numbers are actually re-read from the card.
// SDFileIO_GetVolumeInfo() calls f_getfree(), which is a blocking SD
// transfer -- running it at the 500ms refresh rate would put an avoidable
// card read in the main loop, behind the FLIR video path, twice a second
// for a number that changes only when something writes a file. Same
// treatment system_screen.c gives the DS1683's blocking I2C reads.
#define SD_SCREEN_VOLUME_READ_INTERVAL_MS  2000u

// *****************************************************************************
// Section: Row list
// *****************************************************************************
// Every row on the screen, top to bottom, in one list -- the same X-macro
// idiom as system_screen.c. SECTION() is a heading with no value; VALUE() is
// a name/value pair whose enum tag names the label the refresh writes into.
//
// Between them these carry everything the "SD Card Info?" USB UART command
// prints: SD_Card_PrintInfo()'s CID/CSD fields in the first group, and
// SDFileIO_GetVolumeInfo()'s FAT numbers in the second.
#define SD_SCREEN_ROW_LIST(SECTION, VALUE)                         \
    SECTION("Card")                                                \
    VALUE(STATUS,        "Status")                                 \
    VALUE(TYPE,          "Type")                                   \
    VALUE(CAPACITY,      "Capacity")                               \
    VALUE(BLOCKS,        "Blocks")                                 \
    VALUE(MANUFACTURER,  "Manufacturer ID")                        \
    VALUE(OEM,           "OEM ID")                                 \
    VALUE(PRODUCT,       "Product Name")                           \
    VALUE(REVISION,      "Product Revision")                       \
    VALUE(SERIAL,        "Serial Number")                          \
    VALUE(MADE,          "Manufacture Date")                       \
    VALUE(BUS_WIDTH,     "Bus Width")                              \
    VALUE(HIGH_SPEED,    "High-Speed Mode")                        \
    SECTION("FAT Volume")                                          \
    VALUE(FS_TYPE,       "Filesystem")                             \
    VALUE(LABEL,         "Volume Label")                           \
    VALUE(TOTAL,         "Total Space")                            \
    VALUE(FREE,          "Free Space")                             \
    VALUE(USED,          "Used")

// The value rows only -- this is what indexes value_labels[] below
#define SD_ROW_ENUM_SECTION(label)
#define SD_ROW_ENUM_VALUE(name, label)   SD_ROW_##name,

typedef enum
{
    SD_SCREEN_ROW_LIST(SD_ROW_ENUM_SECTION, SD_ROW_ENUM_VALUE)
    SD_ROW_COUNT
} SD_SCREEN_ROW;

// Every row in display order, sections included, so the build loop can lay
// them out in one pass. `value_row` is meaningless when is_section is true.
typedef struct
{
    bool is_section;
    const char *label;
    SD_SCREEN_ROW value_row;
} SD_SCREEN_ROW_DEF;

#define SD_ROW_DEF_SECTION(label)        { true,  label, 0 },
#define SD_ROW_DEF_VALUE(name, label)    { false, label, SD_ROW_##name },

static const SD_SCREEN_ROW_DEF sd_rows[] =
{
    SD_SCREEN_ROW_LIST(SD_ROW_DEF_SECTION, SD_ROW_DEF_VALUE)
};

#define SD_SCREEN_ROW_TOTAL  (sizeof(sd_rows) / sizeof(sd_rows[0]))

// The card metadata rows, i.e. everything blanked when no card is
// initialized. STATUS is deliberately not in here -- it always says
// something -- and neither are the volume rows, which have their own
// no-volume text.
static const SD_SCREEN_ROW sd_card_rows[] =
{
    SD_ROW_TYPE, SD_ROW_CAPACITY, SD_ROW_BLOCKS, SD_ROW_MANUFACTURER,
    SD_ROW_OEM, SD_ROW_PRODUCT, SD_ROW_REVISION, SD_ROW_SERIAL,
    SD_ROW_MADE, SD_ROW_BUS_WIDTH, SD_ROW_HIGH_SPEED,
};

#define SD_SCREEN_CARD_ROW_COUNT  (sizeof(sd_card_rows) / sizeof(sd_card_rows[0]))

static const SD_SCREEN_ROW sd_volume_rows[] =
{
    SD_ROW_FS_TYPE, SD_ROW_LABEL, SD_ROW_TOTAL, SD_ROW_FREE, SD_ROW_USED,
};

#define SD_SCREEN_VOLUME_ROW_COUNT  (sizeof(sd_volume_rows) / sizeof(sd_volume_rows[0]))

static SCREEN_HEADER header;
static lv_obj_t *value_labels[SD_ROW_COUNT] = { NULL };
static lv_obj_t *used_bar = NULL;

// Cached result of the throttled SDFileIO_GetVolumeInfo() call above
static bool volume_ever_read = false;
static bool volume_valid = false;
static bool volume_last_mounted = false;
static uint32_t volume_last_read_ms = 0;
static char volume_fs_type[SD_SCREEN_FS_TYPE_CHARS];
static char volume_label[SD_SCREEN_LABEL_CHARS];
static uint32_t volume_total_kb = 0;
static uint32_t volume_free_kb = 0;

// Writes a row's value, but ONLY when the text actually changed -- a blind
// lv_label_set_text() invalidates unconditionally, and re-rendering the
// whole panel twice a second competes with the FLIR video path for DDR2
// bandwidth. Same guard system_screen.c uses, for the same reason.
static void ScreenSDCardSetValue(SD_SCREEN_ROW row, const char *text)
{
    lv_obj_t *label = value_labels[row];

    if (label == NULL) return;
    if (strcmp(lv_label_get_text(label), text) == 0) return;

    lv_label_set_text(label, text);
}

// snprintf() rather than lv_label_set_text_fmt(): LVGL's built-in sprintf
// has no floating point conversions, and XC32's does.
static void ScreenSDCardSetValueFmt(SD_SCREEN_ROW row, const char *format, ...)
{
    char text[SD_SCREEN_VALUE_MAX_CHARS];
    va_list args;

    va_start(args, format);
    vsnprintf(text, sizeof(text), format, args);
    va_end(args);

    ScreenSDCardSetValue(row, text);
}

static const char *ScreenSDCardTypeString(sd_card_type_t type)
{
    switch (type)
    {
        case SD_CARD_TYPE_SDSC: return "SDSC";
        case SD_CARD_TYPE_SDHC: return "SDHC";
        case SD_CARD_TYPE_SDXC: return "SDXC";
        default:                return "Unknown";
    }
}

// KB -> the largest unit that leaves a number a person can read at a
// glance. The UART command prints raw KB, which is the right answer for a
// terminal and the wrong one for a 320px-wide row.
static void ScreenSDCardSetSizeValue(SD_SCREEN_ROW row, uint32_t kilobytes)
{
    if (kilobytes >= (1024u * 1024u))
    {
        ScreenSDCardSetValueFmt(row, "%.2f GB", (double)kilobytes / (1024.0 * 1024.0));
    }
    else if (kilobytes >= 1024u)
    {
        ScreenSDCardSetValueFmt(row, "%.1f MB", (double)kilobytes / 1024.0);
    }
    else
    {
        ScreenSDCardSetValueFmt(row, "%lu KB", (unsigned long)kilobytes);
    }
}

// The one row that always says something, and the one an operator reads
// first: which of the four states (see screen_sd_card.h) the card is in.
// Colored green only when the firmware can actually read files off it.
static void ScreenSDCardRefreshStatus(const sd_card_info_t *info, bool mounted)
{
    const char *text;
    uint32_t color;

    if (mounted)
    {
        text = "Mounted";
        color = 0x30C030;
    }
    else if (usb_msd_media_owned_by_host != 0)
    {
        // The card is still powered and initialized -- FatFs has simply been
        // handed to the USB host (SDFileIO_UnmountKeepPower()), so the card
        // rows below stay populated while the volume rows cannot be read
        text = "In use by USB host";
        color = 0xE0C040;
    }
    else if (info != NULL)
    {
        text = "Present, not mounted";
        color = 0xE0C040;
    }
    else
    {
        text = "No card inserted";
        color = 0xE0C040;
    }

    ScreenSDCardSetValue(SD_ROW_STATUS, text);

    if (value_labels[SD_ROW_STATUS] != NULL)
    {
        lv_obj_set_style_text_color(value_labels[SD_ROW_STATUS],
                lv_color_hex(color), LV_PART_MAIN);
    }
}

// The CID/CSD-derived rows -- exactly the fields SD_Card_PrintInfo() prints.
// All of it comes out of the cached sd_card_info_t, so this touches no
// hardware.
static void ScreenSDCardRefreshCard(const sd_card_info_t *info)
{
    uint32_t index;

    if (info == NULL)
    {
        for (index = 0; index < SD_SCREEN_CARD_ROW_COUNT; index++)
        {
            ScreenSDCardSetValue(sd_card_rows[index], "--");
        }
        return;
    }

    ScreenSDCardSetValue(SD_ROW_TYPE, ScreenSDCardTypeString(info->type));

    ScreenSDCardSetValueFmt(SD_ROW_CAPACITY, "%lu MB",
            (unsigned long)(((uint64_t)info->capacity_blocks * 512u) / (1024u * 1024u)));
    ScreenSDCardSetValueFmt(SD_ROW_BLOCKS, "%lu", (unsigned long)info->capacity_blocks);

    ScreenSDCardSetValueFmt(SD_ROW_MANUFACTURER, "0x%02X", (unsigned int)info->manufacturer_id);
    ScreenSDCardSetValue(SD_ROW_OEM, (info->oem_id[0] != '\0') ? info->oem_id : "(none)");
    ScreenSDCardSetValue(SD_ROW_PRODUCT, (info->product_name[0] != '\0') ? info->product_name : "(none)");
    ScreenSDCardSetValueFmt(SD_ROW_REVISION, "%u.%u",
            (unsigned int)(info->product_revision >> 4),
            (unsigned int)(info->product_revision & 0x0Fu));
    ScreenSDCardSetValueFmt(SD_ROW_SERIAL, "0x%08lX", (unsigned long)info->serial_number);
    ScreenSDCardSetValueFmt(SD_ROW_MADE, "%u-%02u",
            (unsigned int)info->manufacture_year, (unsigned int)info->manufacture_month);
    ScreenSDCardSetValue(SD_ROW_BUS_WIDTH, info->wide_bus_active ? "4-bit" : "1-bit");
    ScreenSDCardSetValue(SD_ROW_HIGH_SPEED,
            info->high_speed_capable ? "active" : "Default Speed");
}

// Blanks the volume rows with `text` and empties the usage bar -- the
// no-volume path, shared by "no card", "not mounted" and "host owns it".
static void ScreenSDCardClearVolume(const char *text)
{
    uint32_t index;

    for (index = 0; index < SD_SCREEN_VOLUME_ROW_COUNT; index++)
    {
        ScreenSDCardSetValue(sd_volume_rows[index], text);
    }

    if (used_bar != NULL) lv_bar_set_value(used_bar, 0, LV_ANIM_OFF);
}

// The FAT rows, from the throttled+cached SDFileIO_GetVolumeInfo() read.
static void ScreenSDCardRefreshVolume(void)
{
    bool mounted = SDFileIO_IsMounted();
    uint32_t now = GUI_GetTickMs();

    // A change in mount state (insertion, eject, USB handoff/release) makes
    // the cache stale immediately -- re-read on the next refresh rather than
    // showing the old card's numbers for up to the throttle interval
    if (mounted != volume_last_mounted)
    {
        volume_last_mounted = mounted;
        volume_ever_read = false;
        volume_valid = false;
    }

    if (!mounted)
    {
        ScreenSDCardClearVolume((usb_msd_media_owned_by_host != 0)
                ? "(USB host)" : "--");
        return;
    }

    if (!volume_ever_read || ((now - volume_last_read_ms) >= SD_SCREEN_VOLUME_READ_INTERVAL_MS))
    {
        volume_ever_read = true;
        volume_last_read_ms = now;

        volume_valid = SDFileIO_GetVolumeInfo(volume_fs_type, sizeof(volume_fs_type),
                volume_label, sizeof(volume_label), &volume_total_kb, &volume_free_kb,
                NULL, NULL);
    }

    if (!volume_valid)
    {
        // Mounted, but f_getfree()/f_getlabel() failed -- a card that went
        // away mid-read, or a volume FatFs can't walk
        ScreenSDCardClearVolume("read failed");
        return;
    }

    ScreenSDCardSetValue(SD_ROW_FS_TYPE, volume_fs_type);
    ScreenSDCardSetValue(SD_ROW_LABEL, (volume_label[0] != '\0') ? volume_label : "(none)");
    ScreenSDCardSetSizeValue(SD_ROW_TOTAL, volume_total_kb);
    ScreenSDCardSetSizeValue(SD_ROW_FREE, volume_free_kb);

    // Used is the one number neither the driver nor FatFs hands over, and
    // the one that answers the question the screen gets opened to answer:
    // how much room is left, as a proportion rather than two figures to
    // subtract in your head
    if (volume_total_kb > 0u)
    {
        uint32_t used_kb = (volume_total_kb > volume_free_kb)
                ? (volume_total_kb - volume_free_kb) : 0u;
        uint32_t used_pct = (uint32_t)(((uint64_t)used_kb * 100u) / volume_total_kb);

        ScreenSDCardSetValueFmt(SD_ROW_USED, "%lu%%", (unsigned long)used_pct);

        if (used_bar != NULL) lv_bar_set_value(used_bar, (int32_t)used_pct, LV_ANIM_OFF);
    }
    else
    {
        ScreenSDCardSetValue(SD_ROW_USED, "--");

        if (used_bar != NULL) lv_bar_set_value(used_bar, 0, LV_ANIM_OFF);
    }
}

static void ScreenSDCardBackClicked(lv_event_t *event)
{
    (void)event;

    GUI_ShowScreen(GUI_SCREEN_MENU, GUI_NAV_BACK);
}

lv_obj_t *ScreenSDCard_Create(void)
{
    lv_obj_t *screen = Screen_Create();
    lv_obj_t *panel;
    uint32_t index;
    int32_t y = 0;
    int32_t used_row_y = 0;

    if (screen == NULL) return NULL;

    if (!Screen_CreateHeader(screen, "SD Card Info", &header)) return NULL;

    // Reached from the main menu, so back returns there rather than to home
    if (!Screen_AddBackButton(&header, ScreenSDCardBackClicked, NULL)) return NULL;

    // --- Body panel -------------------------------------------------------
    panel = lv_obj_create(screen);
    if (panel == NULL) return NULL;

    // Plain pixel sizes, NOT LV_PCT(100) minus an inset: LV_PCT() returns an
    // encoded special value rather than a number of pixels, so subtracting
    // from it silently changes the percentage instead of insetting anything.
    lv_obj_set_size(panel,
            LV_HOR_RES - (2 * SD_SCREEN_PANEL_INSET_PX),
            LV_VER_RES - (2 * SCREEN_BAR_HEIGHT_PX) - (2 * SD_SCREEN_PANEL_INSET_PX));
    lv_obj_align(panel, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(panel, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(panel, SCREEN_BAR_OPACITY, LV_PART_MAIN);
    lv_obj_set_style_border_width(panel, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(panel, SD_SCREEN_PANEL_PAD_PX, LV_PART_MAIN);
    lv_obj_set_style_pad_right(panel, SD_SCREEN_SCROLLBAR_LANE_PX, LV_PART_MAIN);

    // Scrollable, like system_screen.c: the row list is taller than the
    // ~156px the two bars leave. Vertical only, so a slightly-off drag can't
    // skew the right-aligned value column sideways.
    lv_obj_set_scroll_dir(panel, LV_DIR_VER);

    // ON rather than AUTO: AUTO only shows the bar while a scroll is in
    // progress, which leaves no hint that there is anything below the fold.
    lv_obj_set_scrollbar_mode(panel, LV_SCROLLBAR_MODE_ON);

    // --- Rows -------------------------------------------------------------
    // Name on the left, value on the right, with section headings between
    // the groups. Explicit y-offsets rather than a layout engine, matching
    // system_screen.c -- the row set is fixed at build time and this keeps
    // flex out of the flash budget.
    for (index = 0; index < SD_SCREEN_ROW_TOTAL; index++)
    {
        const SD_SCREEN_ROW_DEF *row = &sd_rows[index];

        if (row->is_section)
        {
            lv_obj_t *section;

            // Breathing room above each heading except the first, which sits
            // at the top of the panel already
            if (index != 0) y += SD_SCREEN_SECTION_GAP_PX;

            section = Screen_CreateLabel(panel, &lv_font_montserrat_14,
                    LV_ALIGN_TOP_LEFT, 0, y, row->label);

            if (section == NULL) return NULL;

            // Tinted to separate the groups without needing a second font,
            // same as the system status screen's headings
            lv_obj_set_style_text_color(section, lv_color_hex(0x60C0FF), LV_PART_MAIN);

            y += SD_SCREEN_SECTION_HEIGHT_PX;
        }
        else
        {
            if (Screen_CreateLabel(panel, &lv_font_montserrat_14,
                    LV_ALIGN_TOP_LEFT, 0, y, row->label) == NULL) return NULL;

            value_labels[row->value_row] = Screen_CreateLabel(panel,
                    &lv_font_montserrat_14, LV_ALIGN_TOP_RIGHT, 0, y, "--");

            if (value_labels[row->value_row] == NULL) return NULL;

            if (row->value_row == SD_ROW_USED) used_row_y = y;

            y += SD_SCREEN_ROW_HEIGHT_PX;
        }
    }

    // The used row gets a gauge as well as a percentage, sitting just left
    // of its value label -- the same treatment (and geometry) the system
    // status screen gives the GUI heap. Positioned from the y the loop
    // recorded rather than from a row index, since sections make the two
    // differ.
    used_bar = lv_bar_create(panel);
    if (used_bar == NULL) return NULL;

    lv_obj_set_size(used_bar, SD_SCREEN_USED_BAR_WIDTH, SD_SCREEN_USED_BAR_HEIGHT);
    lv_obj_align(used_bar, LV_ALIGN_TOP_RIGHT, -44, used_row_y + 4);
    lv_bar_set_range(used_bar, 0, 100);
    lv_bar_set_value(used_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(used_bar, lv_color_hex(0x404040), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(used_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(used_bar, lv_color_hex(0x3080E0), LV_PART_INDICATOR);

    ScreenSDCard_Refresh();

    return screen;
}

void ScreenSDCard_Refresh(void)
{
    const sd_card_info_t *info;
    bool mounted;

    // ScreenSDCard_Create() either finishes or leaves these NULL
    if ((value_labels[SD_ROW_STATUS] == NULL) || (used_bar == NULL)) return;

    Screen_RefreshHeader(&header);

    // A cached struct pointer, not a card access -- see screen_sd_card.h on
    // why presence is never polled from here
    info = SD_Card_GetInfo();
    mounted = SDFileIO_IsMounted();

    ScreenSDCardRefreshStatus(info, mounted);
    ScreenSDCardRefreshCard(info);
    ScreenSDCardRefreshVolume();
}
