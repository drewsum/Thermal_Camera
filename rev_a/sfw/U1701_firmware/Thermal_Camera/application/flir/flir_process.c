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

void FLIRProcess_Initialize(void)
{
    activePalette = FLIR_PALETTE_IRONBOW;
    FLIRProcess_BuildPalette(activePalette);
    agcSeeded = false;
    smoothMin = 0;
    smoothMax = 0x3FFF;
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
    uint32_t i;
    int32_t rawMin = 0x3FFF;
    int32_t rawMax = 0;
    int32_t range;
    uint32_t y;
    uint8_t rowRGB[GLCD_FRAMEBUFFER_STRIDE_BYTES];
    uint8_t *dstBase = (uint8_t *)GLCD_FRAMEBUFFER_BASE_ADDRESS;

    if (frame == NULL)
    {
        return;
    }

    // --- AGC pass 1: min/max over the frame ---
    for (i = 0; i < FLIR_VOSPI_PIXELS; i++)
    {
        int32_t v = frame[i];
        if (v < rawMin) rawMin = v;
        if (v > rawMax) rawMax = v;
    }

    if (!agcSeeded)
    {
        smoothMin = rawMin;
        smoothMax = rawMax;
        agcSeeded = true;
    }
    else
    {
        smoothMin += (rawMin - smoothMin) >> FLIR_AGC_SMOOTH_SHIFT;
        smoothMax += (rawMax - smoothMax) >> FLIR_AGC_SMOOTH_SHIFT;
    }

    range = smoothMax - smoothMin;
    if (range < 1) range = 1;

    // --- Pass 2: stretch -> palette -> 2x upscale into Layer 0 ---
    // Each of the 120 source rows becomes two identical 320px destination
    // rows; each source pixel becomes two horizontal destination pixels.
    for (y = 0; y < FLIR_VOSPI_HEIGHT_PX; y++)
    {
        const uint16_t *srcRow = &frame[y * FLIR_VOSPI_WIDTH_PX];
        uint32_t x;

        for (x = 0; x < FLIR_VOSPI_WIDTH_PX; x++)
        {
            int32_t stretched = (((int32_t)srcRow[x] - smoothMin) * 255) / range;
            uint8_t idx;
            const uint8_t *rgb;
            uint32_t d = x * 6u;   // 2 dest px * 3 bytes

            if (stretched < 0) stretched = 0;
            else if (stretched > 255) stretched = 255;
            idx = (uint8_t)stretched;
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
}
