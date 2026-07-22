/*******************************************************************************
  Demo GUI Screen

  File Name:
    demo_screen.h

  Summary:
    The heads-up display shown on GLCD Layer 1.

  Description:
    A deliberately small demonstration of the LVGL-on-Layer-1 stack, and the
    template for real screens later. Layout:

      +--------------------------------------------------+
      | Thermal Camera                        2026-07-21 |  translucent bar
      |                                         14:32:07 |
      |                                                  |
      |            (GLCD Layer 0 shows through)          |
      |                                                  |
      | Amb 24.6 C                     Batt [####--] 62% |  translucent bar
      +--------------------------------------------------+

    The screen background is fully transparent and the two bars are only
    partly opaque, so whatever application/image_loader.c has decoded into
    Layer 0 stays visible behind the GUI -- that transparency is the whole
    point of the ARGB8888 overlay, and makes it obvious at a glance whether
    hardware compositing is working.

    Every value shown is read from a cached copy maintained elsewhere
    (rtcc_shadow from the RTCC ISR, telemetry from telemetryTasks()), never
    by talking to a device -- DemoScreen_Refresh() runs in the main loop and
    must not block on I2C.
*******************************************************************************/

#ifndef DEMO_SCREEN_H
#define DEMO_SCREEN_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Builds the screen and makes it active. Call once, after lv_init() and
// lv_port_disp_init(). Returns false if a widget couldn't be created.
bool DemoScreen_Create(void);

// Re-reads the clock, ambient temperature and battery state into the labels.
// Called from GUI_Tasks() every 500ms; safe to call more often.
void DemoScreen_Refresh(void);

#ifdef __cplusplus
}
#endif

#endif /* DEMO_SCREEN_H */
