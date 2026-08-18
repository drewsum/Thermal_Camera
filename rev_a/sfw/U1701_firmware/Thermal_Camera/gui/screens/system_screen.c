/*******************************************************************************
  System Status GUI Screen

  File Name:
    system_screen.c

  Summary:
    See system_screen.h for the layout.
*******************************************************************************/

#include <stdio.h>

#include "gui/screens/system_screen.h"
#include "gui/screens/screen_common.h"

#include "gui/gui.h"
#include "gui/lvgl/lvgl.h"
#include "application/error_handler.h"
#include "application/heartbeat_services.h"
#include "application/main.h"
#include "application/telemetry.h"

// Body panel geometry: fills the gap the two bars leave, inset slightly so
// the panel edge reads as a distinct surface over the Layer 0 image.
#define SYSTEM_SCREEN_PANEL_INSET_PX   6
#define SYSTEM_SCREEN_PANEL_PAD_PX     6
#define SYSTEM_SCREEN_ROW_HEIGHT_PX    18
#define SYSTEM_SCREEN_HEAP_BAR_WIDTH   70
#define SYSTEM_SCREEN_HEAP_BAR_HEIGHT  10

// The rows, top to bottom. Indices into the value_labels array below.
typedef enum
{
    SYSTEM_ROW_FIRMWARE = 0,
    SYSTEM_ROW_UPTIME,
    SYSTEM_ROW_DIE_TEMP,
    SYSTEM_ROW_RAIL_3P0,
    SYSTEM_ROW_RAIL_1P8,
    SYSTEM_ROW_HEAP,
    SYSTEM_ROW_ERRORS,
    SYSTEM_ROW_COUNT
} SYSTEM_SCREEN_ROW;

static const char *const system_row_names[SYSTEM_ROW_COUNT] =
{
    "Firmware",
    "Uptime",
    "MCU Die Temp",
    "+3.0V Rail",
    "+1.8V Rail",
    "GUI Heap",
    "Errors",
};

static SCREEN_HEADER header;
static lv_obj_t *value_labels[SYSTEM_ROW_COUNT] = { NULL };
static lv_obj_t *heap_bar = NULL;

// Counts the error handler flags currently latched. The flag array covers
// both the base flags and the per-I2C-device ones (error_handler.h builds
// both from its X-macro lists), so this is every fault the firmware knows
// how to record.
static uint32_t SystemScreen_CountLatchedErrors(void)
{
    uint32_t count = 0;
    uint32_t index;

    for (index = 0; index < ERROR_HANDLER_NUM_FLAGS; index++)
    {
        if (error_handler.flag_array[index]) count++;
    }

    return count;
}

static void SystemScreenBackClicked(lv_event_t *event)
{
    (void)event;

    GUI_ShowScreen(GUI_SCREEN_MENU, GUI_NAV_BACK);
}

lv_obj_t *SystemScreen_Create(void)
{
    lv_obj_t *screen = Screen_Create();
    lv_obj_t *panel;
    uint32_t row;

    if (screen == NULL) return NULL;

    if (!Screen_CreateHeader(screen, "System Status", &header)) return NULL;

    // Reached from the main menu, so back returns there rather than to home
    if (!Screen_AddBackButton(&header, SystemScreenBackClicked, NULL)) return NULL;

    // --- Body panel -------------------------------------------------------
    panel = lv_obj_create(screen);
    if (panel == NULL) return NULL;

    // Plain pixel sizes, NOT LV_PCT(100) minus an inset: LV_PCT() returns an
    // encoded special value rather than a number of pixels, so subtracting
    // from it silently changes the percentage instead of insetting anything.
    lv_obj_set_size(panel,
            LV_HOR_RES - (2 * SYSTEM_SCREEN_PANEL_INSET_PX),
            LV_VER_RES - (2 * SCREEN_BAR_HEIGHT_PX) - (2 * SYSTEM_SCREEN_PANEL_INSET_PX));
    lv_obj_align(panel, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(panel, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(panel, SCREEN_BAR_OPACITY, LV_PART_MAIN);
    lv_obj_set_style_border_width(panel, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(panel, SYSTEM_SCREEN_PANEL_PAD_PX, LV_PART_MAIN);
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    // Name on the left, value on the right, one row per line. Explicit
    // y-offsets rather than a layout engine: the row count is fixed and
    // this keeps the flex layout out of the flash budget.
    for (row = 0; row < SYSTEM_ROW_COUNT; row++)
    {
        int32_t y = (int32_t)(row * SYSTEM_SCREEN_ROW_HEIGHT_PX);

        if (Screen_CreateLabel(panel, &lv_font_montserrat_14,
                LV_ALIGN_TOP_LEFT, 0, y, system_row_names[row]) == NULL) return NULL;

        value_labels[row] = Screen_CreateLabel(panel, &lv_font_montserrat_14,
                LV_ALIGN_TOP_RIGHT, 0, y, "--");

        if (value_labels[row] == NULL) return NULL;
    }

    // The heap row gets a gauge as well as a percentage, sitting just left
    // of its value label
    heap_bar = lv_bar_create(panel);
    if (heap_bar == NULL) return NULL;

    lv_obj_set_size(heap_bar, SYSTEM_SCREEN_HEAP_BAR_WIDTH, SYSTEM_SCREEN_HEAP_BAR_HEIGHT);
    lv_obj_align(heap_bar, LV_ALIGN_TOP_RIGHT, -44,
            (int32_t)(SYSTEM_ROW_HEAP * SYSTEM_SCREEN_ROW_HEIGHT_PX) + 4);
    lv_bar_set_range(heap_bar, 0, 100);
    lv_bar_set_value(heap_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(heap_bar, lv_color_hex(0x404040), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(heap_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(heap_bar, lv_color_hex(0x3080E0), LV_PART_INDICATOR);

    SystemScreen_Refresh();

    return screen;
}

void SystemScreen_Refresh(void)
{
    char text[32];
    uint32_t seconds;
    uint32_t errors;

    // SystemScreen_Create() either finishes or leaves these NULL
    if ((value_labels[SYSTEM_ROW_ERRORS] == NULL) || (heap_bar == NULL)) return;

    Screen_RefreshHeader(&header);

    snprintf(text, sizeof(text), "%s / Rev %s", FIRMWARE_VERSION_STR, PLATFORM_REVISION_STR);
    lv_label_set_text(value_labels[SYSTEM_ROW_FIRMWARE], text);

    // device_on_time_counter is bumped once a second by heartbeatServices()
    seconds = device_on_time_counter;
    snprintf(text, sizeof(text), "%lud %02lu:%02lu:%02lu",
            (unsigned long)(seconds / 86400u),
            (unsigned long)((seconds / 3600u) % 24u),
            (unsigned long)((seconds / 60u) % 60u),
            (unsigned long)(seconds % 60u));
    lv_label_set_text(value_labels[SYSTEM_ROW_UPTIME], text);

    // snprintf() rather than lv_label_set_text_fmt(): LVGL's built-in
    // sprintf has no floating point conversions, and XC32's does
    snprintf(text, sizeof(text), "%.1f C", telemetry.mcu_die_temp);
    lv_label_set_text(value_labels[SYSTEM_ROW_DIE_TEMP], text);

    snprintf(text, sizeof(text), "%.3f V", telemetry.pos3p0.voltage);
    lv_label_set_text(value_labels[SYSTEM_ROW_RAIL_3P0], text);

    snprintf(text, sizeof(text), "%.3f V", telemetry.pos1p8.voltage);
    lv_label_set_text(value_labels[SYSTEM_ROW_RAIL_1P8], text);

    // Heap: the same DDR2 pool "Peripheral Status? GUI" reports, and the one
    // every PNG decode also draws on (application/image_loader.c) -- so this
    // row is where a decode leak would show up as a creeping percentage
    {
        lv_mem_monitor_t monitor;

        lv_mem_monitor(&monitor);

        lv_bar_set_value(heap_bar, (int32_t)monitor.used_pct, LV_ANIM_OFF);
        snprintf(text, sizeof(text), "%u%%", (unsigned int)monitor.used_pct);
        lv_label_set_text(value_labels[SYSTEM_ROW_HEAP], text);
    }

    errors = SystemScreen_CountLatchedErrors();

    if (errors == 0)
    {
        lv_label_set_text(value_labels[SYSTEM_ROW_ERRORS], "none");
        lv_obj_set_style_text_color(value_labels[SYSTEM_ROW_ERRORS],
                lv_color_hex(0x30C030), LV_PART_MAIN);
    }
    else
    {
        snprintf(text, sizeof(text), "%lu latched", (unsigned long)errors);
        lv_label_set_text(value_labels[SYSTEM_ROW_ERRORS], text);
        lv_obj_set_style_text_color(value_labels[SYSTEM_ROW_ERRORS],
                lv_color_hex(0xE04040), LV_PART_MAIN);
    }
}
