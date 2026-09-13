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

    THE ROW POOL. The list does not build a widget tree per image. Every
    LVGL scroll step moves, and every redraw and press walks, all of a
    panel's descendants, and a row here is six objects -- so a card of a
    couple of hundred images made each frame of a drag walk over a thousand
    objects, and opening the screen or deleting an image built or tore down
    all of them. Instead the panel holds a fixed pool of rows (a little more
    than a screenful), each placed at its catalog index times the row pitch
    and re-pointed at a different image as the list scrolls (LV_EVENT_SCROLL
    rebinds only the rows that went out of view). A style-less 1x1 marker at
    the bottom of the last row gives the panel the full list's scroll extent.
    The rows' callbacks read the image a row currently stands for from the
    pool, not from anything fixed when the row was built.

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
    filesystem work -- the directory scan and, per image, a full PNG decode
    to make its preview. Neither belongs in the 500ms refresh, and the
    previews cannot all be decoded up front (a hundred images would be half
    a minute of frozen panel). So this screen owns an LVGL timer that runs
    from lv_timer_handler() and does at most ONE piece of that work per
    tick:

      - the first tick after the screen becomes active, and after any
        mount/unmount, re-scans the directory and rebuilds the list
      - after that, decodes ONE preview per tick: a visible row's first,
        then a page or so either side of the view, then nothing

    which is what makes the list appear immediately with its previews
    filling in behind it, rather than the panel arriving late and complete.
    A decode stalls the main loop, and a touch that lands in one is read
    late -- so no decode starts while a finger is down, while the list is
    still coasting or springing back, or until the touchscreen has been left
    alone for a moment after that. Nothing decodes while an image is being
    viewed full-screen or while the screen is off either, so this costs
    nothing when it is not being looked at.

    THE PREVIEW CACHE. Every preview is kept, in DDR2, for as long as its
    file is on the card: one fixed 4KB cell per catalog entry
    (SCREEN_SAVED_IMAGES_THUMB_CACHE_* below, 1MB in all -- see the map in
    gui/gui.h). Scrolling back over a row, leaving the screen and coming
    back, or deleting an image never decodes a preview a second time. After
    every scan or delete the cache is re-lined up with the catalog by file
    name, size and timestamp, so a preview follows its image when the list
    renumbers, and a different card or a re-used file number is never shown
    another image's picture. The bookkeeping is in .bss, so nothing DDR2 held
    before a reset is ever drawn.

    A preview is LVGL's LV_COLOR_FORMAT_RGB888, whose byte order is the
    reverse of the GLCD layers' -- see ImageLoader_DecodeThumbnail().
*******************************************************************************/

#ifndef SCREEN_SAVED_IMAGES_H
#define SCREEN_SAVED_IMAGES_H

#include "gui/lvgl/lvgl.h"
#include "application/image_catalog.h"
#include "core/ddr2.h"

#ifdef __cplusplus
extern "C" {
#endif

// The preview cache's DDR2 reservation (see THE PREVIEW CACHE above, and the
// map in gui/gui.h). Cached KSEG0: only the CPU reads it -- LVGL blends the
// previews into the overlay in software. One cell per catalog entry, sized
// to a page so that every cell starts on an LV_DRAW_BUF_ALIGN boundary.
// Public so the storage reports can account for it.
#define SCREEN_SAVED_IMAGES_THUMB_CACHE_OFFSET    0x00A00000u   // DDR2 +10MB
#define SCREEN_SAVED_IMAGES_THUMB_CACHE_ADDRESS \
        (DDR2_KSEG0_BASE_ADDRESS + SCREEN_SAVED_IMAGES_THUMB_CACHE_OFFSET)
#define SCREEN_SAVED_IMAGES_THUMB_CELL_BYTES      4096u
#define SCREEN_SAVED_IMAGES_THUMB_CACHE_SIZE \
        (IMAGE_CATALOG_MAX_ENTRIES * SCREEN_SAVED_IMAGES_THUMB_CELL_BYTES)

// Builds the screen and returns it, without showing it -- gui.c owns which
// screen is loaded. Also builds the row pool and starts the worker timer
// described above. Returns NULL if a widget could not be created.
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
