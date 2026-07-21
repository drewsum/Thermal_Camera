/*******************************************************************************
  GUI Module -- high-level LVGL control

  File Name:
    gui.c

  Summary:
    See gui.h for the module summary, init-order prerequisites, and the
    touch-deferred note.

  Millisecond tick:
    LVGL needs a monotonic millisecond clock. Rather than add a timer ISR,
    this uses the same time base the rest of the project already uses for
    delays and profiling: the MIPS CP0 Count register, which increments at
    SYSCLK/2 (image_loader.c, glt035320240is1.c, sd_card.c). GUI_TickCb()
    converts CP0 Count to milliseconds and accumulates them so the returned
    value keeps climbing across CP0's ~43s wrap (at 200MHz SYSCLK, CP0 wraps
    every 2^32 / 100MHz ~= 42.9s). It is wrap-safe as long as it is called
    more often than once per wrap, which lv_timer_handler() guarantees.

  Performance note (see also glcd/glcd.h):
    LVGL renders into the overlay framebuffer through DDR2's uncached (KSEG1)
    alias, which is coherent with the GLCD's scanout DMA without cache
    maintenance but makes read-modify-write blending slower than a cached
    buffer. At 320x240 with LVGL's dirty-area redraw this is fine for the
    demo; a cached overlay buffer with explicit writeback is the future
    optimization if a busy live UI needs more headroom.
*******************************************************************************/

#include <xc.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "lvgl/lvgl.h"
#include "application/gui/gui.h"
#include "application/gui/lv_port_disp.h"
#include "core/device_control.h"   // SYSCLK_INT
#include "core/rtcc.h"             // rtcc_shadow (ISR-maintained date/time)
#include "application/telemetry.h" // telemetry.ambient_temperature (cached)

static bool s_initialized = false;

// Live-data widgets updated from GUI_Tasks(): the center ambient-temperature
// readout and the top-right date/time. Set in GUI_BuildDemoScreen().
static lv_obj_t *s_temp_label   = NULL;
static lv_obj_t *s_clock_label  = NULL;

// How often GUI_Tasks() refreshes the live readouts, in milliseconds. 500ms
// keeps the clock's seconds visibly current without redrawing every loop.
#define GUI_LIVE_UPDATE_MS  500u

// CP0-Count-derived monotonic millisecond source for LVGL. CP0 Count runs at
// SYSCLK/2, so SYSCLK_INT/2000 counts per millisecond. Accumulating the
// elapsed milliseconds (rather than dividing the raw count) keeps the value
// monotonic across CP0's 32-bit wrap. Main-loop context only (not reentrant).
static uint32_t GUI_TickCb(void)
{
    static uint32_t last_count = 0;
    static uint32_t ms_accum   = 0;
    static bool     primed     = false;

    const uint32_t ticks_per_ms = (uint32_t)(SYSCLK_INT / 2u) / 1000u;  // 100000 @200MHz

    uint32_t now = _CP0_GET_COUNT();
    if (!primed)
    {
        last_count = now;
        primed = true;
        return ms_accum;
    }

    uint32_t elapsed = now - last_count;          // unsigned: wrap-safe
    if (elapsed >= ticks_per_ms)
    {
        uint32_t ms = elapsed / ticks_per_ms;
        ms_accum += ms;
        last_count += ms * ticks_per_ms;          // carry the sub-ms remainder
    }
    return ms_accum;
}

// Builds a static, non-interactive demo screen on the active (Layer 1)
// display. The screen background is left fully transparent so GLCD Layer 0
// (the image / thermal feed) shows through everywhere the GUI hasn't drawn;
// the panels use partial alpha so the image also shows through *behind* them.
static void GUI_BuildDemoScreen(void)
{
    lv_obj_t *scr = lv_screen_active();

    // Transparent screen -> undrawn areas have alpha 0 -> Layer 0 shows
    // through completely. This is the whole point of the overlay layer.
    lv_obj_set_style_bg_opa(scr, LV_OPA_TRANSP, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    // --- Top title bar: translucent black strip across the top ---
    // Tall enough for a two-line clock (date over time) on the right.
    lv_obj_t *topbar = lv_obj_create(scr);
    lv_obj_set_size(topbar, LV_PCT(100), 44);
    lv_obj_align(topbar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_remove_flag(topbar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(topbar, 0, 0);
    lv_obj_set_style_border_width(topbar, 0, 0);
    lv_obj_set_style_bg_color(topbar, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(topbar, LV_OPA_50, 0);
    lv_obj_set_style_pad_all(topbar, 5, 0);

    lv_obj_t *title = lv_label_create(topbar);
    lv_label_set_text(title, "THERMAL CAM");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 0, 0);

    // Top-right: live date (top line) over time (bottom line), from the RTCC
    // via rtcc_shadow, refreshed by GUI_UpdateLiveData(). Right-aligned;
    // placeholder text until the first update.
    s_clock_label = lv_label_create(topbar);
    lv_label_set_text(s_clock_label, "----------\n--:--:--");
    lv_obj_set_style_text_color(s_clock_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(s_clock_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(s_clock_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(s_clock_label, LV_ALIGN_RIGHT_MID, 0, 0);

    // --- Center readings card: translucent rounded panel ---
    lv_obj_t *card = lv_obj_create(scr);
    lv_obj_set_size(card, 170, 104);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, 6);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(card, 10, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, lv_color_white(), 0);
    lv_obj_set_style_border_opa(card, LV_OPA_40, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x101820), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_60, 0);
    lv_obj_set_style_pad_all(card, 8, 0);

    // Big center readout: live ambient temperature (Montserrat 28, amber),
    // updated by GUI_UpdateLiveData() from telemetry.ambient_temperature.
    // ASCII only -- the trimmed Montserrat set has no degree glyph.
    s_temp_label = lv_label_create(card);
    lv_label_set_text(s_temp_label, "--.- C");
    lv_obj_set_style_text_font(s_temp_label, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(s_temp_label, lv_color_hex(0xFFC020), 0);
    lv_obj_align(s_temp_label, LV_ALIGN_TOP_MID, 0, 0);

    // Min / max readout row (Montserrat 14, white).
    lv_obj_t *mm = lv_label_create(card);
    lv_label_set_text(mm, "MIN 18.0    MAX 41.2");
    lv_obj_set_style_text_font(mm, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(mm, lv_color_white(), 0);
    lv_obj_align(mm, LV_ALIGN_BOTTOM_MID, 0, 0);

    // --- Bottom bar: three faux (non-interactive) buttons ---
    lv_obj_t *botbar = lv_obj_create(scr);
    lv_obj_set_size(botbar, LV_PCT(100), 30);
    lv_obj_align(botbar, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_remove_flag(botbar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(botbar, 0, 0);
    lv_obj_set_style_border_width(botbar, 0, 0);
    lv_obj_set_style_bg_color(botbar, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(botbar, LV_OPA_50, 0);
    lv_obj_set_style_pad_all(botbar, 4, 0);

    lv_obj_t *foot = lv_label_create(botbar);
    lv_label_set_text(foot, "MENU        SAVE        MODE");
    lv_obj_set_style_text_font(foot, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(foot, lv_color_white(), 0);
    lv_obj_center(foot);
}

// Refreshes the live readouts from data the main loop already maintains:
// the cached ambient temperature (telemetry.ambient_temperature, folded in by
// telemetryTasks()) and the RTCC date/time shadow (rtcc_shadow, updated by the
// RTCC ISR every second). Both are plain memory reads -- no blocking I2C or
// register polling happens here. lv_label_set_text() only invalidates/redraws
// when the string actually changes, so calling this on a fixed cadence is cheap.
static void GUI_UpdateLiveData(void)
{
    char buf[24];

    if (s_temp_label != NULL)
    {
        snprintf(buf, sizeof buf, "%.1f C", (double)telemetry.ambient_temperature);
        lv_label_set_text(s_temp_label, buf);
    }

    if (s_clock_label != NULL)
    {
        // A one-off torn read across a second boundary is harmless for a clock.
        snprintf(buf, sizeof buf, "%04u-%02u-%02u\n%02u:%02u:%02u",
                 (unsigned)rtcc_shadow.year,    (unsigned)rtcc_shadow.month,
                 (unsigned)rtcc_shadow.day,     (unsigned)rtcc_shadow.hours,
                 (unsigned)rtcc_shadow.minutes, (unsigned)rtcc_shadow.seconds);
        lv_label_set_text(s_clock_label, buf);
    }
}

bool GUI_Initialize(void)
{
    lv_init();
    lv_tick_set_cb(GUI_TickCb);

    lv_display_t *disp = lv_port_disp_init();
    if (disp == NULL)
    {
        return false;   // GUI_Tasks() stays a no-op
    }

    GUI_BuildDemoScreen();
    GUI_UpdateLiveData();   // show real values immediately, not the placeholders

    s_initialized = true;
    return true;
}

void GUI_Tasks(void)
{
    if (!s_initialized)
    {
        return;
    }

    // Refresh the live readouts on a fixed cadence (not every loop). lv_tick_get()
    // is our CP0-derived millisecond clock; the subtraction is wrap-safe.
    static uint32_t last_update_ms = 0;
    uint32_t now_ms = lv_tick_get();
    if ((uint32_t)(now_ms - last_update_ms) >= GUI_LIVE_UPDATE_MS)
    {
        last_update_ms = now_ms;
        GUI_UpdateLiveData();
    }

    lv_timer_handler();
}
