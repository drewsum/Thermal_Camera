/*******************************************************************************
  LVGL Configuration -- Thermal Camera bare-metal GUI

  File Name:
    lv_conf.h

  Summary:
    Project configuration for the vendored LVGL v9.3.0 tree
    (application/gui/lvgl/). This is a minimal override file: LVGL's
    src/lv_conf_internal.h supplies a default for every option not defined
    here, so only the settings this project actually cares about are listed.

  How LVGL finds this file:
    The build (cmake/Thermal_Camera/default/user.cmake) defines
    LV_CONF_INCLUDE_SIMPLE and puts application/gui on the include path, so
    LVGL's `#include "lv_conf.h"` resolves to this file.

  Design notes specific to this port (see application/gui/gui.c for the full
  rationale):
    - Color depth 32: the GUI is GLCD Layer 1, an ARGB8888 overlay the
      controller alpha-blends over Layer 0 (the image/thermal feed). The
      display is configured for LV_COLOR_FORMAT_ARGB8888 in lv_port_disp.c so
      each rendered pixel carries the alpha the GLCD needs.
    - LVGL heap lives in DDR2, not the 128KB internal RAM: LV_MEM_ADR points
      at the DDR2 cached (KSEG0) alias at physical offset +5.5MB, just above
      the Layer 1 overlay framebuffer (glcd.h GLCD_OVERLAY_* at +5MB) and the
      image_loader decode arena (+1..+5MB). CPU-only memory (widgets/styles),
      so the cached alias is fine and fast -- no DMA touches it.
    - Bare metal: LV_USE_OS = LV_OS_NONE. The tick and lv_timer_handler() are
      driven cooperatively from the main loop (see application/gui/gui.c).
    - Software rendering only: LV_USE_DRAW_SW = 1, no GPU draw unit. The
      PIC32MZ-DA 2D GPU is a proprietary Vivante blob and stays disabled
      (application/power_saving.c PMD6.GPUMD); at 320x240 the CPU is plenty.
*******************************************************************************/

#ifndef LV_CONF_H
#define LV_CONF_H

/*====================
   COLOR / MEMORY
 *====================*/

// 32 bits/pixel so the overlay layer can carry per-pixel alpha (ARGB8888).
#define LV_COLOR_DEPTH 32

// LVGL's built-in allocator, backed by a fixed pool in DDR2 rather than the
// ~128KB internal-RAM heap (same reasoning as image_loader.c's DDR2 arena).
#define LV_USE_STDLIB_MALLOC   LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_STRING   LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_SPRINTF  LV_STDLIB_BUILTIN

// 2MB pool at DDR2 physical offset +7MB, through the CACHED (KSEG0) alias
// (0x88000000 + 0x00700000). Sits above the two Layer 1 overlay buffers
// (glcd.h: KSEG1 buffer A at +5MB, buffer B at +6MB, ~300KB each) and the
// image_loader arena (+1..+5MB). Keep this consistent with the DDR2 partition
// map documented in glcd/glcd.h and application/image_loader.h -- there is no
// central allocator. LV_MEM_SIZE is mirrored as GUI_LVGL_HEAP_SIZE_BYTES in
// application/gui/gui.h (for the "Storage Usage?" report) -- keep them equal.
#define LV_MEM_SIZE  (2 * 1024U * 1024U)
#define LV_MEM_ADR   0x88700000UL

/*====================
   HAL / OS
 *====================*/

#define LV_DEF_REFR_PERIOD  33   // ms; ~30 FPS refresh cadence
#define LV_DPI_DEF          130
#define LV_USE_OS           LV_OS_NONE

/*====================
   RENDERING
 *====================*/

#define LV_USE_DRAW_SW  1   // software renderer only; no GPU draw unit

/*====================
   LOGGING / ASSERTS
 *====================*/

// Route LVGL logs to printf(), which this project sends to the USB-UART
// console (usb_uart/). Warnings and errors only, to keep the port quiet.
#define LV_USE_LOG    1
#define LV_LOG_PRINTF 1
#define LV_LOG_LEVEL  LV_LOG_LEVEL_WARN

/*====================
   FONTS
 *====================*/

// Only the sizes vendored under application/gui/lvgl/src/font are enabled;
// the rest of LVGL's font glyph files were trimmed from the vendored tree.
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_DEFAULT &lv_font_montserrat_14

#endif /* LV_CONF_H */
