/*******************************************************************************
  LVGL Display Port -- GLCD Layer 1 overlay (double-buffered, vsync page flip)

  File Name:
    lv_port_disp.c

  Summary:
    LVGL display bound to the GLCD Layer 1 overlay, rendered TEAR- AND
    FLICKER-FREE via double buffering with a vsync-synchronised page flip.
    See lv_port_disp.h for the summary and glcd/glcd.h for the overlay layer
    (GLCD_OVERLAY_*) and its two-buffer DDR2 placement.

  How it works:
    LVGL is given BOTH full-screen overlay buffers (A and B, glcd.h) in
    LV_DISPLAY_RENDER_MODE_DIRECT. Each refresh it renders the changed areas
    into whichever buffer is currently OFF-screen -- so the clear-then-redraw
    of a widget never touches the buffer being scanned out. On the last flush
    of the refresh (lv_display_flush_is_last), the freshly-rendered buffer is
    complete; disp_flush_cb() then waits for the panel's vertical blanking
    (GLCD_WaitOverlayVSync) and repoints Layer 1 at that buffer
    (GLCD_SetOverlayBaseAddress). Because the base address changes only during
    blanking, the next frame scans out entirely from the new, fully-composited
    buffer -- no partial redraw is ever visible.

    The buffers are the DDR2 uncached (KSEG1) alias (glcd.h), so LVGL's writes
    are already coherent with the GLCD's scanout DMA -- no cache maintenance.
    GLCD_Initialize() starts Layer 1 on buffer B and LVGL renders into buffer A
    first, so that first render lands off-screen.
*******************************************************************************/

#include "lvgl/lvgl.h"
#include "glcd/glcd.h"
#include "application/gui/lv_port_disp.h"

static void disp_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    LV_UNUSED(area);

    // In DIRECT mode LVGL renders all of a refresh's dirty areas into the same
    // (off-screen) buffer and the frame is complete only at the last flush.
    // px_map points to that just-rendered buffer's start. Flip to it during
    // vertical blanking so the swap is tear-free; earlier (non-last) flushes
    // have nothing to do but acknowledge.
    if (lv_display_flush_is_last(disp))
    {
        GLCD_WaitOverlayVSync();
        GLCD_SetOverlayBaseAddress((uint32_t)px_map);
    }

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

    // Double-buffered direct mode: both full-screen overlay buffers, so LVGL
    // always renders into the off-screen one and disp_flush_cb() page-flips
    // between them. buffer A is buf1 (LVGL renders it first) and GLCD_Initialize
    // starts scanout on B, keeping that first render off-screen.
    lv_display_set_buffers(disp,
                           (void *)GLCD_OVERLAY_BASE_ADDRESS,    // buffer A (buf1)
                           (void *)GLCD_OVERLAY_BASE_ADDRESS_B,  // buffer B (buf2)
                           GLCD_OVERLAY_SIZE_BYTES,
                           LV_DISPLAY_RENDER_MODE_DIRECT);

    lv_display_set_flush_cb(disp, disp_flush_cb);

    return disp;
}
