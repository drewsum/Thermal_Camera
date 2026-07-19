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

    Frame buffer: Layer 0 only (this driver doesn't use GLCD's 3-layer
    alpha-blending -- a single opaque layer is all a static/blank buffer
    needs). Placed at GLCD_FRAMEBUFFER_BASE_ADDRESS, the DDR2 uncached
    (KSEG1) alias (core/ddr2.h) -- core/ddr2.h has no partition scheme today
    (the whole 32MB is unclaimed), so this is this driver's own reservation.
    RGB888 (3 bytes/pixel), matching the panel's native 24-bit depth.
    GLCD_Initialize() clears it to 0 (black, matching this panel's
    Normally-Black mode) -- filling it with actual image data is a
    deliberately separate, later step.

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

// Prints GLCD Controller settings (PMD gating state, LCDEN, resolution,
// timing registers, clock divider + derived GCLK frequency, Layer 0 color
// mode/size/base address, polarity bits). Backs the "Peripheral Status?
// GLCD" USB UART command.
void GLCD_PrintStatus(void);

#ifdef __cplusplus
}
#endif

#endif /* GLCD_H */
