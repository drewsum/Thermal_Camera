/*******************************************************************************
  LVGL Input Device Port (GT911 Capacitive Touch)

  File Name:
    lv_port_indev.h

  Summary:
    Binds the panel's GT911 capacitive touch controller to LVGL as a pointer
    input device, so taps reach the widgets.

  Description:
    Counterpart to gui/lv_port_disp.c: that binds LVGL's output to GLCD
    Layer 1, this binds its input to the touch controller sitting on the
    same LCD module. LVGL polls the read callback from its own input timer
    (every LV_DEF_REFR_PERIOD ms), which is what paces the I2C traffic --
    nothing here runs from an interrupt.

    Single touch only: GT911_ReadTouch() (i2c/device_driver/gt911.h) returns
    just the first touch point, because every interaction in this GUI is a
    tap. Gestures and multi-touch would need the other four point records
    and LVGL's gesture support turned on.

    Absent-panel behavior: the read callback checks
    I2CDevices_IsPresent(I2C_DEV_CTP_1) before touching the bus, so a board
    with no LCD module attached doesn't generate an I2C transaction (and a
    NACK) 30 times a second. The input device is still created in that case
    -- it simply never reports a press -- which keeps GUI_Initialize()
    independent of whether the CTP probe has run yet. It has NOT run yet at
    that point: main.c probes the CTP a few lines later, right before the
    backlight enable.

    COORDINATE MAPPING -- the part to check first if touch lands in the
    wrong place. The GT911 reports in whatever coordinate space its own
    config registers were programmed with at the factory, which is not
    guaranteed to match the panel's scan orientation. The three switches
    below (swap/invert) are the entire adjustment; verify on hardware by
    tapping a known corner and watching "Peripheral Status? Touch".
*******************************************************************************/

#ifndef LV_PORT_INDEV_H
#define LV_PORT_INDEV_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Touch-to-panel axis mapping. Defaults assume the controller's axes already
// match the display's -- the GLT035320240IS1-CTP integrates both, so this is
// the expected case, but it is UNVERIFIED ON HARDWARE.
//
//   SWAP_XY   1 = the controller's X is the panel's Y (portrait/landscape
//                 mismatch). Applied BEFORE the inversions below.
//   INVERT_X  1 = taps on the left register on the right
//   INVERT_Y  1 = taps on the top register on the bottom
#define LV_PORT_INDEV_SWAP_XY     0
#define LV_PORT_INDEV_INVERT_X    0
#define LV_PORT_INDEV_INVERT_Y    0

// Creates the LVGL pointer input device and registers the GT911 read
// callback. Call from GUI_Initialize() AFTER lv_port_disp_init() -- the
// input device attaches to the default display, which that creates.
// Returns false only if LVGL refused to create the device.
bool lv_port_indev_init(void);

// Prints the last touch report, the mapping switches above, and the running
// counters. Backs the "Peripheral Status? Touch" USB UART command -- this is
// the intended way to check the axis mapping on the bench.
void lv_port_indev_PrintStatus(void);

#ifdef __cplusplus
}
#endif

#endif /* LV_PORT_INDEV_H */
