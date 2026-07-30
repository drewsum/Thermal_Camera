/*******************************************************************************
  LVGL Display Port -- GLCD Layer 1

  File Name:
    lv_port_disp.c

  Summary:
    See lv_port_disp.h for the double-buffering rationale.
*******************************************************************************/

#include "gui/lv_port_disp.h"

#include "gui/lvgl/lvgl.h"
#include "glcd/glcd.h"
#include "application/error_handler.h"
#include "application/flir/flir.h"

// Called by LVGL once per redrawn area. In DIRECT render mode LVGL has
// already written the pixels into the off-screen overlay buffer itself, so
// there is nothing to copy here -- the only work is making that buffer the
// one the panel scans out, and only after the LAST area of the frame has
// been rendered (LVGL can split a refresh into several areas, and flipping
// part-way through would show a half-drawn frame).
static void GUI_DisplayFlush(lv_display_t *display, const lv_area_t *area, uint8_t *px_map)
{
    (void)area;

    if (lv_display_flush_is_last(display))
    {
        // While thermal video is streaming, the video's cadence outranks the
        // GUI's flip cosmetics: this wait blocks the main loop for up to a
        // panel frame (~17ms), and a video frame that completes behind it is
        // presented late -- at ~9fps that jitter reads as stutter. Skipping
        // the wait is safe against mid-scan tearing (GLCDL1BADDR is latched
        // at the next frame start); the residual risk is only LVGL starting
        // its next frame into the just-freed buffer before that latch, which
        // takes an animation redrawing on consecutive main-loop passes --
        // worst case a one-frame glitch in the overlay, accepted while video
        // is on screen.
        if (FLIR_GetState() != FLIR_STATE_STREAMING)
        {
            // Flip inside vertical blanking so the swap is invisible. A
            // timeout here means the panel never asserted VSYNC; the flip is
            // still done (a possibly-torn frame beats a frozen GUI), it is
            // just recorded.
            if (!GLCD_WaitOverlayVSync()) error_handler.flags.gui_vsync_timeout = 1;
        }

        GLCD_SetOverlayBaseAddress(px_map);
    }

    lv_display_flush_ready(display);
}

bool lv_port_disp_init(void)
{
    lv_display_t *display = lv_display_create(GLCD_OVERLAY_WIDTH_PX, GLCD_OVERLAY_HEIGHT_PX);

    if (display == NULL) return false;

    // ARGB8888 to match GLCDL1MODE.COLORMODE (glcd.c) -- the per-pixel alpha
    // LVGL writes is what the GLCD Controller blends Layer 1 over Layer 0
    // with, so a transparent screen background leaves the PNG on Layer 0
    // showing through.
    lv_display_set_color_format(display, LV_COLOR_FORMAT_ARGB8888);

    // DIRECT mode with two full-screen buffers: LVGL renders whole frames
    // off-screen and GUI_DisplayFlush() swaps them. GLCD_OverlayInitialize()
    // starts scanout on buffer B, and LVGL starts drawing into buffer A, so
    // the first frame is off-screen as intended.
    lv_display_set_buffers(display,
            (void *)GLCD_OVERLAY_BUFFER_A_ADDRESS,
            (void *)GLCD_OVERLAY_BUFFER_B_ADDRESS,
            GLCD_OVERLAY_SIZE_BYTES,
            LV_DISPLAY_RENDER_MODE_DIRECT);

    lv_display_set_flush_cb(display, GUI_DisplayFlush);

    return true;
}
