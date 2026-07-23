/*******************************************************************************
  Graphical User Interface (LVGL)

  File Name:
    gui.h

  Summary:
    Brings up LVGL on GLCD Layer 1 and pumps it from the main loop.

  Description:
    The graphics stack, bottom to top:

      glcd/glcd.c                 GLCD Controller registers; owns Layer 0
                                  (opaque RGB888 background) and Layer 1
                                  (transparent ARGB8888 GUI overlay)
      gui/lv_port_disp.c          binds LVGL's lv_display to Layer 1's two
                                  overlay buffers (double buffered)
      gui/lvgl/                   vendored LVGL v9.3.0, configured by
                                  gui/lv_conf.h
      gui/screens/screen_common.c the look every screen shares
      gui/screens/*_screen.c      the individual screens
      gui/gui.c                   this module: init, tick source, log/assert
                                  plumbing, the screen list, and GUI_Tasks()
                                  from main()

    Screens: all of them are built at init and kept; the shutter button
    (via shutter_button_press_event, pushbuttons.h) cycles through them.
    gui.c holds the list -- adding a screen means writing a Create/Refresh
    pair and adding one row to it.

    Module shape mirrors application/image_loader.c and application/telemetry.c:
    one Initialize() at boot and one Tasks() called every pass of main()'s
    loop. There is no RTOS (LV_USE_OS = LV_OS_NONE) and no input device --
    the panel's GT911 touch controller is not currently wired up (its I2C
    entry in i2c/i2c_devices.h is commented out), so the demo screen is
    display-only.

    Timing: LVGL needs a millisecond tick, which lv_tick_set_cb() takes from
    GUI_GetTickMs() reading the CP0 Count register (SYSCLK/2 = 100MHz, so it
    wraps about every 43 seconds -- the conversion below accumulates deltas
    rather than scaling an absolute count, which makes the wrap harmless).
    That deliberately avoids adding work to an ISR. GUI_Tasks() calls
    lv_timer_handler() every pass (it is cheap when nothing is invalidated,
    and it is what drives animations and redraws); the screen's live values
    are re-read at 500ms, requested by heartbeatServices() setting
    gui_refresh_request.

    DDR2 partition map (the whole 32MB, since core/ddr2.h has no allocator
    and every reservation is by convention -- this is the one place the full
    picture is written down):

      +0        230,400 B  KSEG1  GLCD Layer 0 frame buffer  (glcd.h)
                                    -- thermal video from the FLIR VoSPI path
      +1MB      307,200 B  KSEG1  GLCD Layer 1 overlay buf A (glcd.h)
      +2MB      307,200 B  KSEG1  GLCD Layer 1 overlay buf B (glcd.h)
      +3MB      4MB        KSEG0  LVGL heap                  (lv_conf.h)
      +7MB      230,400 B  KSEG1  GLCD Layer 2 still-image buffer (glcd.h)
      +8MB      2x 38,400 B KSEG1 FLIR VoSPI raw frame buffers A/B (flir_vospi.h)
      +9MB      23MB              unreserved

    The overlay buffers are uncached because the GLCD Controller's DMA reads
    them (same reasoning as Layer 0, see core/ddr2.h's cache note). The LVGL
    heap is cached because only the CPU touches it -- it holds LVGL's own
    objects and styles AND, since lodepng now allocates through lv_malloc(),
    every PNG decode application/image_loader.c performs.

    Error handling: the GUI is not critical to the instrument, so failures
    are latched into the error handler and execution continues rather than
    halting -- a broken GUI must not take down the UART console, telemetry,
    or USB mass storage. GUI_AssertHandler() (LV_ASSERT_HANDLER) therefore
    returns instead of LVGL's default while(1). The flags:

      gui_init_error       GUI_Initialize() failed at boot
      gui_lvgl_error       LVGL logged an error, or an LV_ASSERT_* fired
      gui_heap_exhausted   lv_malloc() returned NULL (LV_USE_ASSERT_MALLOC)
      gui_vsync_timeout    GLCD_WaitOverlayVSync() gave up waiting for the
                           panel's vertical blanking interval
*******************************************************************************/

#ifndef GUI_H
#define GUI_H

#include <stdint.h>
#include <stdbool.h>

#include "gui/gui_assert.h"

#ifdef __cplusplus
extern "C" {
#endif

// The LVGL heap, mirrored from LV_MEM_ADR/LV_MEM_SIZE in gui/lv_conf.h so
// that the "Storage Usage?" command can report this DDR2 reservation without
// including lvgl.h (and so this header can document the map above). Keep the
// two in sync -- GUI_Initialize() checks them against lv_conf.h at build time.
#define GUI_LVGL_HEAP_BASE_ADDRESS   0x88300000u
#define GUI_LVGL_HEAP_SIZE_BYTES     (4u * 1024u * 1024u)

// Set by heartbeatServices() every 500ms to ask GUI_Tasks() to re-read the
// live values on screen (clock, temperature, battery). Same idiom as
// telemetry.h's temp_sense_data_request / battery_data_request.
volatile __attribute__((coherent)) uint8_t gui_refresh_request;

// Initializes LVGL (heap, tick source, log/assert hooks), binds it to GLCD
// Layer 1 via lv_port_disp_init(), and builds the demo screen. Must run
// after GLCD_Initialize() (it enables Layer 1 on a running controller) and
// after ddr2Initialize() (every buffer it uses is in DDR2). Returns false on
// any failure; main() reports that into error_handler.flags.gui_init_error.
bool GUI_Initialize(void);

// Pumps LVGL. Call every pass of main()'s loop. Cheap when there is nothing
// to redraw. Also consumes shutter_button_press_event (pushbuttons.h) to
// switch screens. Does nothing if GUI_Initialize() didn't succeed.
void GUI_Tasks(void);

// Switches to the next screen, wrapping around, with a slide animation.
// Normally driven by the shutter button via GUI_Tasks(); exposed so a UART
// command or a future menu can do the same. No-op until GUI_Initialize()
// has succeeded.
void GUI_NextScreen(void);

// Milliseconds since boot, derived from CP0 Count. LVGL's tick source; also
// useful to the screens for their own timing.
uint32_t GUI_GetTickMs(void);

// Prints LVGL/display/heap status. Backs the "Peripheral Status? GUI"
// USB UART command.
void GUI_PrintStatus(void);

#ifdef __cplusplus
}
#endif

#endif /* GUI_H */
