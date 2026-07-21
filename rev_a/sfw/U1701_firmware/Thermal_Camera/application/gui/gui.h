/*******************************************************************************
  GUI Module -- high-level LVGL control

  File Name:
    gui.h

  Summary:
    Owns the LVGL GUI that renders into the GLCD Layer 1 overlay (the
    ARGB8888 framebuffer the controller alpha-blends over Layer 0's image /
    thermal feed -- see glcd/glcd.h GLCD_OVERLAY_*). This module hides LVGL
    behind the same simple, cooperative shape the rest of the codebase uses:
    a one-shot Initialize plus a Tasks() pump called from the main loop
    (mirroring application/image_loader.c).

  Description:
    GUI_Initialize() brings LVGL up (lv_init(), a millisecond tick sourced
    from CP0 Count like the rest of this project, the display port in
    application/gui/lv_port_disp.c) and builds a static demo screen that
    exercises the stack -- translucent panels, three font sizes, rounded
    corners and per-pixel alpha -- so the overlay visibly composites over
    whatever image the "Display Image" USB-UART command has loaded into
    Layer 0. Touch input (indev) is deliberately not wired yet: the GT911
    controller is detected but its coordinate readout is unimplemented
    (i2c/device_driver/gt911.c), so the demo screen is non-interactive for
    now. Adding application/gui/lv_port_indev.c is the next step.

  Prerequisites (main.c init order):
    DDR2 must be up (ddr2Initialize(); LVGL's heap lives in DDR2 via
    lv_conf.h LV_MEM_ADR) and the GLCD Controller initialized
    (GLCD_Initialize(), which programs and clears Layer 1) before
    GUI_Initialize() is called.
*******************************************************************************/

#ifndef GUI_H
#define GUI_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// LVGL's widget/style heap reservation in DDR2 (the pool LVGL's built-in
// allocator manages). MUST match lv_conf.h's LV_MEM_SIZE / LV_MEM_ADR -- it is
// mirrored here (rather than shared) because lv_conf.h is consumed by LVGL
// before any project header is available. Exposed so the "Storage Usage?"
// USB-UART command can report this DDR2 reservation. See glcd/glcd.h for the
// rest of the GUI's DDR2 footprint (the two Layer 1 overlay buffers).
#define GUI_LVGL_HEAP_SIZE_BYTES   (2u * 1024u * 1024u)

// Brings up LVGL and builds the demo screen on the GLCD Layer 1 overlay.
// Assumes DDR2 and the GLCD Controller are already initialized (see the
// header comment). Returns false if LVGL could not create its display;
// GUI_Tasks() is then an inert no-op.
bool GUI_Initialize(void);

// Pump once per main-loop iteration (cooperative, main-loop context only --
// same shape as ImageLoader_Tasks()). Drives LVGL's timers/rendering via
// lv_timer_handler(). Cheap no-op until GUI_Initialize() has succeeded.
void GUI_Tasks(void);

#ifdef __cplusplus
}
#endif

#endif /* GUI_H */
