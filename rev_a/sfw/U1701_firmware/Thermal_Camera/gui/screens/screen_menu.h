/*******************************************************************************
  Main Menu GUI Screen

  File Name:
    screen_menu.h

  Summary:
    The main menu: a table of tappable rows, reached from the home screen's
    "Menu" button and left by the back button in its header.

  Description:
    Layout: the shared header (gui/screens/screen_common.c) with a back
    button on the left returning to the home screen, and a body panel filled
    with one row per entry. Each row is a full-width tap target with its name
    on the left and a chevron on the right.

    Adding an entry is one row in menu_entries[] in the .c file -- a label
    and the GUI_SCREEN_ID it opens. The table is the only thing that needs
    editing; the rows, their event wiring and the panel height all come off
    it. If an entry ever needs to do something other than change screens,
    that is the point to give the entry struct an action callback instead of
    a screen id.

    Like every other screen here it is built once at boot and kept (see the
    screen table in gui/gui.c), so its rows are laid out before the first
    tap, and switching to it is a pointer swap.
*******************************************************************************/

#ifndef SCREEN_MENU_H
#define SCREEN_MENU_H

#include "gui/lvgl/lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// Builds the menu screen. Returns the screen object, or NULL on failure.
lv_obj_t *ScreenMenu_Create(void);

// Re-reads the live values on the screen -- just the header clock, since the
// rows themselves are static. Safe to call if Create() failed.
void ScreenMenu_Refresh(void);

#ifdef __cplusplus
}
#endif

#endif /* SCREEN_MENU_H */
