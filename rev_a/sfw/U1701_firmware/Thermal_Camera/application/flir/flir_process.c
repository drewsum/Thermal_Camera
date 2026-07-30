/*******************************************************************************
  FLIR Lepton Frame Processing

  File Name:
    flir_process.c

  Summary:
    RAW14 -> AGC -> palette -> 2x upscale into GLCD Layer 0. See flir_process.h.
*******************************************************************************/

#include "application/flir/flir_process.h"
#include "application/flir/flir_vospi.h"
#include "glcd/glcd.h"

#include <string.h>

// Active 256-entry RGB888 palette (3 bytes/entry, R,G,B -- matching Layer 0's
// byte order, see application/image_loader.c).
static uint8_t paletteLUT[256][3];
static FLIR_PALETTE activePalette = FLIR_PALETTE_IRONBOW;

// Smoothed AGC window, in raw 14-bit sensor counts. Seeded on the first frame.
static int32_t smoothMin;
static int32_t smoothMax;
static bool    agcSeeded;

// EMA weight for the AGC window: new = old + (raw - old) / 2^SHIFT. Larger =
// steadier but slower to follow a scene change.
#define FLIR_AGC_SMOOTH_SHIFT   3

// --- Palette generation ----------------------------------------------------

// Ironbow control points (index -> R,G,B), linearly interpolated to fill 256.
static const struct { uint8_t idx, r, g, b; } ironbowPoints[] = {
    {   0,   0,   0,   0 },
    {  32,   0,   0,  80 },
    {  64,  60,   0, 120 },
    {  96, 160,  20, 110 },
    { 128, 220,  50,  50 },
    { 160, 255, 120,   0 },
    { 192, 255, 190,   0 },
    { 224, 255, 240, 120 },
    { 255, 255, 255, 255 },
};

static uint8_t lerp8(uint8_t a, uint8_t b, uint32_t num, uint32_t den)
{
    return (uint8_t)((int32_t)a + (((int32_t)b - (int32_t)a) * (int32_t)num) / (int32_t)den);
}

static void FLIRProcess_BuildIronbow(void)
{
    uint32_t seg;
    uint32_t count = (sizeof(ironbowPoints) / sizeof(ironbowPoints[0])) - 1u;

    for (seg = 0; seg < count; seg++)
    {
        uint32_t lo = ironbowPoints[seg].idx;
        uint32_t hi = ironbowPoints[seg + 1].idx;
        uint32_t span = hi - lo;
        uint32_t i;

        for (i = lo; i <= hi; i++)
        {
            uint32_t n = i - lo;
            paletteLUT[i][0] = lerp8(ironbowPoints[seg].r, ironbowPoints[seg + 1].r, n, span);
            paletteLUT[i][1] = lerp8(ironbowPoints[seg].g, ironbowPoints[seg + 1].g, n, span);
            paletteLUT[i][2] = lerp8(ironbowPoints[seg].b, ironbowPoints[seg + 1].b, n, span);
        }
    }
}

static void FLIRProcess_BuildGrayscale(void)
{
    uint32_t i;
    for (i = 0; i < 256u; i++)
    {
        paletteLUT[i][0] = (uint8_t)i;
        paletteLUT[i][1] = (uint8_t)i;
        paletteLUT[i][2] = (uint8_t)i;
    }
}

static void FLIRProcess_BuildPalette(FLIR_PALETTE palette)
{
    switch (palette)
    {
        case FLIR_PALETTE_GRAYSCALE:
            FLIRProcess_BuildGrayscale();
            break;
        case FLIR_PALETTE_IRONBOW:
        default:
            FLIRProcess_BuildIronbow();
            break;
    }
}

// Which of the two Layer 0 buffers the panel is currently scanning out.
// GLCD_Initialize() starts scanout on buffer A (GLCD_FRAMEBUFFER_BASE_ADDRESS),
// so the first render lands in buffer B.
static bool displayingBufferB;

void FLIRProcess_Initialize(void)
{
    activePalette = FLIR_PALETTE_IRONBOW;
    FLIRProcess_BuildPalette(activePalette);
    agcSeeded = false;
    smoothMin = 0;
    smoothMax = 0xFFFF;   // pixels are 16-bit TLinear centi-Kelvin, not 14-bit
    displayingBufferB = false;
}

void FLIRProcess_SetPalette(FLIR_PALETTE palette)
{
    if (palette >= FLIR_PALETTE_COUNT)
    {
        return;
    }
    activePalette = palette;
    FLIRProcess_BuildPalette(palette);
}

FLIR_PALETTE FLIRProcess_GetPalette(void)
{
    return activePalette;
}

void FLIRProcess_GetAGCWindow(uint16_t *minCount, uint16_t *maxCount)
{
    if (minCount != NULL) *minCount = (uint16_t)smoothMin;
    if (maxCount != NULL) *maxCount = (uint16_t)smoothMax;
}

void FLIRProcess_RenderToLayer0(const uint16_t *frame)
{
    int32_t frameMin = 0xFFFF;   // 16-bit TLinear range, see flir_vospi.c
    int32_t frameMax = 0;
    int32_t range;
    uint32_t scale;
    uint32_t y;
    uint8_t rowRGB[GLCD_FRAMEBUFFER_STRIDE_BYTES];
    uint8_t *dstBase;

    if (frame == NULL)
    {
        return;
    }

    // Render into whichever Layer 0 buffer is off-screen, and flip only when
    // the whole frame is written -- the panel never scans a half-drawn frame
    // (the single-buffer original tore on any scene motion).
    dstBase = (uint8_t *)(displayingBufferB ? GLCD_FRAMEBUFFER_BASE_ADDRESS
                                            : GLCD_FRAMEBUFFER_B_ADDRESS);

    // The AGC window is one frame behind: the render stretches with the
    // EMA-smoothed window from previous frames while gathering this frame's
    // min/max for the update below. That folds what used to be a separate
    // full-frame min/max pass into the render loop, and with the EMA already
    // smoothing over 2^SHIFT frames, one frame of extra lag is invisible.
    // The very first frame has no window yet, so seed it with its own pass.
    if (!agcSeeded)
    {
        uint32_t i;

        for (i = 0; i < FLIR_VOSPI_PIXELS; i++)
        {
            int32_t v = frame[i];
            if (v < frameMin) frameMin = v;
            if (v > frameMax) frameMax = v;
        }

        smoothMin = frameMin;
        smoothMax = frameMax;
        agcSeeded = true;
        frameMin = 0xFFFF;
        frameMax = 0;
    }

    range = smoothMax - smoothMin;
    if (range < 1) range = 1;

    // Fixed-point reciprocal so the stretch is one multiply per pixel instead
    // of a ~30-cycle divide: idx = ((v - min) * (255<<16)/range) >> 16, with
    // (v - min) clamped to [0, range] first so the product stays within
    // 255<<16 (no 32-bit overflow) and the index within 0..255.
    scale = ((uint32_t)255u << 16) / (uint32_t)range;

    // --- Stretch -> palette -> 2x upscale into the back buffer ---
    // Each of the 120 source rows becomes two identical 320px destination
    // rows; each source pixel becomes two horizontal destination pixels.
    for (y = 0; y < FLIR_VOSPI_HEIGHT_PX; y++)
    {
        const uint16_t *srcRow = &frame[y * FLIR_VOSPI_WIDTH_PX];
        uint32_t x;

        for (x = 0; x < FLIR_VOSPI_WIDTH_PX; x++)
        {
            int32_t v = srcRow[x];
            int32_t diff = v - smoothMin;
            uint8_t idx;
            const uint8_t *rgb;
            uint32_t d = x * 6u;   // 2 dest px * 3 bytes

            if (v < frameMin) frameMin = v;
            if (v > frameMax) frameMax = v;

            if (diff < 0) diff = 0;
            else if (diff > range) diff = range;
            idx = (uint8_t)((((uint32_t)diff * scale) + 0x8000u) >> 16);
            rgb = paletteLUT[idx];

            // Two horizontal copies (2x upscale in X).
            rowRGB[d + 0] = rgb[0];
            rowRGB[d + 1] = rgb[1];
            rowRGB[d + 2] = rgb[2];
            rowRGB[d + 3] = rgb[0];
            rowRGB[d + 4] = rgb[1];
            rowRGB[d + 5] = rgb[2];
        }

        // Write the 320px row to both scanlines it covers (2x upscale in Y).
        memcpy(dstBase + ((y * 2u) * GLCD_FRAMEBUFFER_STRIDE_BYTES),
               rowRGB, GLCD_FRAMEBUFFER_STRIDE_BYTES);
        memcpy(dstBase + (((y * 2u) + 1u) * GLCD_FRAMEBUFFER_STRIDE_BYTES),
               rowRGB, GLCD_FRAMEBUFFER_STRIDE_BYTES);
    }

    // Advance the AGC window with this frame's measured span, for next frame.
    smoothMin += (frameMin - smoothMin) >> FLIR_AGC_SMOOTH_SHIFT;
    smoothMax += (frameMax - smoothMax) >> FLIR_AGC_SMOOTH_SHIFT;

    // Flip: the controller latches the new base address at the next frame
    // start, so no vertical-blanking wait is needed -- the just-written
    // buffer goes on screen whole, and the other becomes next frame's target.
    GLCD_SetLayer0BaseAddress(dstBase);
    displayingBufferB = !displayingBufferB;
}
