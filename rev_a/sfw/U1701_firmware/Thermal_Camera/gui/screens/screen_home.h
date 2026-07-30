/*******************************************************************************
  Home GUI Screen

  File Name:
    screen_home.h

  Summary:
    The heads-up display shown on GLCD Layer 1 -- the first screen shown at
    boot, and the one the live FLIR video plays behind.

  Description:
    Layout:

      +--------------------------------------------------+
      | Thermal Camera                        2026-07-21 |  translucent bar
      |                                         14:32:07 |
      |    +--+  42.3 C                                  |
      |    |##|                                          |
      |    |##|      (GLCD Layer 0 shows through)         |
      |    |##|                                          |
      |    +--+  18.7 C                                  |
      | Amb 24.6 C                     Batt [####--] 62% |  translucent bar
      +--------------------------------------------------+

    The screen background is fully transparent and the two bars are only
    partly opaque, so whatever application/flir/flir_process.c has rendered
    into Layer 0 stays visible behind the GUI -- that transparency is the
    whole point of the ARGB8888 overlay, and makes it obvious at a glance
    whether hardware compositing is working.

    The left-side scale is a plain lv_obj with its background painted as a
    vertical gradient built from the active FLIR palette's control points
    (application/flir/flir_process.c), so it always matches what the palette
    lookup is actually drawing -- hottest color at the top, coldest at the
    bottom. The min/max labels above and below it read the same AGC window
    (application/flir/flir_process.c: FLIRProcess_GetAGCWindowCelsius())
    that the renderer stretches the image against, converted to Celsius.

    Every value shown is read from a cached copy maintained elsewhere
    (rtcc_shadow from the RTCC ISR, telemetry from telemetryTasks(), the FLIR
    AGC window from the last render), never by talking to a device --
    ScreenHome_Refresh() runs in the main loop and must not block on I2C.
*******************************************************************************/

#ifndef SCREEN_HOME_H
#define SCREEN_HOME_H

#include "gui/lvgl/lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// Builds the screen and returns it, without showing it -- gui.c owns which
// screen is loaded. Call once, after lv_init() and lv_port_disp_init().
// Returns NULL if a widget couldn't be created.
lv_obj_t *ScreenHome_Create(void);

// Re-reads the clock, ambient temperature, battery state and FLIR palette
// scale into the labels. Called from GUI_Tasks() every 500ms while this
// screen is the active one; safe to call more often, and before/without
// Create() having succeeded.
void ScreenHome_Refresh(void);

#ifdef __cplusplus
}
#endif

#endif /* SCREEN_HOME_H */
