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
#include "gui/lv_port_indev.h"
#include "gui/lvgl/lvgl.h"
#include "gui/screens/screen_home.h"
#include "gui/screens/screen_menu.h"
#include "gui/screens/screen_palette.h"
#include "gui/screens/system_screen.h"
#include "gui/screens/flir_error_screen.h"
#include "gui/screens/screen_save_image.h"

#include "application/error_handler.h"
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
// row here.

typedef struct
{
    lv_obj_t *(*create)(void);
    void (*refresh)(void);
    lv_obj_t *screen;
} GUI_SCREEN;

// Designated initializers keep this locked to GUI_SCREEN_ID (gui.h), which
// is what GUI_ShowScreen() and the menu's rows navigate by -- reordering the
// enum reorders the table with it instead of silently pointing every caller
// at the wrong screen.
static GUI_SCREEN gui_screens[GUI_SCREEN_ID_COUNT] =
{
    [GUI_SCREEN_HOME]    = { ScreenHome_Create,    ScreenHome_Refresh,    NULL },
    [GUI_SCREEN_MENU]    = { ScreenMenu_Create,    ScreenMenu_Refresh,    NULL },
    [GUI_SCREEN_SYSTEM]  = { SystemScreen_Create,  SystemScreen_Refresh,  NULL },
    [GUI_SCREEN_PALETTE] = { ScreenPalette_Create, ScreenPalette_Refresh, NULL },
};

#define GUI_SCREEN_COUNT  (sizeof(gui_screens) / sizeof(gui_screens[0]))

static uint32_t gui_active_screen = 0;

// Screens built at init like every other one, but shown only on demand
// rather than being part of the gui_screens[] cycle above: the FLIR error
// screen (GUI_ShowFlirErrorScreen(), see flir_error_screen.h) and the
// save-image prompt (GUI_ShowSaveImageScreen(), see screen_save_image.h).
static lv_obj_t *flir_error_screen = NULL;
static lv_obj_t *save_image_screen = NULL;

// The refresh function of whichever on-demand screen is showing, or NULL when
// one of the cycled gui_screens[] is. This is what GUI_Tasks() refreshes, and
// it is a pointer rather than one flag per screen so adding the next on-demand
// screen stays a one-line change instead of another branch in three places.
static void (*gui_on_demand_refresh)(void) = NULL;

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

    // Touch input. Created unconditionally, even though main.c has not yet
    // probed the CTP at this point in boot -- the read callback checks for
    // the controller itself on every poll, so a board with no panel just
    // never reports a press (see gui/lv_port_indev.h).
    if (!lv_port_indev_init()) return false;

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

    // Built alongside the others (see the comment by their declarations), but
    // not loaded here -- they stay off-screen until their Show call.
    flir_error_screen = FlirErrorScreen_Create();
    if (flir_error_screen == NULL) return false;

    save_image_screen = ScreenSaveImage_Create();
    if (save_image_screen == NULL) return false;

    gui_ready = true;

    return true;
}

// The one place a cycled screen is actually loaded. Safe to call from an
// LVGL event callback (i.e. from a button on the outgoing screen): auto_del
// is false, so nothing the event is still walking gets deleted underneath
// it.
static void GUILoadScreen(uint32_t index, GUI_NAV_DIRECTION direction)
{
    gui_on_demand_refresh = NULL;
    gui_active_screen = index;

    lv_screen_load_anim(gui_screens[index].screen,
            (direction == GUI_NAV_BACK) ? LV_SCR_LOAD_ANIM_MOVE_RIGHT
                                        : LV_SCR_LOAD_ANIM_MOVE_LEFT,
            GUI_SCREEN_SWITCH_ANIM_MS, 0, false);

    // Show current values immediately rather than whatever was on this
    // screen when it last went out of view (up to 500ms stale, and much
    // more if it has been off-screen a while)
    gui_screens[index].refresh();
}

void GUI_ShowScreen(GUI_SCREEN_ID id, GUI_NAV_DIRECTION direction)
{
    if (!gui_ready) return;
    if ((uint32_t)id >= GUI_SCREEN_COUNT) return;

    GUILoadScreen((uint32_t)id, direction);
}

bool GUI_IsScreenActive(GUI_SCREEN_ID id)
{
    if (!gui_ready) return false;

    // An on-demand screen is covering the cycled one, so nothing in
    // gui_screens[] is on the panel right now
    if (gui_on_demand_refresh != NULL) return false;

    return (gui_active_screen == (uint32_t)id);
}

void GUI_NextScreen(void)
{
    if (!gui_ready) return;

    GUILoadScreen((gui_active_screen + 1u) % GUI_SCREEN_COUNT, GUI_NAV_FORWARD);
}

void GUI_ShowFlirErrorScreen(void)
{
    if (!gui_ready) return;

    gui_on_demand_refresh = FlirErrorScreen_Refresh;

    // Instant swap, no slide: this can fire during boot (main() calls it
    // right after FLIR_WaitUntilReady() fails, before the splash screen on
    // Layer 2 has been dismissed), where an animation would just be wasted
    // work under the still-opaque splash.
    lv_screen_load(flir_error_screen);
    FlirErrorScreen_Refresh();
}

void GUI_ShowSaveImageScreen(void)
{
    if (!gui_ready) return;

    gui_on_demand_refresh = ScreenSaveImage_Refresh;

    // Instant swap, again no slide: this one sits over a frozen thermal
    // frame that still_capture.c has just put on Layer 0, and a slide would
    // drag the header/footer across the very image the prompt is about.
    lv_screen_load(save_image_screen);
    ScreenSaveImage_Refresh();
}

void GUI_Tasks(void)
{
    if (!gui_ready) return;

    // Re-read the live values on screen when heartbeatServices() asks
    // (every 500ms). Cheap: it only touches cached telemetry/RTCC copies,
    // and LVGL redraws only the labels whose text actually changed.
    if (gui_refresh_request)
    {
        gui_refresh_request = 0;

        if (gui_on_demand_refresh != NULL)
        {
            gui_on_demand_refresh();
        }
        else
        {
            gui_screens[gui_active_screen].refresh();
        }
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
