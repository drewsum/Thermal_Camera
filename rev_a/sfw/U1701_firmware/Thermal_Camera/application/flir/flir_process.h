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

// One index -> R,G,B stop in a palette's gradient (see flir_process.c). A
// palette LUT is these, linearly interpolated to 256 entries; the GUI reuses
// the same stops to paint a scale that always matches the LUT exactly.
typedef struct { uint8_t idx, r, g, b; } FLIR_PaletteControlPoint;

// Must be >= the largest palette's control-point count (flir_process.c
// enforces this at compile time against ironbowPoints, currently the
// largest at 9). Sized here, rather than derived from the private point
// arrays, so callers can size a fixed-length buffer for
// FLIRProcess_GetPalettePoints() without depending on flir_process.c's
// internals.
#define FLIR_PALETTE_MAX_CONTROL_POINTS   9u

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

// Which of the two Layer 0 buffers holds the frame currently on screen (an
// uncached KSEG1 pointer to GLCD_FRAMEBUFFER_SIZE_BYTES of RGB888). Layer 0
// is double buffered and this module owns the flip, so it is the only thing
// that knows which side is live -- application/still_capture.c reads it to
// snapshot exactly the image the user was looking at when they pressed the
// shutter, rather than re-rendering (which would re-run AGC and could differ
// by a shade). Valid before the first render too: it then names buffer A,
// which GLCD_Initialize() zeroed.
const void *FLIRProcess_GetDisplayedLayer0Buffer(void);

// Reports the AGC window (smoothed min/max sensor counts) from the last render,
// for the status print. Either pointer may be NULL.
void FLIRProcess_GetAGCWindow(uint16_t *minCount, uint16_t *maxCount);

// Same AGC window as FLIRProcess_GetAGCWindow(), converted from raw 16-bit
// TLinear centi-Kelvin counts to degrees Celsius -- this is the min/max of
// the temperature range the last render's palette stretch actually covers,
// for on-screen display. Either pointer may be NULL. Returns false (and
// still writes the boot-default window, which converts to nonsense degrees)
// until FLIRProcess_RenderToLayer0() has processed at least one frame --
// callers displaying this should leave a placeholder rather than show it.
bool FLIRProcess_GetAGCWindowCelsius(float *minCelsius, float *maxCelsius);

// Returns the control points (idx -> R,G,B, idx ascending) that build
// `palette`'s 256-entry LUT, for painting a GUI scale that matches it
// exactly. Writes the array pointer to *points (owned by flir_process.c,
// valid for the program's lifetime) and returns the point count, which is
// always <= FLIR_PALETTE_MAX_CONTROL_POINTS. Returns 0 and leaves *points
// untouched for an out-of-range palette.
uint32_t FLIRProcess_GetPalettePoints(FLIR_PALETTE palette, const FLIR_PaletteControlPoint **points);

#ifdef __cplusplus
}
#endif

#endif /* FLIR_PROCESS_H */
