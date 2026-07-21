/**
 * @file lv_drivers.h
 *
 * Vendored-for-this-project stub.
 *
 * Upstream LVGL's lv_drivers.h #includes every bundled platform driver header
 * (SDL, X11, Linux DRM/fbdev, NuttX, Windows, Wayland, TFT_eSPI, various MIPI
 * display controllers, etc.) UNCONDITIONALLY -- each driver's *body* is guarded
 * by its LV_USE_* config, but the #include lines are not, so all those header
 * files must exist to compile. This bare-metal PIC32MZ-DA target uses none of
 * them (the display is driven by application/gui/lv_port_disp.c against the
 * GLCD Layer 1 framebuffer), so those driver source/header trees were removed
 * from the vendored tree to cut build size and Windows object-path length.
 *
 * This stub therefore includes nothing. If a bundled LVGL driver is ever
 * genuinely needed, restore its headers under src/drivers/ and re-add the
 * corresponding #include here.
 */

#ifndef LV_DRIVERS_H
#define LV_DRIVERS_H

#ifdef __cplusplus
extern "C" {
#endif

/* Intentionally empty -- see the file header above. */

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*LV_DRIVERS_H*/
