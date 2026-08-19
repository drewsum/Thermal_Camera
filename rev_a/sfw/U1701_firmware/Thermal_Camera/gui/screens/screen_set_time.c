/*******************************************************************************
  Set Date and Time GUI Screen

  File Name:
    screen_set_time.c

  Summary:
    See screen_set_time.h.
*******************************************************************************/

#include <stdio.h>

#include "gui/screens/screen_set_time.h"
#include "gui/screens/screen_common.h"

#include "gui/gui.h"
#include "gui/lvgl/lvgl.h"

#include "core/rtcc.h"

// Body panel geometry, matching the menu and brightness screens so the three
// read as the same instrument.
#define SCREEN_SET_TIME_PANEL_INSET_PX   6
#define SCREEN_SET_TIME_PANEL_PAD_PX     6

// The panel's content box, which every offset below is measured inside:
// 320 - 2*6 (inset) - 2*6 (padding) = 296 wide, and
// 240 - 2*36 (bars) - 2*6 (inset) - 2*6 (padding) = 144 tall.
#define SCREEN_SET_TIME_CONTENT_W_PX \
        (LV_HOR_RES - (2 * SCREEN_SET_TIME_PANEL_INSET_PX) \
                - (2 * SCREEN_SET_TIME_PANEL_PAD_PX))

// Gap between adjacent stepper columns. The columns themselves are not all
// the same width -- the year needs four 20pt digits where the rest need two
// -- so the widths live in the field table below and this is what separates
// them.
#define SCREEN_SET_TIME_COLUMN_GAP_PX    5

// Rows within the content box, as offsets from its top edge:
//
//   0    caption      14pt, ~17px tall
//   18   "+" button   28px, the touch target floor
//   50   value        20pt, ~25px tall
//   78   "-" button   28px
//   112  commit row   28px  -> ends at 140, inside the 144 available
//
// Explicit offsets rather than a layout engine, matching every other screen
// here: the row count is fixed at build time and this keeps flex out of the
// flash budget.
#define SCREEN_SET_TIME_CAPTION_Y_PX     0
#define SCREEN_SET_TIME_UP_Y_PX          18
#define SCREEN_SET_TIME_VALUE_Y_PX       50
#define SCREEN_SET_TIME_DOWN_Y_PX        78
#define SCREEN_SET_TIME_COMMIT_Y_PX      112

#define SCREEN_SET_TIME_STEP_HEIGHT_PX   SCREEN_TOUCH_TARGET_MIN_PX
#define SCREEN_SET_TIME_COMMIT_W_PX      120
#define SCREEN_SET_TIME_COMMIT_H_PX      SCREEN_TOUCH_TARGET_MIN_PX

// Vertical nudge that centers a 14pt line against the 28px commit button
// beside it: (28 - 17) / 2, rounded down.
#define SCREEN_SET_TIME_WEEKDAY_Y_PX \
        (SCREEN_SET_TIME_COMMIT_Y_PX + ((SCREEN_SET_TIME_COMMIT_H_PX - 17) / 2))

// The secondary-text gray the home and brightness screens already use, for
// the column captions and the derived weekday
#define SCREEN_SET_TIME_DIM_TEXT_COLOR   0x808080

// The six editable fields, in the order they sit across the panel. This is
// the table that drives everything below -- the columns, their captions and
// widths, and the wrap limits the steppers obey.
typedef enum
{
    SCREEN_SET_TIME_MONTH = 0,
    SCREEN_SET_TIME_DAY,
    SCREEN_SET_TIME_YEAR,
    SCREEN_SET_TIME_HOUR,
    SCREEN_SET_TIME_MINUTE,
    SCREEN_SET_TIME_SECOND,
    SCREEN_SET_TIME_FIELD_COUNT
} SCREEN_SET_TIME_FIELD;

typedef struct
{
    const char *caption;
    int32_t     width;      // column width, px
    uint16_t    min;
    uint16_t    max;        // 0 for the day: see ScreenSetTimeFieldMax()
    bool        four_digit; // formats as %04u rather than %02u
} SCREEN_SET_TIME_FIELD_DESC;

// Widths sum to 42+42+60+42+42+42 = 270, plus five 5px gaps = 295, which
// fits the 296px content box with a pixel to spare. The year gets the wide
// column because "2026" in 20pt is about 46px against the 23px a two-digit
// field needs.
//
// Captions are abbreviated hard: at 14pt "Month" is wider than the 42px
// column it would sit over.
static const SCREEN_SET_TIME_FIELD_DESC set_time_fields[SCREEN_SET_TIME_FIELD_COUNT] =
{
    [SCREEN_SET_TIME_MONTH]  = { "MON",  42, 1, 12, false },
    [SCREEN_SET_TIME_DAY]    = { "DAY",  42, 1,  0, false },
    [SCREEN_SET_TIME_YEAR]   = { "YEAR", 60, SCREEN_SET_TIME_YEAR_MIN,
                                            SCREEN_SET_TIME_YEAR_MAX, true },
    [SCREEN_SET_TIME_HOUR]   = { "HR",   42, 0, 23, false },
    [SCREEN_SET_TIME_MINUTE] = { "MIN",  42, 0, 59, false },
    [SCREEN_SET_TIME_SECOND] = { "SEC",  42, 0, 59, false },
};

static SCREEN_HEADER header;

// Left clear until Create() finishes, which is what Refresh() checks -- the
// same "did the screen get built" guard the other screens use.
static lv_obj_t *value_label[SCREEN_SET_TIME_FIELD_COUNT];
static lv_obj_t *weekday_label = NULL;
static bool screen_built = false;

// What the user is building. Not the RTCC: nothing here reaches the hardware
// until the commit button is tapped, for the DMA-suspension reason given in
// screen_set_time.h.
static uint16_t staged[SCREEN_SET_TIME_FIELD_COUNT];

// Set by the first stepper tap and cleared by the commit button or by
// leaving the screen. While it is set, Refresh() leaves the fields alone --
// otherwise the 500ms tick would overwrite the edit with the live clock
// twice a second.
static bool edit_pending = false;

static bool ScreenSetTimeIsLeapYear(uint16_t year)
{
    return (((year % 4u) == 0u) && ((year % 100u) != 0u)) || ((year % 400u) == 0u);
}

static uint16_t ScreenSetTimeDaysInMonth(uint16_t month, uint16_t year)
{
    static const uint8_t days[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

    if ((month < 1u) || (month > 12u)) return 31u;

    if ((month == 2u) && ScreenSetTimeIsLeapYear(year)) return 29u;

    return days[month - 1u];
}

// The day's upper limit is the only one that moves, so the table stores 0
// for it and it is resolved here against whatever month and year are staged.
static uint16_t ScreenSetTimeFieldMax(SCREEN_SET_TIME_FIELD field)
{
    if (field == SCREEN_SET_TIME_DAY)
    {
        return ScreenSetTimeDaysInMonth(staged[SCREEN_SET_TIME_MONTH],
                staged[SCREEN_SET_TIME_YEAR]);
    }

    return set_time_fields[field].max;
}

// Sakamoto's algorithm, returning 0 for Sunday to match weekday_t
// (core/rtcc.h). The weekday is derived rather than edited -- see the note
// in screen_set_time.h.
static weekday_t ScreenSetTimeWeekday(uint16_t year, uint16_t month, uint16_t day)
{
    static const uint8_t month_offset[12] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
    uint16_t y = year;

    if ((month < 1u) || (month > 12u)) return Sunday;

    // January and February are counted as months 13 and 14 of the previous
    // year, which is what puts the leap day at the end of the year being
    // divided
    if (month < 3u) y -= 1u;

    return (weekday_t)(((uint32_t)y + (y / 4u) - (y / 100u) + (y / 400u)
            + month_offset[month - 1u] + day) % 7u);
}

// Pulls every field back inside its limits. Runs after a load from the RTCC
// (a clock that has never been set, or one whose backup supply died, reads
// back values that are not a real date) and after any month or year change,
// which can strand the day past the end of the new month -- Jan 31 stepped
// into February.
static void ScreenSetTimeClamp(void)
{
    uint32_t index;

    for (index = 0; index < SCREEN_SET_TIME_FIELD_COUNT; index++)
    {
        uint16_t min = set_time_fields[index].min;
        uint16_t max = ScreenSetTimeFieldMax((SCREEN_SET_TIME_FIELD)index);

        if (staged[index] < min) staged[index] = min;
        if (staged[index] > max) staged[index] = max;
    }
}

// rtcc_shadow is maintained by the RTCC interrupt (core/rtcc.c) in plain
// binary, so this is a struct read rather than any device access.
static void ScreenSetTimeLoadFromRtcc(void)
{
    staged[SCREEN_SET_TIME_MONTH]  = rtcc_shadow.month;
    staged[SCREEN_SET_TIME_DAY]    = rtcc_shadow.day;
    staged[SCREEN_SET_TIME_YEAR]   = rtcc_shadow.year;
    staged[SCREEN_SET_TIME_HOUR]   = rtcc_shadow.hours;
    staged[SCREEN_SET_TIME_MINUTE] = rtcc_shadow.minutes;
    staged[SCREEN_SET_TIME_SECOND] = rtcc_shadow.seconds;

    ScreenSetTimeClamp();
}

// Writes the staged values into the six readouts and the derived weekday
// under them.
static void ScreenSetTimeRender(void)
{
    uint32_t index;

    if (!screen_built) return;

    for (index = 0; index < SCREEN_SET_TIME_FIELD_COUNT; index++)
    {
        char text[8];

        snprintf(text, sizeof(text),
                set_time_fields[index].four_digit ? "%04u" : "%02u",
                (unsigned int)staged[index]);

        lv_label_set_text(value_label[index], text);
    }

    lv_label_set_text(weekday_label,
            getDayOfWeek((uint8_t)ScreenSetTimeWeekday(staged[SCREEN_SET_TIME_YEAR],
                    staged[SCREEN_SET_TIME_MONTH], staged[SCREEN_SET_TIME_DAY])));
}

// Shared by all twelve stepper buttons. The field index and the direction
// arrive packed into the event's user data, so one callback covers the whole
// table -- cast through uintptr_t rather than pointing at anything, the same
// way screen_menu.c dispatches its rows.
#define SCREEN_SET_TIME_PACK(field, up)  ((uintptr_t)(((field) << 1) | ((up) ? 1u : 0u)))

static void ScreenSetTimeStepClicked(lv_event_t *event)
{
    uintptr_t packed = (uintptr_t)lv_event_get_user_data(event);
    SCREEN_SET_TIME_FIELD field = (SCREEN_SET_TIME_FIELD)(packed >> 1);
    bool up = ((packed & 1u) != 0u);
    uint16_t min;
    uint16_t max;

    if (field >= SCREEN_SET_TIME_FIELD_COUNT) return;

    // Latch the edit BEFORE touching the value: from here the 500ms refresh
    // has to leave the fields alone or it would undo this on the next tick.
    // The seconds field in particular would otherwise be un-settable.
    edit_pending = true;

    min = set_time_fields[field].min;
    max = ScreenSetTimeFieldMax(field);

    // Wrap rather than stopping at the ends: getting from January back to
    // December is one tap either way, and there is no separate readout
    // telling the user they have hit a limit.
    if (up)  staged[field] = (staged[field] >= max) ? min : (uint16_t)(staged[field] + 1u);
    else     staged[field] = (staged[field] <= min) ? max : (uint16_t)(staged[field] - 1u);

    // A month or year step can leave the day past the end of the month it
    // now sits in
    ScreenSetTimeClamp();

    ScreenSetTimeRender();
}

// The one place this screen touches the hardware. All three writes run back
// to back so the RTCC is only unlocked -- and the DMA controller only
// suspended -- for as long as one commit takes. rtccWriteDate() and
// rtccWriteTime() each refresh rtcc_shadow, so the header clock picks the
// new time up on the next refresh with nothing more to do here.
static void ScreenSetTimeCommitClicked(lv_event_t *event)
{
    (void)event;

    rtccWriteDate((uint8_t)staged[SCREEN_SET_TIME_MONTH],
            (uint8_t)staged[SCREEN_SET_TIME_DAY],
            staged[SCREEN_SET_TIME_YEAR]);

    rtccWriteTime((uint8_t)staged[SCREEN_SET_TIME_HOUR],
            (uint8_t)staged[SCREEN_SET_TIME_MINUTE],
            (uint8_t)staged[SCREEN_SET_TIME_SECOND]);

    rtccWriteWeekday(ScreenSetTimeWeekday(staged[SCREEN_SET_TIME_YEAR],
            staged[SCREEN_SET_TIME_MONTH], staged[SCREEN_SET_TIME_DAY]));

    // The staged values are the live ones now, so hand the fields back to
    // the clock -- the seconds column starts counting again, which is the
    // acknowledgement that the write landed.
    edit_pending = false;

    ScreenSetTime_Refresh();
}

static void ScreenSetTimeBackClicked(lv_event_t *event)
{
    (void)event;

    GUI_ShowScreen(GUI_SCREEN_MENU, GUI_NAV_BACK);
}

// Builds one column: caption, "+" above, value in 20pt, "-" below. `x` is
// the offset of the column's center from the center of the panel's content
// box, which is what LV_ALIGN_TOP_MID measures against. Returns false if any
// of the four objects could not be created.
static bool ScreenSetTimeCreateColumn(lv_obj_t *panel, SCREEN_SET_TIME_FIELD field,
        int32_t x)
{
    const SCREEN_SET_TIME_FIELD_DESC *desc = &set_time_fields[field];
    lv_obj_t *caption;
    lv_obj_t *up;
    lv_obj_t *down;

    caption = Screen_CreateLabel(panel, &lv_font_montserrat_14,
            LV_ALIGN_TOP_MID, x, SCREEN_SET_TIME_CAPTION_Y_PX, desc->caption);
    if (caption == NULL) return false;

    lv_obj_set_style_text_color(caption, lv_color_hex(SCREEN_SET_TIME_DIM_TEXT_COLOR),
            LV_PART_MAIN);

    up = Screen_CreateButton(panel, LV_ALIGN_TOP_MID, x, SCREEN_SET_TIME_UP_Y_PX,
            desc->width, SCREEN_SET_TIME_STEP_HEIGHT_PX, LV_SYMBOL_PLUS,
            ScreenSetTimeStepClicked, (void *)SCREEN_SET_TIME_PACK(field, true));
    if (up == NULL) return false;

    // Placeholder sized like the widest real value, so the label is laid out
    // at its final width before the first render and the centered readout
    // does not shift as the number changes digit count
    value_label[field] = Screen_CreateLabel(panel, &lv_font_montserrat_20,
            LV_ALIGN_TOP_MID, x, SCREEN_SET_TIME_VALUE_Y_PX,
            desc->four_digit ? "0000" : "00");
    if (value_label[field] == NULL) return false;

    down = Screen_CreateButton(panel, LV_ALIGN_TOP_MID, x, SCREEN_SET_TIME_DOWN_Y_PX,
            desc->width, SCREEN_SET_TIME_STEP_HEIGHT_PX, LV_SYMBOL_MINUS,
            ScreenSetTimeStepClicked, (void *)SCREEN_SET_TIME_PACK(field, false));
    if (down == NULL) return false;

    return true;
}

lv_obj_t *ScreenSetTime_Create(void)
{
    lv_obj_t *screen = Screen_Create();
    lv_obj_t *panel;
    lv_obj_t *commit;
    int32_t panel_width;
    int32_t column_x = 0;
    uint32_t index;

    if (screen == NULL) return NULL;

    if (!Screen_CreateHeader(screen, "Set Date and Time", &header)) return NULL;

    if (!Screen_AddBackButton(&header, ScreenSetTimeBackClicked, NULL)) return NULL;

    // --- Body panel -------------------------------------------------------
    panel = lv_obj_create(screen);
    if (panel == NULL) return NULL;

    // Plain pixel arithmetic, NOT LV_PCT() minus an inset -- LV_PCT()
    // returns an encoded special value, so subtracting from it silently
    // changes the percentage instead of insetting anything.
    panel_width = LV_HOR_RES - (2 * SCREEN_SET_TIME_PANEL_INSET_PX);

    lv_obj_set_size(panel, panel_width,
            LV_VER_RES - (2 * SCREEN_BAR_HEIGHT_PX) - (2 * SCREEN_SET_TIME_PANEL_INSET_PX));
    lv_obj_align(panel, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(panel, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(panel, SCREEN_BAR_OPACITY, LV_PART_MAIN);
    lv_obj_set_style_border_width(panel, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(panel, SCREEN_SET_TIME_PANEL_PAD_PX, LV_PART_MAIN);

    // Everything fits, so nothing here scrolls -- and a scrollable parent
    // would turn a slightly-dragged tap on a stepper into a scroll gesture
    // and swallow the press.
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    // --- Stepper columns --------------------------------------------------
    // Laid out left to right, accumulating each column's own width so the
    // wide year column pushes the rest along rather than every offset having
    // to be hand-computed.
    for (index = 0; index < SCREEN_SET_TIME_FIELD_COUNT; index++)
    {
        int32_t width = set_time_fields[index].width;
        int32_t center = column_x + (width / 2);

        if (!ScreenSetTimeCreateColumn(panel, (SCREEN_SET_TIME_FIELD)index,
                center - (SCREEN_SET_TIME_CONTENT_W_PX / 2)))
        {
            return NULL;
        }

        column_x += width + SCREEN_SET_TIME_COLUMN_GAP_PX;
    }

    // --- Commit row -------------------------------------------------------
    // The weekday sits on the left as a plain readout: it is derived from
    // the date above it, not something the user picks. Placeholder text
    // sized like the longest name, for the same layout reason as the values.
    weekday_label = Screen_CreateLabel(panel, &lv_font_montserrat_14,
            LV_ALIGN_TOP_LEFT, 0, SCREEN_SET_TIME_WEEKDAY_Y_PX, "Wednesday");
    if (weekday_label == NULL) return NULL;

    lv_obj_set_style_text_color(weekday_label,
            lv_color_hex(SCREEN_SET_TIME_DIM_TEXT_COLOR), LV_PART_MAIN);

    commit = Screen_CreateButton(panel, LV_ALIGN_TOP_RIGHT, 0,
            SCREEN_SET_TIME_COMMIT_Y_PX, SCREEN_SET_TIME_COMMIT_W_PX,
            SCREEN_SET_TIME_COMMIT_H_PX, "Set Clock",
            ScreenSetTimeCommitClicked, NULL);
    if (commit == NULL)
    {
        weekday_label = NULL;
        return NULL;
    }

    screen_built = true;

    ScreenSetTime_Refresh();

    return screen;
}

void ScreenSetTime_Refresh(void)
{
    // ScreenSetTime_Create() either finishes or leaves this clear
    if (!screen_built) return;

    Screen_RefreshHeader(&header);

    // Do not overwrite a half-finished edit. While one is pending the header
    // clock above still shows the live RTCC, so the difference between what
    // is staged and what the clock actually reads stays visible.
    if (edit_pending) return;

    ScreenSetTimeLoadFromRtcc();
    ScreenSetTimeRender();
}

void ScreenSetTime_DiscardEdits(void)
{
    edit_pending = false;
}
