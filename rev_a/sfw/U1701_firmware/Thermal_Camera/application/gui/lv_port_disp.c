/*******************************************************************************
  LVGL Display Port -- GLCD Layer 1 overlay

  File Name:
    lv_port_disp.c

  Summary:
    Zero-copy LVGL display bound to the GLCD Layer 1 overlay framebuffer.
    See lv_port_disp.h for the summary and glcd/glcd.h for the overlay layer
    (GLCD_OVERLAY_*) and DDR2 placement.

  Why the flush is a no-op:
    LVGL is given the overlay framebuffer itself as its single, full-screen
    render buffer, in LV_DISPLAY_RENDER_MODE_DIRECT. So LVGL draws directly
    into the memory the GLCD Controller's DMA scans out. That framebuffer is
    the DDR2 uncached (KSEG1) alias (glcd.h), so the controller already sees
    every pixel LVGL wrote with no cache writeback -- there is nothing for the
    flush callback to transfer.

  Known limitation (future refinement):
    Single-buffer direct rendering into the live scanout buffer can tear on
    fast-changing content. It is fine for the current static/slow overlay; a
    second buffer plus a vertical-blank swap would remove tearing later.
*******************************************************************************/

#include "lvgl/lvgl.h"
#include "glcd/glcd.h"
#include "application/gui/lv_port_disp.h"

static void disp_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    LV_UNUSED(area);
    LV_UNUSED(px_map);

    // The render buffer IS the GLCD Layer 1 scanout buffer (uncached KSEG1
    // DDR2), so the controller's DMA already sees the rendered pixels. No
    // transfer, no cache maintenance -- just release the buffer back to LVGL.
    lv_display_flush_ready(disp);
}

lv_display_t *lv_port_disp_init(void)
{
    lv_display_t *disp = lv_display_create(GLCD_OVERLAY_WIDTH_PX, GLCD_OVERLAY_HEIGHT_PX);
    if (disp == NULL)
    {
        return NULL;
    }

    // Per-pixel alpha: the GLCD blends Layer 1 over Layer 0 using each pixel's
    // alpha. Must match GLCD_COLORMODE_ARGB8888 programmed into GLCDL1MODE in
    // glcd.c (0xAARRGGBB byte order).
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_ARGB8888);

    // Direct render mode, single full-screen buffer = the overlay framebuffer
    // itself (glcd.h GLCD_OVERLAY_BASE_ADDRESS, KSEG1 uncached). Size is the
    // whole layer in bytes; buf2 = NULL (single buffer -- see the tearing note
    // in the file header).
    lv_display_set_buffers(disp,
                           (void *)GLCD_OVERLAY_BASE_ADDRESS,
                           NULL,
                           GLCD_OVERLAY_SIZE_BYTES,
                           LV_DISPLAY_RENDER_MODE_DIRECT);

    lv_display_set_flush_cb(disp, disp_flush_cb);

    return disp;
}
