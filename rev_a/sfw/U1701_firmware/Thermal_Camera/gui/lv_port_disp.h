/*******************************************************************************
  LVGL Display Port -- GLCD Layer 1

  File Name:
    lv_port_disp.h

  Summary:
    Binds LVGL's lv_display to the GLCD Controller's Layer 1 overlay.

  Description:
    LVGL renders in DIRECT mode into two full-screen ARGB8888 buffers in DDR2
    (GLCD_OVERLAY_BUFFER_A/B_ADDRESS, glcd.h). Only one of them is ever being
    scanned out; LVGL draws the complete frame into the other, and the flush
    callback flips Layer 1's base address to it during vertical blanking.

    That is what makes the GUI tear-free. With a single buffer, LVGL's
    clear-then-redraw of a changed area happens in the memory the panel is
    actively reading, which shows up as flicker; with two, the panel only
    ever sees finished frames, and the swap itself lands in the blanking
    interval where it is invisible.

    Note DIRECT mode means LVGL keeps both buffers whole and in sync, so the
    cost is 2 x 300KB of DDR2 rather than the small partial buffers a
    PARTIAL-mode port would use. DDR2 is 32MB; the tear-free result is worth
    far more here than the memory.
*******************************************************************************/

#ifndef LV_PORT_DISP_H
#define LV_PORT_DISP_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Creates the lv_display, points it at the two overlay buffers, and installs
// the flush callback. Call after lv_init() and before building any screen.
// Returns false if LVGL refused to create the display.
bool lv_port_disp_init(void);

#ifdef __cplusplus
}
#endif

#endif /* LV_PORT_DISP_H */
