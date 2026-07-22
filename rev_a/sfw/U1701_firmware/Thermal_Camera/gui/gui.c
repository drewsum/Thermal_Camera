/*******************************************************************************
  Graphical User Interface (LVGL)

  File Name:
    gui.c

  Summary:
    LVGL bring-up, tick source, diagnostics plumbing, and the main-loop pump.
    See gui.h for the stack layout, the DDR2 map and the error-flag policy.
*******************************************************************************/

#include <xc.h>
#include <stdio.h>

#include "gui/gui.h"
#include "gui/lv_port_disp.h"
#include "gui/lvgl/lvgl.h"
#include "gui/screens/demo_screen.h"
#include "gui/screens/system_screen.h"

#include "application/error_handler.h"
#include "application/pushbuttons.h"
#include "core/device_control.h"
#include "glcd/glcd.h"
#include "usb_uart/terminal_control.h"

// The heap constants in gui.h are a hand-mirrored copy of lv_conf.h's, so
// that the storage report doesn't have to include all of LVGL. Catch them
// drifting apart at build time rather than reporting a fictional number.
#if (GUI_LVGL_HEAP_BASE_ADDRESS != LV_MEM_ADR) || (GUI_LVGL_HEAP_SIZE_BYTES != LV_MEM_SIZE)
    #error "GUI_LVGL_HEAP_* in gui/gui.h no longer matches LV_MEM_ADR/LV_MEM_SIZE in gui/lv_conf.h"
#endif

// Set once GUI_Initialize() has fully succeeded. GUI_Tasks() is called
// unconditionally from main()'s loop, and calling into LVGL before lv_init()
// (or after a failed init) would dereference an unbuilt global state.
static bool gui_ready = false;

// *****************************************************************************
// Section: Screens
// *****************************************************************************
// Every screen is built once at init and kept, rather than created and
// destroyed on each switch: the widgets are small next to the 4MB heap, and
// keeping them means switching is just a pointer swap with no chance of an
// allocation failing halfway through a screen change. Only the ACTIVE
// screen is refreshed (see GUI_Tasks()), so an off-screen one costs nothing
// per pass. Add a screen by writing its Create/Refresh pair and adding one
// row here -- the shutter button cycles through however many there are.

typedef struct
{
    lv_obj_t *(*create)(void);
    void (*refresh)(void);
    lv_obj_t *screen;
} GUI_SCREEN;

static GUI_SCREEN gui_screens[] =
{
    { DemoScreen_Create,   DemoScreen_Refresh,   NULL },
    { SystemScreen_Create, SystemScreen_Refresh, NULL },
};

#define GUI_SCREEN_COUNT  (sizeof(gui_screens) / sizeof(gui_screens[0]))

static uint32_t gui_active_screen = 0;

// Ignore a second shutter press within this long of the last one. The
// buttons are capacitive and should already produce clean edges, so this is
// precautionary -- but a bouncing button that toggled the screen twice
// would look like the press was simply ignored.
#define GUI_SCREEN_SWITCH_DEBOUNCE_MS   250

// How long the slide between screens takes. Set to 0 for an instant swap if
// the animation ever misbehaves against the transparent overlay.
#define GUI_SCREEN_SWITCH_ANIM_MS       250

// Milliseconds since boot, accumulated from CP0 Count deltas. Count runs at
// SYSCLK/2 and is only 32 bits, so it wraps roughly every 43 seconds --
// accumulating deltas makes that wrap harmless (the unsigned subtraction
// below is still correct across it), where scaling the absolute count would
// make the clock jump backwards on every wrap.
static uint32_t gui_tick_ms = 0;
static uint32_t gui_tick_last_count = 0;
static uint32_t gui_tick_remainder = 0;

uint32_t GUI_GetTickMs(void)
{
    uint32_t now = _CP0_GET_COUNT();
    uint32_t elapsed = now - gui_tick_last_count;   // wrap-safe
    uint32_t ticks_per_ms = (uint32_t)(SYSCLK_INT / 2u) / 1000u;

    gui_tick_last_count = now;

    // Carry the sub-millisecond remainder forward so the tick doesn't lose
    // time on every call (GUI_Tasks() calls this far more often than 1kHz)
    elapsed += gui_tick_remainder;
    gui_tick_ms += elapsed / ticks_per_ms;
    gui_tick_remainder = elapsed % ticks_per_ms;

    return gui_tick_ms;
}

// LVGL's log sink (LV_USE_LOG). Warnings and errors only, per LV_LOG_LEVEL
// in lv_conf.h -- LVGL has already formatted and level-prefixed the text.
static void GUI_LogPrint(lv_log_level_t level, const char *buffer)
{
    if (level >= LV_LOG_LEVEL_ERROR) error_handler.flags.gui_lvgl_error = 1;

    terminalTextAttributes((level >= LV_LOG_LEVEL_ERROR) ? RED_COLOR : YELLOW_COLOR,
            BLACK_COLOR, NORMAL_FONT);
    printf("LVGL: %s\r\n", buffer);
    terminalTextAttributesReset();
}

// Below this much contiguous free space the LVGL heap can no longer serve a
// PNG decode (a 320x240 RGB888 frame alone is 230,400 bytes), so an
// allocation failure at that point is exhaustion rather than a one-off.
#define GUI_HEAP_EXHAUSTED_THRESHOLD_BYTES  (256u * 1024u)

void GUI_AssertHandler(void)
{
    // LVGL doesn't tell the handler which assertion fired, but the one that
    // matters here is LV_USE_ASSERT_MALLOC -- lv_malloc() returning NULL.
    // Check the heap directly so that case gets its own flag and points at
    // "Peripheral Status? GUI" / "Storage Usage?" rather than being lumped
    // in with every other LVGL fault.
    lv_mem_monitor_t monitor;

    lv_mem_monitor(&monitor);

    if (monitor.free_biggest_size < GUI_HEAP_EXHAUSTED_THRESHOLD_BYTES)
    {
        error_handler.flags.gui_heap_exhausted = 1;
    }

    error_handler.flags.gui_lvgl_error = 1;

    terminalTextAttributes(RED_COLOR, BLACK_COLOR, BOLD_FONT);
    printf("LVGL assertion failed -- GUI may be in an undefined state\r\n");
    terminalTextAttributesReset();

    // Deliberately returns: see gui.h. Upstream's default is while(1).
}

bool GUI_Initialize(void)
{
    if (gui_ready) return true;

    lv_init();

    if (!lv_is_initialized()) return false;

    // Both of these callbacks live in lv_global_t, which lv_init() zeroes --
    // registering them first would silently discard them (and a GUI with no
    // tick source never refreshes at all). The cost is that warnings logged
    // during lv_init() itself go nowhere.
    lv_log_register_print_cb(GUI_LogPrint);

    // Seed the CP0 reference before handing LVGL the tick source, otherwise
    // the first call reports every millisecond since reset in one jump.
    gui_tick_last_count = _CP0_GET_COUNT();
    lv_tick_set_cb(GUI_GetTickMs);

    if (!lv_port_disp_init()) return false;

    // Enable the hardware layer last of the three: the overlay buffers must
    // already be LVGL's (they are cleared to transparent in here) and the
    // display object must exist before the panel starts compositing Layer 1.
    if (!GLCD_OverlayInitialize()) return false;

    // Build every screen, then show the first
    {
        uint32_t index;

        for (index = 0; index < GUI_SCREEN_COUNT; index++)
        {
            gui_screens[index].screen = gui_screens[index].create();

            if (gui_screens[index].screen == NULL) return false;
        }
    }

    gui_active_screen = 0;
    lv_screen_load(gui_screens[0].screen);

    gui_ready = true;

    return true;
}

void GUI_NextScreen(void)
{
    if (!gui_ready) return;

    gui_active_screen = (gui_active_screen + 1u) % GUI_SCREEN_COUNT;

    // auto_del = false: the outgoing screen is kept, since these are built
    // once and cycled through
    lv_screen_load_anim(gui_screens[gui_active_screen].screen,
            LV_SCR_LOAD_ANIM_MOVE_LEFT, GUI_SCREEN_SWITCH_ANIM_MS, 0, false);

    // Show current values immediately rather than whatever was on this
    // screen when it last went out of view (up to 500ms stale, and much
    // more if it has been off-screen a while)
    gui_screens[gui_active_screen].refresh();
}

void GUI_Tasks(void)
{
    if (!gui_ready) return;

    // Advance to the next screen on a shutter press. Consumed here, at
    // main-loop level, rather than in the Port A change-notice ISR that
    // latched it -- building a frame is far too much work for IPL3.
    if (shutter_button_press_event)
    {
        static uint32_t last_switch_ms = 0;
        uint32_t now_ms = GUI_GetTickMs();

        shutter_button_press_event = 0;

        // Wrap-safe: GUI_GetTickMs() is monotonic, so an unsigned
        // subtraction stays correct across its (49-day) rollover
        if ((now_ms - last_switch_ms) >= GUI_SCREEN_SWITCH_DEBOUNCE_MS)
        {
            last_switch_ms = now_ms;
            GUI_NextScreen();
        }
    }

    // Re-read the live values on screen when heartbeatServices() asks
    // (every 500ms). Cheap: it only touches cached telemetry/RTCC copies,
    // and LVGL redraws only the labels whose text actually changed.
    if (gui_refresh_request)
    {
        gui_refresh_request = 0;
        gui_screens[gui_active_screen].refresh();
    }

    // Drives redraws, animations and LVGL's own timers. Returns quickly
    // when there is nothing invalidated.
    lv_timer_handler();
}

void GUI_PrintStatus(void)
{
    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, BOLD_FONT);
    printf("    --- Graphical User Interface (LVGL) ---\n\r");

    if (!gui_ready)
    {
        terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("    Not initialized\n\r");
        terminalTextAttributesReset();
        return;
    }

    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("    LVGL Version: %u.%u.%u\n\r",
            (unsigned int)LVGL_VERSION_MAJOR,
            (unsigned int)LVGL_VERSION_MINOR,
            (unsigned int)LVGL_VERSION_PATCH);

    lv_display_t *display = lv_display_get_default();

    if (display != NULL)
    {
        printf("    Display: %ldx%ld, color format 0x%02X, direct render, double buffered\n\r",
                (long)lv_display_get_horizontal_resolution(display),
                (long)lv_display_get_vertical_resolution(display),
                (unsigned int)lv_display_get_color_format(display));
    }

    printf("    Tick: %lu ms since boot (CP0 Count, SYSCLK/2)\n\r",
            (unsigned long)GUI_GetTickMs());

    // Heap: the DDR2 pool LV_MEM_ADR/LV_MEM_SIZE hand to LVGL's built-in
    // allocator, which also backs every lodepng decode (image_loader.c)
    {
        lv_mem_monitor_t monitor;

        lv_mem_monitor(&monitor);

        printf("    LVGL Heap: 0x%08lX, %lu bytes (DDR2, cached KSEG0)\n\r",
                (unsigned long)GUI_LVGL_HEAP_BASE_ADDRESS,
                (unsigned long)GUI_LVGL_HEAP_SIZE_BYTES);
        printf("    Heap Used: %lu bytes (%u%%), free %lu bytes, largest free block %lu bytes\n\r",
                (unsigned long)(monitor.total_size - monitor.free_size),
                (unsigned int)monitor.used_pct,
                (unsigned long)monitor.free_size,
                (unsigned long)monitor.free_biggest_size);

        if (monitor.frag_pct > 50)
        {
            terminalTextAttributes(YELLOW_COLOR, BLACK_COLOR, NORMAL_FONT);
            printf("    Heap Fragmentation: %u%%\n\r", (unsigned int)monitor.frag_pct);
            terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
        }
        else
        {
            printf("    Heap Fragmentation: %u%%\n\r", (unsigned int)monitor.frag_pct);
        }
    }

    terminalTextAttributesReset();
}
