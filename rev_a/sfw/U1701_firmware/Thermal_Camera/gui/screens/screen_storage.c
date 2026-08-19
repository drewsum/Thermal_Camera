/*******************************************************************************
  Storage Usage GUI Screen

  File Name:
    screen_storage.c

  Summary:
    See screen_storage.h.
*******************************************************************************/

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "gui/screens/screen_storage.h"
#include "gui/screens/screen_common.h"

#include "gui/gui.h"
#include "gui/lvgl/lvgl.h"

#include "application/flir/flir_vospi.h"
#include "core/ddr2.h"
#include "core/device_control.h"
#include "glcd/glcd.h"
#include "sdhc/sd_fileio.h"
#include "spi/flash_fileio.h"

// Body panel geometry, matching system_screen.c and screen_sd_card.c so the
// three read as the same instrument.
#define STORAGE_SCREEN_PANEL_INSET_PX      6
#define STORAGE_SCREEN_PANEL_PAD_PX        6
#define STORAGE_SCREEN_ROW_HEIGHT_PX       18
#define STORAGE_SCREEN_SECTION_HEIGHT_PX   20
#define STORAGE_SCREEN_SECTION_GAP_PX      6

// Right-hand lane kept clear for the scrollbar the default theme draws
// inside the panel's right edge -- see system_screen.c, same reasoning.
#define STORAGE_SCREEN_SCROLLBAR_LANE_PX   14

// The usage gauge on a "used" row, sized and placed like the GUI Heap bar on
// the system status screen so the two read the same.
#define STORAGE_SCREEN_BAR_WIDTH           70
#define STORAGE_SCREEN_BAR_HEIGHT          10
#define STORAGE_SCREEN_BAR_RIGHT_INSET     52

// Longest value string this screen formats, plus room to grow
#define STORAGE_SCREEN_VALUE_MAX_CHARS     32

// Same palette as system_screen.c. Amber marks a volume that is nearly full;
// gray marks a figure that is a reservation rather than a measurement, which
// is the distinction this screen exists to keep straight.
#define STORAGE_SCREEN_OK_COLOR            0x30C030
#define STORAGE_SCREEN_WARN_COLOR          0xE0A040
#define STORAGE_SCREEN_UNKNOWN_COLOR       0x808080
#define STORAGE_SCREEN_VALUE_COLOR         0xFFFFFF

// Above this, a volume's used bar goes amber. Matches the threshold
// printStorageUsageLine() uses in the USB UART command (900 permille).
#define STORAGE_SCREEN_NEARLY_FULL_PERMILLE  900u

// How often the two FAT volumes are actually re-read. Both calls go through
// f_getfree(), a blocking media read; running them at the 500ms refresh rate
// would put two of those in the main loop, behind the FLIR video path, twice
// a second for numbers that change only when a file is written. Same
// treatment (and same interval) screen_sd_card.c gives the identical call.
#define STORAGE_SCREEN_VOLUME_READ_INTERVAL_MS  2000u

// Linker-provided symbols, declared exactly as the "Storage Usage?" command
// declares them -- see the long note there. _end marks the first free byte
// after all linked .data/.bss; _min_heap_size is this project's own
// --defsym build flag echoed back. Neither is a variable: the SYMBOL'S
// ADDRESS is the value, and the array-of-unknown-size form is what keeps
// XC32 from trying to reach them through GP-relative addressing (which
// fails to link once the value outgrows that offset window).
extern uint32_t _end[];
extern uint32_t _min_heap_size[];

// *****************************************************************************
// Section: Row list
// *****************************************************************************
// Every row on the screen, top to bottom, in one list -- the same X-macro
// idiom as system_screen.c. SECTION() is a heading with no value; VALUE() is
// a name/value pair whose enum tag names the label the refresh writes into.
#define STORAGE_SCREEN_ROW_LIST(SECTION, VALUE)                    \
    SECTION("microSD Card (0:)")                                   \
    VALUE(SD_TOTAL,       "Total")                                 \
    VALUE(SD_USED,        "Used")                                  \
    VALUE(SD_FREE,        "Free")                                  \
    SECTION("SPI Flash (1:)")                                      \
    VALUE(FLASH_TOTAL,    "Total")                                 \
    VALUE(FLASH_USED,     "Used")                                  \
    VALUE(FLASH_FREE,     "Free")                                  \
    SECTION("Internal SRAM")                                       \
    VALUE(SRAM_TOTAL,     "Total")                                 \
    VALUE(SRAM_STATIC,    "Static (.data+.bss)")                   \
    VALUE(SRAM_HEAP,      "Heap (reserved)")                       \
    VALUE(SRAM_STACK,     "Stack (reserved)")                      \
    SECTION("Internal Program Flash")                              \
    VALUE(PFLASH_TOTAL,   "Total")                                 \
    VALUE(PFLASH_USED,    "Used")                                  \
    SECTION("DDR2 SDRAM (reservations)")                           \
    VALUE(DDR2_TOTAL,     "Total")                                 \
    VALUE(DDR2_FRAMEBUF,  "GLCD Frame Buffer")                     \
    VALUE(DDR2_OVERLAY_A, "GUI Overlay Buffer A")                  \
    VALUE(DDR2_OVERLAY_B, "GUI Overlay Buffer B")                  \
    VALUE(DDR2_LVGL,      "LVGL Heap")                             \
    VALUE(DDR2_LAYER2,    "GLCD Layer 2 Image")                    \
    VALUE(DDR2_VOSPI,     "FLIR VoSPI Frames x2")                  \
    VALUE(DDR2_FREE,      "Unreserved")

// The value rows only -- this is what indexes value_labels[] below
#define STORAGE_ROW_ENUM_SECTION(label)
#define STORAGE_ROW_ENUM_VALUE(name, label)   STORAGE_ROW_##name,

typedef enum
{
    STORAGE_SCREEN_ROW_LIST(STORAGE_ROW_ENUM_SECTION, STORAGE_ROW_ENUM_VALUE)
    STORAGE_ROW_COUNT
} STORAGE_SCREEN_ROW;

// Every row in display order, sections included, so the build loop can lay
// them out in one pass. `value_row` is meaningless when is_section is true.
typedef struct
{
    bool is_section;
    const char *label;
    STORAGE_SCREEN_ROW value_row;
} STORAGE_SCREEN_ROW_DEF;

#define STORAGE_ROW_DEF_SECTION(label)        { true,  label, 0 },
#define STORAGE_ROW_DEF_VALUE(name, label)    { false, label, STORAGE_ROW_##name },

static const STORAGE_SCREEN_ROW_DEF storage_rows[] =
{
    STORAGE_SCREEN_ROW_LIST(STORAGE_ROW_DEF_SECTION, STORAGE_ROW_DEF_VALUE)
};

#define STORAGE_SCREEN_ROW_TOTAL  (sizeof(storage_rows) / sizeof(storage_rows[0]))

// The rows that get a usage gauge beside their percentage. Everything else
// is a plain byte count.
static const STORAGE_SCREEN_ROW storage_bar_rows[] =
{
    STORAGE_ROW_SD_USED, STORAGE_ROW_FLASH_USED,
    STORAGE_ROW_SRAM_STATIC, STORAGE_ROW_SRAM_HEAP, STORAGE_ROW_SRAM_STACK,
    STORAGE_ROW_DDR2_FRAMEBUF, STORAGE_ROW_DDR2_OVERLAY_A,
    STORAGE_ROW_DDR2_OVERLAY_B, STORAGE_ROW_DDR2_LVGL,
    STORAGE_ROW_DDR2_LAYER2, STORAGE_ROW_DDR2_VOSPI, STORAGE_ROW_DDR2_FREE,
};

#define STORAGE_SCREEN_BAR_ROW_COUNT \
        (sizeof(storage_bar_rows) / sizeof(storage_bar_rows[0]))

static SCREEN_HEADER header;
static lv_obj_t *value_labels[STORAGE_ROW_COUNT] = { NULL };

// Indexed by STORAGE_SCREEN_ROW like value_labels[], but only the rows in
// storage_bar_rows[] above ever get one -- the rest stay NULL and the
// helpers below no-op on them.
static lv_obj_t *bar_widgets[STORAGE_ROW_COUNT] = { NULL };

// Cached volume figures, refreshed on the throttle rather than per refresh.
static bool volume_ever_read = false;
static uint32_t volume_last_read_ms = 0;
static bool sd_mounted = false;
static bool flash_mounted = false;
static uint32_t sd_total_bytes = 0;
static uint32_t sd_free_bytes = 0;
static uint32_t flash_total_bytes = 0;
static uint32_t flash_free_bytes = 0;

// Writes a row's value only when the text actually changed --
// lv_label_set_text() invalidates unconditionally, and this screen has
// twenty live rows. Same guard, same reasoning, as system_screen.c's.
static void ScreenStorageSetValue(STORAGE_SCREEN_ROW row, const char *text)
{
    lv_obj_t *label = value_labels[row];

    if (label == NULL) return;
    if (strcmp(lv_label_get_text(label), text) == 0) return;

    lv_label_set_text(label, text);
}

static void ScreenStorageSetValueFmt(STORAGE_SCREEN_ROW row, const char *format, ...)
{
    char text[STORAGE_SCREEN_VALUE_MAX_CHARS];
    va_list args;

    va_start(args, format);
    vsnprintf(text, sizeof(text), format, args);
    va_end(args);

    ScreenStorageSetValue(row, text);
}

static void ScreenStorageSetColor(STORAGE_SCREEN_ROW row, uint32_t color)
{
    if (value_labels[row] == NULL) return;

    lv_obj_set_style_text_color(value_labels[row], lv_color_hex(color), LV_PART_MAIN);
}

// Byte counts on this board span five orders of magnitude -- 38KB of VoSPI
// frames next to a 32GB card -- so a single unit would either lose the small
// ones to rounding or print the big ones as ten digits nothing can read at a
// glance. Scaled to the largest unit that leaves a number below 1000.
static void ScreenStorageFormatBytes(char *text, size_t size, uint32_t bytes)
{
    if (bytes >= (1024u * 1024u * 1024u))
    {
        snprintf(text, size, "%.2f GB", (double)bytes / (1024.0 * 1024.0 * 1024.0));
    }
    else if (bytes >= (1024u * 1024u))
    {
        snprintf(text, size, "%.1f MB", (double)bytes / (1024.0 * 1024.0));
    }
    else if (bytes >= 1024u)
    {
        snprintf(text, size, "%.1f KB", (double)bytes / 1024.0);
    }
    else
    {
        snprintf(text, size, "%lu B", (unsigned long)bytes);
    }
}

// Tenths of a percent, computed through uint64_t. A plain "part * 1000"
// overflows 32 bits at 4.2MB, which every one of the volumes on this board
// exceeds -- the USB UART command's helper widens for exactly this reason.
static uint32_t ScreenStoragePermille(uint32_t part, uint32_t total)
{
    if (total == 0) return 0;

    return (uint32_t)(((uint64_t)part * 1000u) / total);
}

// A row that is a fraction of something: "12.3%" in the value column with
// the gauge beside it. `color` is the text color, which is what separates a
// live measurement from a fixed reservation.
static void ScreenStorageSetFraction(STORAGE_SCREEN_ROW row, uint32_t part,
        uint32_t total, uint32_t color)
{
    uint32_t permille = ScreenStoragePermille(part, total);

    ScreenStorageSetValueFmt(row, "%lu.%lu%%",
            (unsigned long)(permille / 10u), (unsigned long)(permille % 10u));
    ScreenStorageSetColor(row, color);

    if (bar_widgets[row] != NULL)
    {
        lv_bar_set_value(bar_widgets[row], (int32_t)(permille / 10u), LV_ANIM_OFF);
        lv_obj_remove_flag(bar_widgets[row], LV_OBJ_FLAG_HIDDEN);
    }
}

// Blanks a row and hides its gauge, for a volume that is not mounted.
static void ScreenStorageSetUnavailable(STORAGE_SCREEN_ROW row, const char *text)
{
    ScreenStorageSetValue(row, text);
    ScreenStorageSetColor(row, STORAGE_SCREEN_UNKNOWN_COLOR);

    if (bar_widgets[row] != NULL) lv_obj_add_flag(bar_widgets[row], LV_OBJ_FLAG_HIDDEN);
}

// The two blocking reads, behind the throttle. Cached into the statics above
// so the row-writing below stays pure arithmetic.
static void ScreenStorageRefreshVolumes(void)
{
    uint32_t now = GUI_GetTickMs();

    if (volume_ever_read &&
        ((now - volume_last_read_ms) < STORAGE_SCREEN_VOLUME_READ_INTERVAL_MS))
    {
        return;
    }

    volume_ever_read = true;
    volume_last_read_ms = now;

    sd_mounted = SDFileIO_GetVolumeInfo(NULL, 0, NULL, 0, NULL, NULL,
            &sd_total_bytes, &sd_free_bytes);

    flash_mounted = FlashFileIO_GetVolumeInfo(NULL, 0, NULL, 0, NULL, NULL,
            &flash_total_bytes, &flash_free_bytes);
}

// One FAT volume's three rows.
static void ScreenStorageWriteVolume(bool mounted, uint32_t total, uint32_t free_bytes,
        STORAGE_SCREEN_ROW total_row, STORAGE_SCREEN_ROW used_row,
        STORAGE_SCREEN_ROW free_row)
{
    char text[STORAGE_SCREEN_VALUE_MAX_CHARS];
    uint32_t used;
    uint32_t permille;

    if (!mounted)
    {
        ScreenStorageSetUnavailable(total_row, "not mounted");
        ScreenStorageSetUnavailable(used_row, "--");
        ScreenStorageSetUnavailable(free_row, "--");
        return;
    }

    used = total - free_bytes;
    permille = ScreenStoragePermille(used, total);

    ScreenStorageFormatBytes(text, sizeof(text), total);
    ScreenStorageSetValue(total_row, text);
    ScreenStorageSetColor(total_row, STORAGE_SCREEN_VALUE_COLOR);

    // The only rows on this screen where a high percentage is actually a
    // warning -- everything below is a fixed carve-up, where "100%" just
    // means the memory is fully allocated by design
    ScreenStorageSetFraction(used_row, used, total,
            (permille >= STORAGE_SCREEN_NEARLY_FULL_PERMILLE)
                    ? STORAGE_SCREEN_WARN_COLOR : STORAGE_SCREEN_OK_COLOR);

    ScreenStorageFormatBytes(text, sizeof(text), free_bytes);
    ScreenStorageSetValue(free_row, text);
    ScreenStorageSetColor(free_row, STORAGE_SCREEN_VALUE_COLOR);
}

static void ScreenStorageBackClicked(lv_event_t *event)
{
    (void)event;

    GUI_ShowScreen(GUI_SCREEN_MENU, GUI_NAV_BACK);
}

lv_obj_t *ScreenStorage_Create(void)
{
    lv_obj_t *screen = Screen_Create();
    lv_obj_t *panel;
    uint32_t index;
    int32_t y = 0;
    int32_t bar_row_y[STORAGE_ROW_COUNT];

    if (screen == NULL) return NULL;

    if (!Screen_CreateHeader(screen, "Storage Usage", &header)) return NULL;

    if (!Screen_AddBackButton(&header, ScreenStorageBackClicked, NULL)) return NULL;

    // --- Body panel -------------------------------------------------------
    panel = lv_obj_create(screen);
    if (panel == NULL) return NULL;

    // Plain pixel arithmetic, NOT LV_PCT() minus an inset -- LV_PCT()
    // returns an encoded special value, so subtracting from it silently
    // changes the percentage instead of insetting anything.
    lv_obj_set_size(panel,
            LV_HOR_RES - (2 * STORAGE_SCREEN_PANEL_INSET_PX),
            LV_VER_RES - (2 * SCREEN_BAR_HEIGHT_PX) - (2 * STORAGE_SCREEN_PANEL_INSET_PX));
    lv_obj_align(panel, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(panel, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(panel, SCREEN_BAR_OPACITY, LV_PART_MAIN);
    lv_obj_set_style_border_width(panel, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(panel, STORAGE_SCREEN_PANEL_PAD_PX, LV_PART_MAIN);
    lv_obj_set_style_pad_right(panel, STORAGE_SCREEN_SCROLLBAR_LANE_PX, LV_PART_MAIN);

    // Vertical only: the value labels are right-aligned, and a horizontal
    // scroll would let a slightly-off vertical drag skew the list sideways.
    lv_obj_set_scroll_dir(panel, LV_DIR_VER);

    // ON rather than AUTO: AUTO only shows the bar during a scroll, which
    // leaves no hint that there is more below the fold.
    lv_obj_set_scrollbar_mode(panel, LV_SCROLLBAR_MODE_ON);

    // --- Rows -------------------------------------------------------------
    for (index = 0; index < STORAGE_ROW_COUNT; index++) bar_row_y[index] = 0;

    for (index = 0; index < STORAGE_SCREEN_ROW_TOTAL; index++)
    {
        const STORAGE_SCREEN_ROW_DEF *row = &storage_rows[index];

        if (row->is_section)
        {
            lv_obj_t *section;

            if (index != 0) y += STORAGE_SCREEN_SECTION_GAP_PX;

            section = Screen_CreateLabel(panel, &lv_font_montserrat_14,
                    LV_ALIGN_TOP_LEFT, 0, y, row->label);

            if (section == NULL) return NULL;

            // The same tint system_screen.c gives its headings
            lv_obj_set_style_text_color(section, lv_color_hex(0x60C0FF), LV_PART_MAIN);

            y += STORAGE_SCREEN_SECTION_HEIGHT_PX;
        }
        else
        {
            if (Screen_CreateLabel(panel, &lv_font_montserrat_14,
                    LV_ALIGN_TOP_LEFT, 0, y, row->label) == NULL) return NULL;

            value_labels[row->value_row] = Screen_CreateLabel(panel,
                    &lv_font_montserrat_14, LV_ALIGN_TOP_RIGHT, 0, y, "--");

            if (value_labels[row->value_row] == NULL) return NULL;

            // Recorded rather than recomputed: sections make the row index
            // and the y-offset diverge
            bar_row_y[row->value_row] = y;

            y += STORAGE_SCREEN_ROW_HEIGHT_PX;
        }
    }

    // --- Usage gauges -----------------------------------------------------
    // Built in a second pass, from the y-offsets the row loop recorded, so
    // the bar list stays a plain list of row ids rather than something the
    // layout loop has to know about.
    for (index = 0; index < STORAGE_SCREEN_BAR_ROW_COUNT; index++)
    {
        STORAGE_SCREEN_ROW row = storage_bar_rows[index];
        lv_obj_t *bar = lv_bar_create(panel);

        if (bar == NULL) return NULL;

        lv_obj_set_size(bar, STORAGE_SCREEN_BAR_WIDTH, STORAGE_SCREEN_BAR_HEIGHT);
        lv_obj_align(bar, LV_ALIGN_TOP_RIGHT, -STORAGE_SCREEN_BAR_RIGHT_INSET,
                bar_row_y[row] + 4);
        lv_bar_set_range(bar, 0, 100);
        lv_bar_set_value(bar, 0, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(bar, lv_color_hex(0x404040), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_bg_color(bar, lv_color_hex(0x3080E0), LV_PART_INDICATOR);

        bar_widgets[row] = bar;
    }

    ScreenStorage_Refresh();

    return screen;
}

void ScreenStorage_Refresh(void)
{
    char text[STORAGE_SCREEN_VALUE_MAX_CHARS];
    uint32_t sram_static;
    uint32_t sram_heap;
    uint32_t sram_stack;
    uint32_t ddr2_reserved;

    // ScreenStorage_Create() either finishes or leaves this NULL
    if (value_labels[STORAGE_ROW_SD_TOTAL] == NULL) return;

    Screen_RefreshHeader(&header);

    // --- FAT volumes (throttled, blocking media reads) --------------------
    ScreenStorageRefreshVolumes();

    ScreenStorageWriteVolume(sd_mounted, sd_total_bytes, sd_free_bytes,
            STORAGE_ROW_SD_TOTAL, STORAGE_ROW_SD_USED, STORAGE_ROW_SD_FREE);

    ScreenStorageWriteVolume(flash_mounted, flash_total_bytes, flash_free_bytes,
            STORAGE_ROW_FLASH_TOTAL, STORAGE_ROW_FLASH_USED, STORAGE_ROW_FLASH_FREE);

    // --- Internal SRAM ----------------------------------------------------
    // Only the static figure is measured. Heap and stack are what the
    // linker's best-fit allocator set aside at link time -- see the note in
    // screen_storage.h -- so they are drawn in the reservation gray, not the
    // green a live usage figure gets.
    sram_static = (uint32_t)_end - MCU_SRAM_BASE_ADDRESS;
    sram_heap = (uint32_t)_min_heap_size;
    sram_stack = MCU_SRAM_TOTAL_BYTES - sram_static - sram_heap;

    ScreenStorageFormatBytes(text, sizeof(text), MCU_SRAM_TOTAL_BYTES);
    ScreenStorageSetValue(STORAGE_ROW_SRAM_TOTAL, text);
    ScreenStorageSetColor(STORAGE_ROW_SRAM_TOTAL, STORAGE_SCREEN_VALUE_COLOR);

    ScreenStorageSetFraction(STORAGE_ROW_SRAM_STATIC, sram_static,
            MCU_SRAM_TOTAL_BYTES, STORAGE_SCREEN_VALUE_COLOR);
    ScreenStorageSetFraction(STORAGE_ROW_SRAM_HEAP, sram_heap,
            MCU_SRAM_TOTAL_BYTES, STORAGE_SCREEN_UNKNOWN_COLOR);
    ScreenStorageSetFraction(STORAGE_ROW_SRAM_STACK, sram_stack,
            MCU_SRAM_TOTAL_BYTES, STORAGE_SCREEN_UNKNOWN_COLOR);

    // --- Internal program flash -------------------------------------------
    ScreenStorageFormatBytes(text, sizeof(text), MCU_FLASH_TOTAL_BYTES);
    ScreenStorageSetValue(STORAGE_ROW_PFLASH_TOTAL, text);
    ScreenStorageSetColor(STORAGE_ROW_PFLASH_TOTAL, STORAGE_SCREEN_VALUE_COLOR);

    // No linker symbol on this device marks the end of used flash the way
    // _end does for RAM, so there is nothing honest to put here
    ScreenStorageSetUnavailable(STORAGE_ROW_PFLASH_USED, "not available");

    // --- DDR2 SDRAM -------------------------------------------------------
    // Fixed reservations, not an allocator's view: core/ddr2.h has no
    // allocator and every one of these is by convention. The full map, and
    // the reasoning behind each entry, is in gui/gui.h.
    ddr2_reserved = GLCD_FRAMEBUFFER_SIZE_BYTES
            + (2u * GLCD_OVERLAY_SIZE_BYTES)
            + GUI_LVGL_HEAP_SIZE_BYTES
            + GLCD_LAYER2_SIZE_BYTES
            + (2u * FLIR_VOSPI_FRAME_SIZE_BYTES);

    ScreenStorageFormatBytes(text, sizeof(text), DDR2_SIZE_BYTES);
    ScreenStorageSetValue(STORAGE_ROW_DDR2_TOTAL, text);
    ScreenStorageSetColor(STORAGE_ROW_DDR2_TOTAL, STORAGE_SCREEN_VALUE_COLOR);

    ScreenStorageSetFraction(STORAGE_ROW_DDR2_FRAMEBUF, GLCD_FRAMEBUFFER_SIZE_BYTES,
            DDR2_SIZE_BYTES, STORAGE_SCREEN_UNKNOWN_COLOR);
    ScreenStorageSetFraction(STORAGE_ROW_DDR2_OVERLAY_A, GLCD_OVERLAY_SIZE_BYTES,
            DDR2_SIZE_BYTES, STORAGE_SCREEN_UNKNOWN_COLOR);
    ScreenStorageSetFraction(STORAGE_ROW_DDR2_OVERLAY_B, GLCD_OVERLAY_SIZE_BYTES,
            DDR2_SIZE_BYTES, STORAGE_SCREEN_UNKNOWN_COLOR);
    ScreenStorageSetFraction(STORAGE_ROW_DDR2_LVGL, GUI_LVGL_HEAP_SIZE_BYTES,
            DDR2_SIZE_BYTES, STORAGE_SCREEN_UNKNOWN_COLOR);
    ScreenStorageSetFraction(STORAGE_ROW_DDR2_LAYER2, GLCD_LAYER2_SIZE_BYTES,
            DDR2_SIZE_BYTES, STORAGE_SCREEN_UNKNOWN_COLOR);
    ScreenStorageSetFraction(STORAGE_ROW_DDR2_VOSPI, 2u * FLIR_VOSPI_FRAME_SIZE_BYTES,
            DDR2_SIZE_BYTES, STORAGE_SCREEN_UNKNOWN_COLOR);

    // Green, because unlike every other DDR2 row this one IS headroom
    ScreenStorageSetFraction(STORAGE_ROW_DDR2_FREE, DDR2_SIZE_BYTES - ddr2_reserved,
            DDR2_SIZE_BYTES, STORAGE_SCREEN_OK_COLOR);
}
