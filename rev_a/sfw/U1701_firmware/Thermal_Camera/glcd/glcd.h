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

    Layers: this driver uses two of the GLCD's three layers.

      Layer 0 is the opaque background: GLCD_FRAMEBUFFER_BASE_ADDRESS, RGB888
      (3 bytes/pixel, matching the panel's native 24-bit depth), written by
      application/image_loader.c's PNG loader. GLCD_Initialize() clears it to
      0 (black, matching this panel's Normally-Black mode).

      Layer 1 is a transparent GUI overlay: ARGB8888 (4 bytes/pixel), which
      the controller composites over Layer 0 per-pixel in hardware, and which
      LVGL renders into (gui/lv_port_disp.c). It is brought up separately by
      GLCD_OverlayInitialize() -- a board that never starts the GUI simply
      never enables Layer 1. It is double buffered: the GUI renders a whole
      frame into the buffer that is NOT being scanned out, then flips
      GLCDL1BADDR between them during vertical blanking, so the panel never
      shows a half-drawn frame.

    All three buffers live in DDR2 through the uncached (KSEG1) alias
    (core/ddr2.h) -- core/ddr2.h has no allocator, so each is this driver's
    own reservation, documented in the map in gui/gui.h.

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

// Layer 1 (GUI overlay) geometry/placement -- same 320x240 as Layer 0, but
// ARGB8888 so the GLCD Controller can alpha-blend it over Layer 0 per pixel.
// Two full-screen buffers (see the double-buffering note in the file header),
// both through the uncached KSEG1 alias for the same coherency reason as
// Layer 0. Placed clear of Layer 0's ~230KB at DDR2 offset 0; the full DDR2
// partition map lives in gui/gui.h.
#define GLCD_OVERLAY_WIDTH_PX              320
#define GLCD_OVERLAY_HEIGHT_PX             240
#define GLCD_OVERLAY_BYTES_PER_PIXEL       4u
#define GLCD_OVERLAY_STRIDE_BYTES          (GLCD_OVERLAY_WIDTH_PX * GLCD_OVERLAY_BYTES_PER_PIXEL)
#define GLCD_OVERLAY_SIZE_BYTES            ((uint32_t)GLCD_OVERLAY_STRIDE_BYTES * GLCD_OVERLAY_HEIGHT_PX)

#define GLCD_OVERLAY_BUFFER_A_ADDRESS      (DDR2_KSEG1_BASE_ADDRESS + 0x00100000u)
#define GLCD_OVERLAY_BUFFER_B_ADDRESS      (DDR2_KSEG1_BASE_ADDRESS + 0x00200000u)

// Layer 2 (still-image loader) geometry/placement -- full-screen 320x240
// RGB888, same as Layer 0. This is the top-most layer, so the GLCD composites
// it ABOVE both the GUI (Layer 1) and the thermal video (Layer 0): while
// enabled and opaque it hides everything beneath it, which is exactly the
// intent -- it is disabled by default and only turned on while an image is
// being shown (application/image_loader.c). Placed at DDR2 +7MB, clear of the
// reserved 0..7MB region; the full partition map lives in gui/gui.h.
#define GLCD_LAYER2_WIDTH_PX               320
#define GLCD_LAYER2_HEIGHT_PX              240
#define GLCD_LAYER2_BYTES_PER_PIXEL        3u
#define GLCD_LAYER2_STRIDE_BYTES           (GLCD_LAYER2_WIDTH_PX * GLCD_LAYER2_BYTES_PER_PIXEL)
#define GLCD_LAYER2_SIZE_BYTES             ((uint32_t)GLCD_LAYER2_STRIDE_BYTES * GLCD_LAYER2_HEIGHT_PX)
#define GLCD_LAYER2_BASE_ADDRESS           (DDR2_KSEG1_BASE_ADDRESS + 0x00700000u)

// Brings up the GLCD Controller: assumes PMD6bits.GLCDMD == 0 already
// (application/power_saving.c) and REFCLK5 already configured (by
// clockInitialize() at boot, before this runs). Programs GLCDCLKCON,
// GLCDRES/FPORCH/BLANKING/BPORCH from glt035320240is1.h's panel timing,
// configures Layer 0 for the full-screen RGB888 frame buffer, zeroes the
// frame buffer, drives the panel reset sequence
// (GLT035320240IS1_ResetPulse()), then sets GLCDMODE.LCDEN=1 last. Returns
// false if PMD6bits.GLCDMD is still set (GLCD_Initialize() called before/
// without PMDInitialize() clearing it).
bool GLCD_Initialize(void);

// Brings up Layer 1, the transparent ARGB8888 GUI overlay: clears both
// overlay buffers to fully transparent, programs Layer 1's geometry/blend/
// color mode, and points it at buffer B -- so whichever buffer the GUI
// renders into first (buffer A, per gui/lv_port_disp.c) is off-screen and
// its first frame appears only when it is flipped in. Separate from
// GLCD_Initialize() so the overlay only exists if a GUI is actually running.
// Returns false if GLCD_Initialize() hasn't run (LCDEN still clear).
bool GLCD_OverlayInitialize(void);

// Points Layer 1's DMA at `buffer` (a KSEG0/KSEG1 pointer to one of the two
// overlay buffers; converted to the physical address GLCDL1BADDR wants).
// The controller latches the new base at the next frame start, so call this
// from inside vertical blanking -- see GLCD_WaitOverlayVSync().
void GLCD_SetOverlayBaseAddress(const void *buffer);

// Blocks until the panel is in vertical blanking (GLCDSTAT.VSYNC), which is
// the window in which an overlay buffer flip is invisible. Bounded by a CP0
// Count deadline of roughly two frame periods, so a panel that never asserts
// VSYNC (or a GLCD that was never enabled) stalls the caller briefly rather
// than hanging the main loop forever. Returns false on that timeout, which
// the caller should latch -- the flip is then still safe to perform, it just
// may tear.
bool GLCD_WaitOverlayVSync(void);

// Brings up Layer 2, the top-most full-screen RGB888 still-image layer, with
// the layer DISABLED (LAYEREN=0) so it is invisible until an image is loaded.
// Programs its geometry/blend/color mode and clears its buffer to black.
// Returns false if GLCD_Initialize() hasn't run (LCDEN still clear). Separate
// from GLCD_Initialize() so the layer only exists once something needs it.
bool GLCD_Layer2Initialize(void);

// Enables or disables Layer 2 (its LAYEREN bit). Enable after writing an image
// into GLCD_LAYER2_BASE_ADDRESS to show it over the video/GUI; disable to
// reveal them again. Full-word read-modify-write per the GLCD access rule.
void GLCD_Layer2SetEnabled(bool enabled);

// Returns whether Layer 2 is currently enabled (LAYEREN set).
bool GLCD_Layer2IsEnabled(void);

// Prints GLCD Controller settings (PMD gating state, LCDEN, resolution,
// timing registers, clock divider + derived GCLK frequency, Layer 0 and
// Layer 1 color mode/size/base address, polarity bits). Backs the
// "Peripheral Status? GLCD" USB UART command.
void GLCD_PrintStatus(void);

#ifdef __cplusplus
}
#endif

#endif /* GLCD_H */
