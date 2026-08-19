/*******************************************************************************
  Storage Usage GUI Screen

  File Name:
    screen_storage.h

  Summary:
    The panel-side equivalent of the "Storage Usage?" USB UART command: how
    full each mounted FAT volume is, and what the three memories on this
    board are carved up into.

  Description:
    Reached from the main menu. One scrolling panel of label/value rows with
    a usage bar per section, in the same order the command prints them:

      +--------------------------------------------------+
      | < | Storage Usage                     08-18-2026 |
      |                                         14:32:07 |
      | +----------------------------------------------+ |
      | | microSD Card (0:)                            | |
      | | Total               29.7 GB                  | |
      | | Used                [###-------]  28.4%      | |
      | | Free                21.2 GB                  | |
      | | SPI Flash (1:)                               | |
      | | ...                                          | |
      | | Internal SRAM                                | |
      | | Static (.data+.bss) [####------]  41.2%      | |
      | | ...                                          | |
      | +----------------------------------------------+ |
      +--------------------------------------------------+

    FOUR SECTIONS, AND ONLY THE FIRST TWO ARE REAL USAGE. This distinction is
    the whole reason the command prints as much prose as it does, and it is
    kept here:

      microSD / SPI flash   true live figures, from FatFs f_getfree()
      Internal SRAM         static (.data+.bss) is live, from the linker's
                            _end symbol; heap and stack are the fixed
                            RESERVATIONS the linker's best-fit allocator made
                            at link time, not current usage -- this toolchain
                            exposes no mallinfo()-style introspection
      Program Flash         capacity only. No linker symbol on this device
                            marks the end of used flash the way _end does for
                            RAM, so there is no "used" figure to show
      DDR2 SDRAM            the fixed firmware reservations documented in
                            gui/gui.h, since core/ddr2.h has no allocator.
                            The LVGL heap is the one entry with a live
                            utilization figure, and it has its own row on the
                            system status screen

    Rows that are a reservation rather than a measurement say so, so that a
    "100% used" SRAM figure is not read as an imminent overflow.

    COST OF A REFRESH. The two FAT volumes are the expensive part:
    SDFileIO_GetVolumeInfo() and FlashFileIO_GetVolumeInfo() both call
    f_getfree(), which is a blocking media read. They are throttled and
    cached exactly the way screen_sd_card.c throttles the same call, rather
    than run at the 500ms refresh rate -- these numbers only move when
    something writes a file. Everything else on the screen is arithmetic on
    build-time constants and costs nothing.
*******************************************************************************/

#ifndef SCREEN_STORAGE_H
#define SCREEN_STORAGE_H

#include "gui/lvgl/lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// Builds the storage usage screen. Returns the screen object, or NULL on
// failure.
lv_obj_t *ScreenStorage_Create(void);

// Re-reads the volume figures (throttled -- see above) and rewrites every
// row. Safe to call if Create() failed.
void ScreenStorage_Refresh(void);

#ifdef __cplusplus
}
#endif

#endif /* SCREEN_STORAGE_H */
