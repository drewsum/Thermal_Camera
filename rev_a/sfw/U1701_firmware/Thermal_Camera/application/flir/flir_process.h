/*******************************************************************************
  FLIR Lepton Frame Processing

  File Name:
    flir_process.h

  Summary:
    Turns a captured RAW14 Lepton frame (160x120 14-bit values) into a visible
    320x240 RGB888 image on GLCD Layer 0: software AGC (contrast stretch), a
    color-palette lookup, and a 2x nearest-neighbor upscale.

  Description:
    The Lepton is configured for RAW14 output (flir_cci.c), so each pixel is a
    linear 14-bit sensor value rather than a pre-colorized one. This module:
      1. Scans the frame for min/max and stretches to 0..255 (AGC), with the
         min/max exponentially smoothed across frames to stop the image
         flickering as the scene's dynamic range wobbles frame to frame.
      2. Maps each 8-bit value through a 256-entry RGB888 palette LUT.
      3. Writes the result 2x-upscaled straight into the Layer 0 frame buffer
         (the display is exactly 2x the sensor in each axis), a row at a time
         through the uncached KSEG1 alias -- the same blit idiom as
         application/image_loader.c.

    The raw frame itself is kept by flir_vospi.c, so a future "temperature at
    pixel" feature can reuse the radiometric values this path throws away.
*******************************************************************************/

#ifndef FLIR_PROCESS_H
#define FLIR_PROCESS_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    FLIR_PALETTE_IRONBOW = 0,   // classic thermal black->purple->red->yellow->white
    FLIR_PALETTE_WHITEHOT,      // linear white-hot
    FLIR_PALETTE_BLACKHOT,      // linear black-hot (inverse of white-hot)
    FLIR_PALETTE_RAINBOW,       // full-spectrum blue(cold)->green->yellow->red->white(hot)
    FLIR_PALETTE_RAINBOW_HC,    // rainbow, high-contrast variant with a violet floor
    FLIR_PALETTE_ARCTIC,        // cool blues/whites, with a hot orange/red accent
    FLIR_PALETTE_LAVA,          // black->purple->magenta->orange->yellow
    FLIR_PALETTE_GLOWBOW,       // black->red->amber->pale yellow->white glow
    FLIR_PALETTE_COUNT
} FLIR_PALETTE;

// Builds the active palette LUT (default ironbow) and resets the AGC state.
// Call once at startup before the first RenderToLayer0().
void FLIRProcess_Initialize(void);

// Selects the color palette used by subsequent renders.
void FLIRProcess_SetPalette(FLIR_PALETTE palette);

// Returns the currently selected palette.
FLIR_PALETTE FLIRProcess_GetPalette(void);

// Human-readable palette name (e.g. "Ironbow", "Rainbow HC"). Used for the
// status print and doubles as the token the USB UART "FLIR Palette:" command
// matches against, so the name set only has to live in one place. Returns
// "?" for an out-of-range value.
const char *FLIRProcess_PaletteName(FLIR_PALETTE palette);

// AGC + palette + 2x upscale of a 160x120 14-bit frame (as produced by
// FLIR_VOSPI_TakeFrame()) into the GLCD Layer 0 RGB888 frame buffer.
void FLIRProcess_RenderToLayer0(const uint16_t *frame);

// Reports the AGC window (smoothed min/max sensor counts) from the last render,
// for the status print. Either pointer may be NULL.
void FLIRProcess_GetAGCWindow(uint16_t *minCount, uint16_t *maxCount);

#ifdef __cplusplus
}
#endif

#endif /* FLIR_PROCESS_H */
