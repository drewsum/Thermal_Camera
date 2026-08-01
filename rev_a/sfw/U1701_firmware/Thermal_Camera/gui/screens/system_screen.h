/*******************************************************************************
  System Status GUI Screen

  File Name:
    system_screen.h

  Summary:
    The second screen: the numbers an operator would otherwise have to open
    a serial terminal to see.

  Description:
    Same furniture as the demo screen (transparent background, translucent
    top bar with the clock), with a translucent panel in the middle holding
    label/value rows:

      +--------------------------------------------------+
      | System Status                         07-31-2026 |
      |                                         14:32:07 |
      | +----------------------------------------------+ |
      | | Firmware            0.2 / Rev A              | |
      | | Uptime              0d 01:12:33              | |
      | | MCU Die Temp        45.2 C                   | |
      | | +3.0V Rail          3.012 V                  | |
      | | +1.8V Rail          1.799 V                  | |
      | | GUI Heap            [####------]  12%        | |
      | | Errors              none                     | |
      | +----------------------------------------------+ |
      +--------------------------------------------------+

    Everything shown is read from a cached copy (telemetry, the error
    handler's flag array, LVGL's heap monitor) -- SystemScreen_Refresh()
    runs in the main loop and never talks to a device.
*******************************************************************************/

#ifndef SYSTEM_SCREEN_H
#define SYSTEM_SCREEN_H

#include "gui/lvgl/lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// Builds the screen and returns it, without showing it -- gui.c owns which
// screen is loaded. Returns NULL if a widget couldn't be created.
lv_obj_t *SystemScreen_Create(void);

// Re-reads every value on the screen. Called from GUI_Tasks() every 500ms
// while this screen is the active one; safe to call more often, and
// before/without Create() having succeeded.
void SystemScreen_Refresh(void);

#ifdef __cplusplus
}
#endif

#endif /* SYSTEM_SCREEN_H */
