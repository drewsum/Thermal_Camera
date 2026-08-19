/*******************************************************************************
  Saved Images GUI Screen

  File Name:
    screen_saved_images.h

  Summary:
    The camera roll: every thermal still on the SD card, with a preview, in a
    list that opens one full screen and can delete one.

  Description:
    Reached from the main menu. The list is application/image_catalog.c's
    view of the directory application/image_saver.c writes into, one row per
    image, newest first:

      +--------------------------------------------------+
      | < | Saved Images (12)                 08-18-2026 |
      |                                         14:32:07 |
      | +----------------------------------------------+ |
      | | [img] FLIR0012.PNG           14 KB       [T] | |
      | | [img] FLIR0011.PNG           13 KB       [T] | |
      | | [img] FLIR0010.PNG           14 KB       [T] | |
      | | ...                                          | |
      | +----------------------------------------------+ |
      +--------------------------------------------------+

    Three things a row can do:

      tap the row     decodes the PNG onto GLCD Layer 2 and enables that
                      layer, which composites the image OVER both the
                      thermal video and this GUI -- the picture fills the
                      panel with nothing drawn on top of it. Tapping
                      anywhere then returns here (see the catcher below).
      tap the bin     asks "Delete FLIR0012.PNG?" over a dimmed list, with
                      Delete and Cancel. Only Delete touches the card.
      scroll          the panel scrolls; the list is as long as the card's
                      directory, up to IMAGE_CATALOG_MAX_ENTRIES.

    HOW THE FULL-SCREEN VIEW TAKES ITS TAP. Layer 2 is a hardware layer
    above the GUI's Layer 1, so while an image is up the GUI is invisible --
    but it is still there, and the touch controller still reports through it
    to LVGL. The screen therefore keeps a full-screen, fully transparent,
    clickable object (the "catcher") that is hidden except while an image is
    showing. It draws nothing; it exists to be the thing every tap lands on
    while the panel is covered, so no button underneath can be pressed
    blind. GUI_ShowScreen() also dismisses the view, so an event that
    changes screens from elsewhere (the shutter button starting a capture)
    cannot leave Layer 2 stranded over a screen that knows nothing about it.

    WHERE THE WORK HAPPENS. Two of the three operations here are blocking
    filesystem work -- the directory scan and, for every visible row, a full
    PNG decode to make its preview. Neither belongs in the 500ms refresh,
    and the previews cannot all be decoded up front (a hundred images would
    be half a minute of frozen panel). So this screen owns an LVGL timer
    that runs from lv_timer_handler() and does at most ONE piece of that
    work per tick:

      - the first tick after the screen becomes active, and after any
        mount/unmount, re-scans the directory and rebuilds the rows
      - every tick after that, decodes ONE preview, for a row that is
        actually scrolled into view

    which is what makes the list appear immediately with its previews
    filling in behind it, rather than the panel arriving late and complete.
    Nothing decodes while an image is being viewed full-screen or while the
    screen is off, so this costs nothing when it is not being looked at.

    Previews are cached in a small fixed pool of buffers rather than one per
    image: only a handful of rows are ever on screen, and a buffer per
    catalog entry would be megabytes. When the pool is full the
    least-recently-used buffer is taken, and the row that had it goes back
    to showing an empty frame until it is scrolled to again. A rebuild
    re-binds the pool to the new rows BY FILENAME, so deleting one image
    does not throw away the previews of all the others.

    A preview is LVGL's LV_COLOR_FORMAT_RGB888, whose byte order is the
    reverse of the GLCD layers' -- see ImageLoader_DecodeThumbnail().
*******************************************************************************/

#ifndef SCREEN_SAVED_IMAGES_H
#define SCREEN_SAVED_IMAGES_H

#include "gui/lvgl/lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// Builds the screen and returns it, without showing it -- gui.c owns which
// screen is loaded. Also allocates the preview buffer pool and starts the
// worker timer described above. Returns NULL if a widget or the pool could
// not be created.
lv_obj_t *ScreenSavedImages_Create(void);

// Re-reads the live values on the screen: the header clock and the title's
// image count. The list itself is rebuilt by the worker timer, not here --
// re-scanning the card twice a second would put a directory walk in the
// main loop behind the FLIR video path. Safe to call if Create() failed.
void ScreenSavedImages_Refresh(void);

// Takes the full-screen image off Layer 2 and hides the catcher, if one is
// showing. Called by the row that opens the view when the user taps to
// leave, and by gui.c whenever the screen changes underneath it -- see the
// catcher note in the description above. No-op when no image is up.
void ScreenSavedImages_DismissViewer(void);

#ifdef __cplusplus
}
#endif

#endif /* SCREEN_SAVED_IMAGES_H */
