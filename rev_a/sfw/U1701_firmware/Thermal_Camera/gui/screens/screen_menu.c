/*******************************************************************************
  Main Menu GUI Screen

  File Name:
    screen_menu.c

  Summary:
    See screen_menu.h -- in particular, adding an entry means adding one row
    to menu_entries[] below and nothing else.
*******************************************************************************/

#include "gui/screens/screen_menu.h"
#include "gui/screens/screen_common.h"

#include "gui/gui.h"
#include "gui/lvgl/lvgl.h"

// Body panel geometry, matching system_screen.c so the two read as the same
// instrument. Rows are tall enough to be a comfortable tap target rather
// than being sized to the 14pt text.
#define SCREEN_MENU_PANEL_INSET_PX   6
#define SCREEN_MENU_PANEL_PAD_PX     6
#define SCREEN_MENU_ROW_HEIGHT_PX    SCREEN_TOUCH_TARGET_MIN_PX
#define SCREEN_MENU_ROW_GAP_PX       4

// One row of the menu. `screen` is what tapping it opens.
//
// This is the table to edit when adding a menu option -- everything below
// is driven off it, including the row count.
typedef struct
{
    const char *label;
    GUI_SCREEN_ID screen;
} SCREEN_MENU_ENTRY;

static const SCREEN_MENU_ENTRY menu_entries[] =
{
    { "System Status", GUI_SCREEN_SYSTEM },
};

#define SCREEN_MENU_ENTRY_COUNT  (sizeof(menu_entries) / sizeof(menu_entries[0]))

static SCREEN_HEADER header;

// Shared by every row: the entry index arrives as the event's user data, so
// one callback covers the whole table however long it grows. Cast through
// uintptr_t rather than pointing at the table, so nothing has to outlive the
// event or be kept in sync with it.
static void ScreenMenuRowClicked(lv_event_t *event)
{
    uintptr_t index = (uintptr_t)lv_event_get_user_data(event);

    if (index >= SCREEN_MENU_ENTRY_COUNT) return;

    GUI_ShowScreen(menu_entries[index].screen, GUI_NAV_FORWARD);
}

static void ScreenMenuBackClicked(lv_event_t *event)
{
    (void)event;

    GUI_ShowScreen(GUI_SCREEN_HOME, GUI_NAV_BACK);
}

lv_obj_t *ScreenMenu_Create(void)
{
    lv_obj_t *screen = Screen_Create();
    lv_obj_t *panel;
    int32_t panel_width;
    uint32_t index;

    if (screen == NULL) return NULL;

    if (!Screen_CreateHeader(screen, "Main Menu", &header)) return NULL;

    if (!Screen_AddBackButton(&header, ScreenMenuBackClicked, NULL)) return NULL;

    // --- Body panel -------------------------------------------------------
    panel = lv_obj_create(screen);
    if (panel == NULL) return NULL;

    // Plain pixel arithmetic, NOT LV_PCT() minus an inset -- LV_PCT()
    // returns an encoded special value, so subtracting from it silently
    // changes the percentage instead of insetting anything.
    panel_width = LV_HOR_RES - (2 * SCREEN_MENU_PANEL_INSET_PX);

    lv_obj_set_size(panel, panel_width,
            LV_VER_RES - (2 * SCREEN_BAR_HEIGHT_PX) - (2 * SCREEN_MENU_PANEL_INSET_PX));
    lv_obj_align(panel, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(panel, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(panel, SCREEN_BAR_OPACITY, LV_PART_MAIN);
    lv_obj_set_style_border_width(panel, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(panel, SCREEN_MENU_PANEL_PAD_PX, LV_PART_MAIN);
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    // --- Rows -------------------------------------------------------------
    // Explicit y-offsets rather than a layout engine, matching
    // system_screen.c: the row count is known at build time and this keeps
    // flex out of the flash budget.
    //
    // Once the table grows past what fits (the panel holds 5 rows at this
    // height), this is where a scrollable panel or a second page goes --
    // right now the rows would simply run off the bottom.
    for (index = 0; index < SCREEN_MENU_ENTRY_COUNT; index++)
    {
        int32_t y = (int32_t)(index * (SCREEN_MENU_ROW_HEIGHT_PX + SCREEN_MENU_ROW_GAP_PX));
        int32_t row_width = panel_width - (2 * SCREEN_MENU_PANEL_PAD_PX);
        lv_obj_t *row;
        lv_obj_t *chevron;

        // Screen_CreateButton centers its own label; the menu wants the name
        // on the left, so the returned chip is re-laid-out below.
        row = Screen_CreateButton(panel, LV_ALIGN_TOP_LEFT, 0, y,
                row_width, SCREEN_MENU_ROW_HEIGHT_PX,
                menu_entries[index].label, ScreenMenuRowClicked,
                (void *)(uintptr_t)index);

        if (row == NULL) return NULL;

        // lv_obj_get_child(row, 0) is the label Screen_CreateButton made
        lv_obj_align(lv_obj_get_child(row, 0), LV_ALIGN_LEFT_MID, 0, 0);

        chevron = Screen_CreateLabel(row, &lv_font_montserrat_14,
                LV_ALIGN_RIGHT_MID, 0, 0, LV_SYMBOL_RIGHT);

        if (chevron == NULL) return NULL;

        // Same reason as the label inside Screen_CreateButton: a clickable
        // child would eat the press before the row sees it.
        lv_obj_remove_flag(chevron, LV_OBJ_FLAG_CLICKABLE);
    }

    ScreenMenu_Refresh();

    return screen;
}

void ScreenMenu_Refresh(void)
{
    // Only the clock changes on this screen; Screen_RefreshHeader() is
    // already safe against a header whose creation failed.
    Screen_RefreshHeader(&header);
}
