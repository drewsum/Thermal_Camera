/*******************************************************************************
  LVGL Assert Handler Declaration

  File Name:
    gui_assert.h

  Summary:
    Declares GUI_AssertHandler(), the function gui/lv_conf.h names as
    LV_ASSERT_HANDLER.

  Description:
    This exists as its own one-declaration header purely to break an include
    cycle: lv_conf.h needs LV_ASSERT_HANDLER_INCLUDE to name a header that
    declares the handler, and that header gets pulled in from deep inside
    LVGL (src/misc/lv_assert.h) while lvgl.h is still being processed. Naming
    gui/gui.h there would be circular, because gui.h includes lvgl.h.

    The handler itself is defined in gui.c -- see gui.h for why it latches
    an error flag and returns instead of halting.
*******************************************************************************/

#ifndef GUI_ASSERT_H
#define GUI_ASSERT_H

#ifdef __cplusplus
extern "C" {
#endif

// Called by LVGL's LV_ASSERT_* macros when an assertion fails
void GUI_AssertHandler(void);

#ifdef __cplusplus
}
#endif

#endif /* GUI_ASSERT_H */
