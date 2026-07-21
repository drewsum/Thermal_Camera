/*******************************************************************************
  LVGL Display Port -- GLCD Layer 1 overlay

  File Name:
    lv_port_disp.h

  Summary:
    Binds an LVGL display to the GLCD Controller's Layer 1 (the ARGB8888 GUI
    overlay framebuffer in DDR2, see glcd/glcd.h GLCD_OVERLAY_*). Called once
    by GUI_Initialize() (application/gui/gui.c) after lv_init().

  Description:
    The overlay framebuffer is memory-mapped in DDR2 through the uncached
    (KSEG1) alias, so LVGL renders straight into the buffer the GLCD scans
    out -- there is no panel bus/SPI transfer and no cache maintenance, and
    the flush callback is effectively a no-op. See lv_port_disp.c.
*******************************************************************************/

#ifndef LV_PORT_DISP_H
#define LV_PORT_DISP_H

#include "lvgl/lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// Creates and configures the LVGL display for the GLCD Layer 1 overlay
// (ARGB8888, 320x240, direct render into the DDR2 overlay framebuffer).
// Returns the display, or NULL if LVGL could not create it.
lv_display_t *lv_port_disp_init(void);

#ifdef __cplusplus
}
#endif

#endif /* LV_PORT_DISP_H */
