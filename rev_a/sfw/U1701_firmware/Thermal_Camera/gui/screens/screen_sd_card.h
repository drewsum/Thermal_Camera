/*******************************************************************************
  SD Card Info GUI Screen

  File Name:
    screen_sd_card.h

  Summary:
    The panel-side equivalent of the "SD Card Info?" USB UART command: the
    CID/CSD-derived card metadata and the mounted FAT volume's numbers,
    without a serial terminal.

  Description:
    Same furniture as the system status screen (transparent background,
    translucent top bar with the clock and a back button, one scrolling
    panel of label/value rows):

      +--------------------------------------------------+
      | SD Card Info                          08-18-2026 |
      |                                         14:32:07 |
      | +----------------------------------------------+ |
      | | Card                                         | |
      | | Status              Mounted                  | |
      | | Type                SDHC                     | |
      | | Capacity            30436 MB                 | |
      | | ...                                          | |
      | | Volume                                       | |
      | | Filesystem          FAT32                    | |
      | | Volume Label        SD                       | |
      | | Total Space         29.7 GB                  | |
      | | Free Space          21.2 GB                  | |
      | | Used                [###-------]  28%        | |
      | +----------------------------------------------+ |
      +--------------------------------------------------+

    Every state the card can be in is a legible one, since the whole point
    of the screen is to be looked at when something is wrong:

      no card inserted              card rows read "--", volume rows "--"
      card present but not mounted  card rows populated, volume rows "--"
      media owned by the USB host   card rows populated, volume rows read
                                    "in use by USB host" (FatFs has been
                                    handed off -- see SDFileIO_IsMounted())
      mounted                       everything populated

    Cost of a refresh: the card metadata is a cached struct read
    (SD_Card_GetInfo()), so it is free. The volume numbers are NOT --
    SDFileIO_GetVolumeInfo() calls f_getfree(), which is a blocking SD
    read, so it is throttled and cached rather than run at the 500ms
    refresh rate (same treatment system_screen.c gives the DS1683). Card
    presence is deliberately never polled here: SD_Card_IsPresent() busy-
    waits 5ms debouncing, which has no business running in the main loop
    behind the FLIR video path -- the hot-swap path (SDFileIO_HotSwapTasks())
    already keeps the driver state this screen reads up to date.
*******************************************************************************/

#ifndef SCREEN_SD_CARD_H
#define SCREEN_SD_CARD_H

#include "gui/lvgl/lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// Builds the screen and returns it, without showing it -- gui.c owns which
// screen is loaded. Returns NULL if a widget couldn't be created.
lv_obj_t *ScreenSDCard_Create(void);

// Re-reads every value on the screen. Called from GUI_Tasks() every 500ms
// while this screen is the active one; safe to call more often (the
// blocking volume read is throttled internally), and before/without
// Create() having succeeded.
void ScreenSDCard_Refresh(void);

#ifdef __cplusplus
}
#endif

#endif /* SCREEN_SD_CARD_H */
