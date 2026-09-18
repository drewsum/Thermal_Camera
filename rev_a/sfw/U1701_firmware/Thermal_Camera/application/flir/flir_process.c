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

// A palette is a handful of index -> R,G,B control points, linearly
// interpolated to fill all 256 LUT entries (see FLIRProcess_BuildFromPoints).
// The Lepton is run in RAW14/TLinear mode (flir_cci.c) rather than the
// sensor's own AGC/colorizer, so all of these are software approximations
// of FLIR's named palettes rather than a copy of proprietary on-camera LUTs.
// FLIR_PaletteControlPoint itself is public (flir_process.h) -- the GUI
// reuses it to paint a scale from these same stops.
#define FLIR_PALETTE_POINT_COUNT(points)   ((uint32_t)(sizeof(points) / sizeof((points)[0])))

static const FLIR_PaletteControlPoint ironbowPoints[] = {
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

static const FLIR_PaletteControlPoint whiteHotPoints[] = {
    {   0,   0,   0,   0 },
    { 255, 255, 255, 255 },
};

static const FLIR_PaletteControlPoint blackHotPoints[] = {
    {   0, 255, 255, 255 },
    { 255,   0,   0,   0 },
};

static const FLIR_PaletteControlPoint rainbowPoints[] = {
    {   0,   0,   0, 140 },
    {  40,   0,   0, 255 },
    {  80,   0, 255, 255 },
    { 120,   0, 255,   0 },
    { 160, 255, 255,   0 },
    { 200, 255, 128,   0 },
    { 230, 255,   0,   0 },
    { 255, 255, 255, 255 },
};

static const FLIR_PaletteControlPoint rainbowHCPoints[] = {
    {   0,   0,   0,   0 },
    {  20,  40,   0,  80 },
    {  60,   0,   0, 255 },
    { 100,   0, 200, 200 },
    { 140,   0, 255,   0 },
    { 180, 255, 255,   0 },
    { 220, 255,   0,   0 },
    { 255, 255, 255, 255 },
};

static const FLIR_PaletteControlPoint arcticPoints[] = {
    {   0,   0,  20,  60 },
    {  60,   0,  80, 160 },
    { 120,  60, 160, 220 },
    { 180, 180, 220, 255 },
    { 220, 255, 255, 255 },
    { 255, 255,  60,   0 },
};

static const FLIR_PaletteControlPoint lavaPoints[] = {
    {   0,   0,   0,   0 },
    {  60,  40,   0,  80 },
    { 110, 120,   0, 120 },
    { 150, 200,   0,  80 },
    { 190, 255,  60,   0 },
    { 230, 255, 160,   0 },
    { 255, 255, 255,  80 },
};

static const FLIR_PaletteControlPoint glowbowPoints[] = {
    {   0,   0,   0,   0 },
    {  50,  40,   0,  20 },
    { 100, 120,  20,  20 },
    { 150, 220,  80,   0 },
    { 200, 255, 170,  40 },
    { 255, 255, 255, 200 },
};

// ironbowPoints is the largest control-point array (9); catch a future
// palette outgrowing FLIR_PALETTE_MAX_CONTROL_POINTS (flir_process.h) at
// build time instead of silently truncating the GUI's gradient scale.
#if FLIR_PALETTE_MAX_CONTROL_POINTS < 9
#error "FLIR_PALETTE_MAX_CONTROL_POINTS must cover ironbowPoints (9 stops)"
#endif

static uint8_t lerp8(uint8_t a, uint8_t b, uint32_t num, uint32_t den)
{
    return (uint8_t)((int32_t)a + (((int32_t)b - (int32_t)a) * (int32_t)num) / (int32_t)den);
}

static void FLIRProcess_BuildFromPoints(const FLIR_PaletteControlPoint *points, uint32_t count)
{
    uint32_t seg;

    for (seg = 0; seg < count - 1u; seg++)
    {
        uint32_t lo = points[seg].idx;
        uint32_t hi = points[seg + 1].idx;
        uint32_t span = hi - lo;
        uint32_t i;

        for (i = lo; i <= hi; i++)
        {
            uint32_t n = i - lo;
            paletteLUT[i][0] = lerp8(points[seg].r, points[seg + 1].r, n, span);
            paletteLUT[i][1] = lerp8(points[seg].g, points[seg + 1].g, n, span);
            paletteLUT[i][2] = lerp8(points[seg].b, points[seg + 1].b, n, span);
        }
    }
}

static void FLIRProcess_BuildPalette(FLIR_PALETTE palette)
{
    switch (palette)
    {
        case FLIR_PALETTE_WHITEHOT:
            FLIRProcess_BuildFromPoints(whiteHotPoints, FLIR_PALETTE_POINT_COUNT(whiteHotPoints));
            break;
        case FLIR_PALETTE_BLACKHOT:
            FLIRProcess_BuildFromPoints(blackHotPoints, FLIR_PALETTE_POINT_COUNT(blackHotPoints));
            break;
        case FLIR_PALETTE_RAINBOW:
            FLIRProcess_BuildFromPoints(rainbowPoints, FLIR_PALETTE_POINT_COUNT(rainbowPoints));
            break;
        case FLIR_PALETTE_RAINBOW_HC:
            FLIRProcess_BuildFromPoints(rainbowHCPoints, FLIR_PALETTE_POINT_COUNT(rainbowHCPoints));
            break;
        case FLIR_PALETTE_ARCTIC:
            FLIRProcess_BuildFromPoints(arcticPoints, FLIR_PALETTE_POINT_COUNT(arcticPoints));
            break;
        case FLIR_PALETTE_LAVA:
            FLIRProcess_BuildFromPoints(lavaPoints, FLIR_PALETTE_POINT_COUNT(lavaPoints));
            break;
        case FLIR_PALETTE_GLOWBOW:
            FLIRProcess_BuildFromPoints(glowbowPoints, FLIR_PALETTE_POINT_COUNT(glowbowPoints));
            break;
        case FLIR_PALETTE_IRONBOW:
        default:
            FLIRProcess_BuildFromPoints(ironbowPoints, FLIR_PALETTE_POINT_COUNT(ironbowPoints));
            break;
    }
}

const char *FLIRProcess_PaletteName(FLIR_PALETTE palette)
{
    switch (palette)
    {
        case FLIR_PALETTE_IRONBOW:     return "Ironbow";
        case FLIR_PALETTE_WHITEHOT:    return "White Hot";
        case FLIR_PALETTE_BLACKHOT:    return "Black Hot";
        case FLIR_PALETTE_RAINBOW:     return "Rainbow";
        case FLIR_PALETTE_RAINBOW_HC:  return "Rainbow HC";
        case FLIR_PALETTE_ARCTIC:      return "Arctic";
        case FLIR_PALETTE_LAVA:        return "Lava";
        case FLIR_PALETTE_GLOWBOW:     return "Glowbow";
        default:                       return "?";
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

// Same formula as the AGC window print in flir.c: pixels are raw 16-bit
// TLinear centi-Kelvin counts (flir_cci.c runs the Lepton in RAW14/TLinear
// mode), so Celsius = counts/100 - 273.15.
#define FLIR_PROCESS_KELVIN_OFFSET_C   273.15f

bool FLIRProcess_GetAGCWindowCelsius(float *minCelsius, float *maxCelsius)
{
    if (minCelsius != NULL) *minCelsius = ((float)smoothMin / 100.0f) - FLIR_PROCESS_KELVIN_OFFSET_C;
    if (maxCelsius != NULL) *maxCelsius = ((float)smoothMax / 100.0f) - FLIR_PROCESS_KELVIN_OFFSET_C;
    return agcSeeded;
}

uint32_t FLIRProcess_GetPalettePoints(FLIR_PALETTE palette, const FLIR_PaletteControlPoint **points)
{
    switch (palette)
    {
        case FLIR_PALETTE_WHITEHOT:
            *points = whiteHotPoints;
            return FLIR_PALETTE_POINT_COUNT(whiteHotPoints);
        case FLIR_PALETTE_BLACKHOT:
            *points = blackHotPoints;
            return FLIR_PALETTE_POINT_COUNT(blackHotPoints);
        case FLIR_PALETTE_RAINBOW:
            *points = rainbowPoints;
            return FLIR_PALETTE_POINT_COUNT(rainbowPoints);
        case FLIR_PALETTE_RAINBOW_HC:
            *points = rainbowHCPoints;
            return FLIR_PALETTE_POINT_COUNT(rainbowHCPoints);
        case FLIR_PALETTE_ARCTIC:
            *points = arcticPoints;
            return FLIR_PALETTE_POINT_COUNT(arcticPoints);
        case FLIR_PALETTE_LAVA:
            *points = lavaPoints;
            return FLIR_PALETTE_POINT_COUNT(lavaPoints);
        case FLIR_PALETTE_GLOWBOW:
            *points = glowbowPoints;
            return FLIR_PALETTE_POINT_COUNT(glowbowPoints);
        case FLIR_PALETTE_IRONBOW:
            *points = ironbowPoints;
            return FLIR_PALETTE_POINT_COUNT(ironbowPoints);
        default:
            return 0;
    }
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

const void *FLIRProcess_GetDisplayedLayer0Buffer(void)
{
    // displayingBufferB is toggled at the END of the render above, right
    // after the flip, so it always names the buffer holding the last frame
    // written -- the one on screen.
    return (const void *)(displayingBufferB ? GLCD_FRAMEBUFFER_B_ADDRESS
                                            : GLCD_FRAMEBUFFER_BASE_ADDRESS);
}

const uint8_t (*FLIRProcess_GetPaletteLUT(void))[3]
{
    return (const uint8_t (*)[3])paletteLUT;
}
