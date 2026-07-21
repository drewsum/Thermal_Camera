/*******************************************************************************
  Graphics LCD (GLCD) Controller Driver

  File Name:
    glcd.h

  Summary:
    Register-level driver for the PIC32MZ2064DAR176's on-die Graphics LCD
    Controller (GLCDMODE..GLCDSTAT, base address 0xBF8EA000, PIC32 Family
    Reference Manual Section 54, DS60001379).

  Description:
    This is the lowest layer of the graphics stack (glcd.c -> panel-specific
    device_driver, mirroring sdhc.c -> sd_card.c). It knows the GLCD
    Controller's own registers and Layer 0's timing/framebuffer setup; it
    knows nothing about a specific panel's timing numbers or reset sequence
    -- that's glcd/device_driver/glt035320240is1.h.

    Clock: GCLK (the pixel clock) is sourced from REFCLKO5
    (core/device_control.c's REFCLK5Initialize(), already run unconditionally
    by clockInitialize() before this driver's GLCD_Initialize() is called),
    further divided by GLCDCLKCON.CLKDIV. This is confirmed by the
    PIC32MZ-DA family *device* datasheet (DS60001565, Register 36-2) --
    unlike the generic PIC32 Family Reference Manual, which only says
    "PLL_CLOCK" -- so this is not a source shared with any other peripheral's
    PLL. REFCLKO5 must run at SYSCLK undivided (200MHz), with all division
    done in GLCDCLKCON.CLKDIV -- see REFCLK5Initialize()'s comment for the
    bring-up evidence behind that (GLCD register writes bus-fault with a
    slow REFCLKO5), and GLCD_Initialize() below for the CLKDIV choice.

    Frame buffers: two of GLCD's three hardware layers are used.
      - Layer 0 -- the opaque background: RGB888 (3 bytes/pixel), matching
        the panel's native 24-bit depth, at GLCD_FRAMEBUFFER_BASE_ADDRESS
        (DDR2 uncached KSEG1 alias, core/ddr2.h). This is the image/thermal
        feed; application/image_loader.c blits into it.
      - Layer 1 -- the GUI overlay: ARGB8888 (4 bytes/pixel), at
        GLCD_OVERLAY_BASE_ADDRESS, composited over Layer 0 by the controller
        using per-pixel alpha (see the GLCD_OVERLAY_* block below). This is
        what application/gui (LVGL) renders into.
    Both reservations are this driver's own -- core/ddr2.h has no central
    partition scheme (the 32MB is carved up by convention across headers).
    GLCD_Initialize() clears Layer 0 to 0 (black, matching this panel's
    Normally-Black mode) and Layer 1 to fully transparent, so at boot the
    panel is black and the overlay is invisible until something draws into
    each -- filling them with real content is a deliberately separate step.

    Pins: GD0-23/HSYNC/VSYNC/GCLK/GEN are dedicated (non-PPS) GLCD Controller
    pins per the device datasheet's pinout table -- no gpio/pin_macros.h
    entries or TRIS/ANSEL configuration needed here; the peripheral takes
    the pins over from GPIO via CFGCON2.GLCDPINEN, exactly like this
    codebase's other dedicated-pin peripherals (SDHC, DDR2).

    ACCESS-SIZE WARNING for anyone adding GLCD register code: the GLCD SFR
    block only supports 32-bit accesses -- sub-word (byte/halfword) loads
    and stores take a Data Bus Error exception, and XC32 shrinks
    GLCDxxxbits.FIELD bitfield accesses to sub-word ops. Use full-word
    register reads/writes only. Full story in glcd.c's file header.
*******************************************************************************/

#ifndef GLCD_H
#define GLCD_H

#include <stdint.h>
#include <stdbool.h>

#include "core/ddr2.h"

#ifdef __cplusplus
extern "C" {
#endif

// Frame buffer geometry/placement -- Layer 0, RGB888, blank (zeroed) by
// GLCD_Initialize(). See the file header comment for why this lives here
// rather than in core/ddr2.h.
#define GLCD_FRAMEBUFFER_WIDTH_PX          320
#define GLCD_FRAMEBUFFER_HEIGHT_PX         240
#define GLCD_FRAMEBUFFER_BYTES_PER_PIXEL   3u
#define GLCD_FRAMEBUFFER_STRIDE_BYTES      (GLCD_FRAMEBUFFER_WIDTH_PX * GLCD_FRAMEBUFFER_BYTES_PER_PIXEL)
#define GLCD_FRAMEBUFFER_SIZE_BYTES        ((uint32_t)GLCD_FRAMEBUFFER_STRIDE_BYTES * GLCD_FRAMEBUFFER_HEIGHT_PX)

// Uncached (KSEG1) alias, per the cache note in core/ddr2.h: code writing
// the frame buffer through this pointer needs no cache writeback/invalidate
// for the GLCD Controller's own DMA (a separate DDR2 bus master) to see it.
#define GLCD_FRAMEBUFFER_BASE_ADDRESS      DDR2_KSEG1_BASE_ADDRESS

// GUI overlay -- Layer 1. A second, transparent, ARGB8888 layer that the GUI
// (application/gui, LVGL) renders into; the GLCD Controller composites it
// over Layer 0 (the image/thermal feed above) during scanout using the
// overlay's own per-pixel alpha, so a menu can be drawn semi-transparently
// on top of a loaded image with no CPU/GPU compositing. GLCD_Initialize()
// brings this layer up (enabled but cleared to fully transparent, so it is
// invisible until something draws into it) alongside Layer 0.
//
// Placement: DDR2 physical offset +5MB, immediately above the image_loader
// decode arena (application/image_loader.h reserves +1MB..+5MB). Same
// partition-by-convention scheme as the Layer 0 buffer and that arena --
// core/ddr2.h still has no central allocator. Accessed through the uncached
// (KSEG1) alias for the same GLCD-DMA coherency reason as Layer 0 (see the
// performance note in application/gui/gui.c about a cached alternative).
#define GLCD_OVERLAY_WIDTH_PX              320
#define GLCD_OVERLAY_HEIGHT_PX             240
#define GLCD_OVERLAY_BYTES_PER_PIXEL       4u   // ARGB8888 (0xAARRGGBB)
#define GLCD_OVERLAY_STRIDE_BYTES          (GLCD_OVERLAY_WIDTH_PX * GLCD_OVERLAY_BYTES_PER_PIXEL)
#define GLCD_OVERLAY_SIZE_BYTES            ((uint32_t)GLCD_OVERLAY_STRIDE_BYTES * GLCD_OVERLAY_HEIGHT_PX)
#define GLCD_OVERLAY_DDR2_OFFSET           0x00500000u
#define GLCD_OVERLAY_BASE_ADDRESS          (DDR2_KSEG1_BASE_ADDRESS + GLCD_OVERLAY_DDR2_OFFSET)

// Brings up the GLCD Controller: assumes PMD6bits.GLCDMD == 0 already
// (application/power_saving.c) and REFCLK5 already configured (by
// clockInitialize() at boot, before this runs). Programs GLCDCLKCON,
// GLCDRES/FPORCH/BLANKING/BPORCH from glt035320240is1.h's panel timing,
// configures Layer 0 for the full-screen RGB888 frame buffer and Layer 1
// for the full-screen ARGB8888 GUI overlay, zeroes Layer 0 (black) and
// clears Layer 1 (transparent), drives the panel reset sequence
// (GLT035320240IS1_ResetPulse()), then sets GLCDMODE.LCDEN=1 last. Returns
// false if PMD6bits.GLCDMD is still set (GLCD_Initialize() called before/
// without PMDInitialize() clearing it).
bool GLCD_Initialize(void);

// Prints GLCD Controller settings (PMD gating state, LCDEN, resolution,
// timing registers, clock divider + derived GCLK frequency, Layer 0 color
// mode/size/base address, polarity bits). Backs the "Peripheral Status?
// GLCD" USB UART command.
void GLCD_PrintStatus(void);

#ifdef __cplusplus
}
#endif

#endif /* GLCD_H */
