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

    Screens: all of them are built at init and kept. gui.c holds the list --
    adding a screen means writing a Create/Refresh pair and adding one row
    to it. Switching between them is GUI_NextScreen(); nothing currently
    calls it (the panel's touch controller is not wired up -- see below),
    so it is exposed for a future UART command or menu.

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

      +0        230,400 B  KSEG1  GLCD Layer 0 video buffer A (glcd.h)
                                    -- thermal video from the FLIR VoSPI path
      +512KB    230,400 B  KSEG1  GLCD Layer 0 video buffer B (glcd.h)
                                    -- double buffered; flir_process.c flips
      +1MB      307,200 B  KSEG1  GLCD Layer 1 overlay buf A (glcd.h)
      +2MB      307,200 B  KSEG1  GLCD Layer 1 overlay buf B (glcd.h)
      +3MB      4MB        KSEG0  LVGL heap                  (lv_conf.h)
      +7MB      230,400 B  KSEG1  GLCD Layer 2 still-image buffer (glcd.h)
                                    -- also what the Saved Images screen
                                       shows a picked image through
      +8MB      2x 38,400 B KSEG0 FLIR VoSPI raw frame buffers A/B (flir_vospi.h)
                                    -- cached: CPU-only, no DMA touches them
      +9MB       38,400 B  KSEG0  still capture, raw 14-bit frame (still_capture.h)
                                    -- cached, CPU-only, same as the two above
      +9MB+64KB 230,400 B  KSEG1  still capture, frozen RGB888 image
                                    -- uncached: Layer 0 scans it out directly
                                       while the shutter capture is held
      +10MB     22MB              unreserved

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
// to redraw. Does nothing if GUI_Initialize() didn't succeed.
void GUI_Tasks(void);

// The screens that make up the navigable hierarchy: home is the boot screen
// shown over the live thermal video, its "Menu" button opens the main menu,
// and the menu's rows open the screens below it. These ARE the indices into
// gui.c's screen table (it is initialized with designated initializers, so
// the two cannot drift).
typedef enum
{
    GUI_SCREEN_HOME = 0,
    GUI_SCREEN_MENU,
    GUI_SCREEN_SYSTEM,
    GUI_SCREEN_PALETTE,
    GUI_SCREEN_BRIGHTNESS,
    GUI_SCREEN_SD_CARD,
    GUI_SCREEN_SAVED_IMAGES,
    GUI_SCREEN_ID_COUNT
} GUI_SCREEN_ID;

// Which way the slide animation runs. Purely cosmetic, but it is what makes
// the hierarchy legible: going deeper slides the new screen in from the
// right, backing out reverses it.
typedef enum
{
    GUI_NAV_FORWARD = 0,   // home -> menu -> system
    GUI_NAV_BACK           // the back buttons
} GUI_NAV_DIRECTION;

// Loads `id` with a slide animation in `direction`, and refreshes it so it
// shows current values rather than whatever was on it when it last went out
// of view. This is what the on-screen Menu and back buttons call. Also
// leaves the FLIR error screen or the save-image prompt if one was showing.
// No-op until GUI_Initialize() has succeeded.
void GUI_ShowScreen(GUI_SCREEN_ID id, GUI_NAV_DIRECTION direction);

// Returns whether `id` is the screen currently on the panel. False while an
// on-demand screen (the FLIR error screen, the save-image prompt) is up,
// since neither of those is one of the screens above, and false before
// GUI_Initialize() has succeeded.
//
// This exists so a subsystem can do work only while the screen that consumes
// it is actually visible: heartbeatServices() uses it to sample the sensors
// feeding the system status screen, which would otherwise only be read when
// the UART live telemetry page is enabled.
//
// Safe to call from interrupt context (heartbeatServices() runs in the
// Timer1 ISR): it only reads word-sized statics that main-loop code writes,
// touches no LVGL state, and prints nothing -- see the no-printf-in-ISR rule
// this codebase follows.
bool GUI_IsScreenActive(GUI_SCREEN_ID id);

// Switches to the next screen, wrapping around, with a slide animation.
// Nothing currently calls this; exposed for a future UART command. Also
// leaves the FLIR error screen if it was showing (see
// GUI_ShowFlirErrorScreen() below). No-op until GUI_Initialize() has
// succeeded.
void GUI_NextScreen(void);

// Loads the FLIR error screen (gui/screens/flir_error_screen.c) in place of
// whatever screen is currently showing, with the reason read from the
// latched flir_* error_handler flag. main() calls this when
// FLIR_WaitUntilReady() returns false -- there is no thermal video to show
// behind the GUI otherwise. Not part of the normal GUI_NextScreen() cycle;
// call GUI_NextScreen() to leave it. No-op until GUI_Initialize() has
// succeeded.
void GUI_ShowFlirErrorScreen(void);

// Loads the save-image prompt (gui/screens/screen_save_image.c) over the
// frozen thermal frame the shutter just captured. application/still_capture.c
// calls this; like the FLIR error screen it is outside the navigable screen
// list above. It leaves itself: its Cancel/Done and back buttons call
// GUI_ShowScreen(GUI_SCREEN_HOME, ...) once the capture is resolved. No-op
// until GUI_Initialize() has succeeded.
void GUI_ShowSaveImageScreen(void);

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
