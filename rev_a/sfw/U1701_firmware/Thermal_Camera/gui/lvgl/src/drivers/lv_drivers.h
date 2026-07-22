/**
 * @file lv_drivers.h
 *
 * LOCAL MODIFICATION (Thermal_Camera): upstream, this header unconditionally
 * #includes every platform driver's header (SDL, X11, Linux DRM/fbdev,
 * NuttX, Windows, GLFW, Wayland, UEFI, and a pile of SPI display
 * controllers). None of those drivers exist in this vendored tree -- they
 * were deleted, because the MPLAB VS Code extension builds its file list by
 * SCANNING the source tree, and their sources are variously ARM assembly,
 * C++, or POSIX/desktop-only C that cannot compile for this MIPS target.
 *
 * This target's display driver is gui/lv_port_disp.c (GLCD Controller
 * Layer 1), which is application code rather than part of the LVGL tree. So
 * this header is deliberately emptied rather than deleted -- lvgl.h includes
 * it unconditionally, and keeping the file means lvgl.h stays unpatched.
 */

#ifndef LV_DRIVERS_H
#define LV_DRIVERS_H

#endif /*LV_DRIVERS_H*/
