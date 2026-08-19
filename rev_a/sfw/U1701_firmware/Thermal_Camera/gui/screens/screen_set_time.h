/*******************************************************************************
  Set Date and Time GUI Screen

  File Name:
    screen_set_time.h

  Summary:
    Sets the Real Time Clock and Calendar (core/rtcc.c) from the panel: six
    stepper fields and one button that commits them.

  Description:
    Reached from the main menu. Layout is the shared header
    (gui/screens/screen_common.c) with a back button returning to the menu,
    and a body panel holding six columns, each a "+" over a value over a "-":

      +--------------------------------------------------+
      | < | Set Date and Time                 08-18-2026 |
      |                                         14:32:07 |
      | +----------------------------------------------+ |
      | | MON   DAY   YEAR    HR    MIN   SEC          | |
      | | [+]   [+]   [ + ]   [+]   [+]   [+]          | |
      | |  08    18    2026    14    32    07          | |
      | | [-]   [-]   [ - ]   [-]   [-]   [-]          | |
      | |                                              | |
      | | Tuesday                        [ Set Clock ] | |
      | +----------------------------------------------+ |
      +--------------------------------------------------+

    STAGED, NOT LIVE. Unlike the brightness screen -- which drives the
    backlight on every touch report because the user judges the result by
    eye -- the steppers here only edit a local copy. Nothing reaches the
    RTCC until "Set Clock" is tapped, and then all three of
    rtccWriteDate/Time/Weekday run back to back.

    That is a hardware constraint, not a preference: rtccUnlock() and
    rtccLock() each disable global interrupts AND suspend the DMA controller
    (DMACON.SUSPEND, spinning on DMABUSY) around the RTCWREN write. The FLIR
    VoSPI receive path is a chained DMA stream that must not miss a segment
    (see application/flir/), so every one of those suspensions is a chance
    to break the video feed. Staging turns "one suspension per button tap"
    into three for the whole edit.

    The weekday is NOT one of the fields -- it is computed from the staged
    date (Sakamoto's algorithm) and shown at the bottom left, because a
    weekday the user can set independently of the date is a weekday that can
    disagree with it. The UART "Set RTCC: Weekday:" command still sets it
    directly for the case where the RTCC and the calendar really do need to
    differ.

    WHAT REFRESH DOES. With no edit pending, the six fields track the live
    clock, so arriving on the screen shows the current time and the seconds
    field counts. The first tap on any stepper latches an edit, and from
    there the fields hold what the user is building while the header clock
    in the top-right keeps showing the live RTCC -- which is what makes the
    pending change visible. "Set Clock" clears the latch, as does leaving
    the screen (see ScreenSetTime_DiscardEdits() below).

    Like every other screen here it is built once at boot and kept (see the
    screen table in gui/gui.c), so switching to it is a pointer swap.
*******************************************************************************/

#ifndef SCREEN_SET_TIME_H
#define SCREEN_SET_TIME_H

#include "gui/lvgl/lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// The RTCC stores the year as two BCD digits with an implied 2000 (see
// rtccWriteDate()), so this is the whole range the hardware can hold.
#define SCREEN_SET_TIME_YEAR_MIN   2000u
#define SCREEN_SET_TIME_YEAR_MAX   2099u

// Builds the set date and time screen. Returns the screen object, or NULL
// on failure.
lv_obj_t *ScreenSetTime_Create(void);

// Re-reads the live RTCC into the fields, unless an edit is pending -- see
// the refresh note above. Always updates the header clock. Safe to call if
// Create() failed.
void ScreenSetTime_Refresh(void);

// Drops a half-finished edit, so the screen is next arrived at showing the
// live clock rather than whatever was being built when it was last left.
// gui.c calls this from every screen change, the same way it dismisses the
// saved-image viewer: the shutter button can navigate away from here
// without the back button ever being tapped. No-op when nothing is pending.
void ScreenSetTime_DiscardEdits(void);

#ifdef __cplusplus
}
#endif

#endif /* SCREEN_SET_TIME_H */
