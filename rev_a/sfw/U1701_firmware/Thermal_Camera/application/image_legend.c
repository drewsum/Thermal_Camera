/*******************************************************************************
  Thermal Legend Renderer

  File Name:
    image_legend.c

  Summary:
    Paints the home screen's palette legend into an RGB888 frame buffer. See
    image_legend.h for why this exists instead of copying Layer 1.
*******************************************************************************/

#include <stdio.h>
#include <string.h>

#include "application/image_legend.h"

#include "application/flir/flir_process.h"
#include "gui/lvgl/lvgl.h"

// The alpha the home screen's widgets are drawn at, restated as 0..255 for
// the blends below. SCREEN_BAR_OPACITY is an LV_OPA_* enumerator, which is
// already that scale -- named here so the arithmetic reads as alpha rather
// than as a magic constant.
#define IMAGE_LEGEND_FILL_ALPHA     ((uint32_t)SCREEN_BAR_OPACITY)

// The scale's 1px outline, matching screen_home.c's border_opa of LV_OPA_80.
#define IMAGE_LEGEND_BORDER_ALPHA   ((uint32_t)LV_OPA_80)

// Widest glyph box this has to rasterize into. Montserrat 14's largest cell
// is well inside this; anything bigger is skipped rather than allowed to run
// off the end of the scratch buffer (see ImageLegendDrawGlyph()).
#define IMAGE_LEGEND_GLYPH_MAX_W_PX  32
#define IMAGE_LEGEND_GLYPH_MAX_H_PX  32

// Where the drawing lands. Held in one struct so the clip test below has a
// single thing to check against, rather than every helper carrying the
// buffer, its geometry and its stride separately.
typedef struct
{
    uint8_t *base;
    int32_t width;
    int32_t height;
} IMAGE_LEGEND_TARGET;

// One glyph, converted to A8 by lv_font_get_glyph_bitmap(). Static rather
// than on the stack because the main-loop stack is shared with the SD and
// USB paths this runs alongside, and 1KB of it is not worth borrowing.
// Aligned like any other draw buffer, or lv_draw_buf_init() logs about it.
static uint8_t glyphScratch[IMAGE_LEGEND_GLYPH_MAX_W_PX * IMAGE_LEGEND_GLYPH_MAX_H_PX]
        __attribute__((aligned(LV_DRAW_BUF_ALIGN)));

// Blends `src` over the pixel at (x, y) with `alpha` (0 = leave alone,
// 255 = replace). Silently drops anything outside the frame OR outside the
// legend's declared bounding box -- the box is what still_capture.c restores
// when the legend is switched back off, so a pixel painted outside it could
// never be taken away again.
static void ImageLegendBlendPixel(const IMAGE_LEGEND_TARGET *target, int32_t x, int32_t y,
        const uint8_t *src, uint32_t alpha)
{
    uint8_t *pixel;
    uint32_t channel;

    if ((x < IMAGE_LEGEND_BOX_X_PX) || (x >= (IMAGE_LEGEND_BOX_X_PX + IMAGE_LEGEND_BOX_WIDTH_PX))) return;
    if ((y < IMAGE_LEGEND_BOX_Y_PX) || (y >= (IMAGE_LEGEND_BOX_Y_PX + IMAGE_LEGEND_BOX_HEIGHT_PX))) return;
    if ((x < 0) || (x >= target->width) || (y < 0) || (y >= target->height)) return;

    if (alpha == 0) return;

    pixel = target->base + ((uint32_t)y * (uint32_t)target->width * 3u) + ((uint32_t)x * 3u);

    if (alpha >= LV_OPA_COVER)
    {
        pixel[0] = src[0];
        pixel[1] = src[1];
        pixel[2] = src[2];
        return;
    }

    for (channel = 0; channel < 3u; channel++)
    {
        uint32_t mixed = ((uint32_t)src[channel] * alpha) +
                         ((uint32_t)pixel[channel] * (255u - alpha));

        // Rounded to nearest rather than truncated: over the scale's 112
        // rows, always landing a count dark turns the gradient's smallest
        // steps into visible banding
        pixel[channel] = (uint8_t)((mixed + 127u) / 255u);
    }
}

static void ImageLegendFillRect(const IMAGE_LEGEND_TARGET *target, int32_t x, int32_t y,
        int32_t w, int32_t h, const uint8_t *color, uint32_t alpha)
{
    int32_t row;
    int32_t column;

    for (row = 0; row < h; row++)
    {
        for (column = 0; column < w; column++)
        {
            ImageLegendBlendPixel(target, x + column, y + row, color, alpha);
        }
    }
}

// --- Text ------------------------------------------------------------------
// The labels are drawn glyph by glyph out of the same LVGL font the home
// screen's chips use, so the baked-in text is the one on the panel rather
// than an approximation of it. Only the parts of LVGL's label pipeline that
// matter here are reproduced: no wrapping, no letter spacing (the theme
// leaves it at 0), no bidi, no recoloring.

// Advance width of `text`, in pixels, with kerning -- LVGL's own
// lv_text_get_width() minus the features listed above.
static int32_t ImageLegendTextWidth(const char *text)
{
    int32_t width = 0;
    uint32_t i;

    for (i = 0; text[i] != '\0'; i++)
    {
        // The legend's text is ASCII (digits, '-', '.', ' ', 'C'), so a byte
        // IS a code point here -- no UTF-8 decoding needed
        width += (int32_t)lv_font_get_glyph_width(IMAGE_LEGEND_FONT,
                (uint32_t)(uint8_t)text[i], (uint32_t)(uint8_t)text[i + 1u]);
    }

    return width;
}

// Rasterizes one glyph at (x, y) = the pen position, i.e. the top-left of
// the text line, exactly as lv_draw_label.c places it.
static void ImageLegendDrawGlyph(const IMAGE_LEGEND_TARGET *target, int32_t x, int32_t y,
        uint32_t letter, uint32_t letterNext, const uint8_t *color, int32_t *advance)
{
    lv_font_glyph_dsc_t glyph;
    lv_draw_buf_t drawBuf;
    const lv_font_t *font = IMAGE_LEGEND_FONT;
    const uint8_t *alphaMap;
    uint32_t stride;
    int32_t glyphX;
    int32_t glyphY;
    int32_t row;
    int32_t column;

    *advance = 0;

    if (!lv_font_get_glyph_dsc(font, &glyph, letter, letterNext)) return;

    *advance = (int32_t)glyph.adv_w;

    // Spaces have no cell to draw, and an oversized one would overrun the
    // scratch buffer -- neither is an error, both just advance the pen
    if ((glyph.box_w == 0) || (glyph.box_h == 0) ||
        (glyph.box_w > IMAGE_LEGEND_GLYPH_MAX_W_PX) || (glyph.box_h > IMAGE_LEGEND_GLYPH_MAX_H_PX))
    {
        lv_font_glyph_release_draw_data(&glyph);
        return;
    }

    // The built-in Montserrat fonts store 4bpp cells and have no static
    // bitmap, so the only way to their coverage values is this call, which
    // expands the cell to A8 into the draw buffer handed to it. Describing
    // the scratch above with lv_draw_buf_init() rather than filling the
    // struct by hand is what keeps that free of any allocation: it is the
    // one call that also installs the default handlers, and the converter
    // ends by flushing the cache THROUGH them.
    if (lv_draw_buf_init(&drawBuf, glyph.box_w, glyph.box_h, LV_COLOR_FORMAT_A8, 0,
            glyphScratch, sizeof(glyphScratch)) != LV_RESULT_OK)
    {
        lv_font_glyph_release_draw_data(&glyph);
        return;
    }

    // Returns the DRAW BUFFER, not the pixels -- the coverage values are in
    // the buffer it was given, which is the scratch above
    if (lv_font_get_glyph_bitmap(&glyph, &drawBuf) == NULL)
    {
        lv_font_glyph_release_draw_data(&glyph);
        return;
    }

    alphaMap = drawBuf.data;

    // Same stride the converter wrote with, and the same placement
    // lv_draw_letter() computes -- the font's box offsets are relative to
    // the baseline, which sits (line_height - base_line) below the pen
    stride = drawBuf.header.stride;
    glyphX = x + glyph.ofs_x;
    glyphY = y + (font->line_height - font->base_line) - (int32_t)glyph.box_h - glyph.ofs_y;

    for (row = 0; row < (int32_t)glyph.box_h; row++)
    {
        const uint8_t *alphaRow = &alphaMap[(uint32_t)row * stride];

        for (column = 0; column < (int32_t)glyph.box_w; column++)
        {
            ImageLegendBlendPixel(target, glyphX + column, glyphY + row, color, alphaRow[column]);
        }
    }

    lv_font_glyph_release_draw_data(&glyph);
}

static void ImageLegendDrawText(const IMAGE_LEGEND_TARGET *target, int32_t x, int32_t y,
        const char *text, const uint8_t *color)
{
    uint32_t i;

    for (i = 0; text[i] != '\0'; i++)
    {
        int32_t advance;

        ImageLegendDrawGlyph(target, x, y, (uint32_t)(uint8_t)text[i],
                (uint32_t)(uint8_t)text[i + 1u], color, &advance);

        x += advance;
    }
}

// One of the two temperature chips: a translucent black backing rectangle
// with white text on it, horizontally centered on the scale strip. `topY` is
// the top of the chip, which is what the caller has (the scale's edges plus
// the gap), not the top of the text.
static void ImageLegendDrawChipLabel(const IMAGE_LEGEND_TARGET *target, int32_t topY,
        const char *text)
{
    static const uint8_t black[3] = { 0x00, 0x00, 0x00 };
    static const uint8_t white[3] = { 0xFF, 0xFF, 0xFF };

    int32_t textWidth = ImageLegendTextWidth(text);
    int32_t chipWidth = textWidth + (2 * IMAGE_LEGEND_LABEL_PAD_HOR_PX);
    int32_t chipHeight = lv_font_get_line_height(IMAGE_LEGEND_FONT) +
                         (2 * IMAGE_LEGEND_LABEL_PAD_VER_PX);

    int32_t chipX = ImageLegend_ChipX(chipWidth);

    ImageLegendFillRect(target, chipX, topY, chipWidth, chipHeight, black, IMAGE_LEGEND_FILL_ALPHA);

    ImageLegendDrawText(target, chipX + IMAGE_LEGEND_LABEL_PAD_HOR_PX,
            topY + IMAGE_LEGEND_LABEL_PAD_VER_PX, text, white);
}

int32_t ImageLegend_ChipX(int32_t chipWidth)
{
    // Centered on the strip, by the same expression -- integer division and
    // all -- that LVGL's LV_ALIGN_OUT_*_MID uses
    int32_t x = IMAGE_LEGEND_SCALE_X_PX + ((IMAGE_LEGEND_SCALE_WIDTH_PX - chipWidth) / 2);

    // ...then onto the panel. A chip is around 56px against the strip's 16,
    // so this bites on every real temperature and the chips end up flush
    // with the left edge rather than centered -- which is the only place
    // they can be without losing their first digit.
    if (x < 0) x = 0;

    return x;
}

// The gradient strip: one LUT entry per row, hot at the top to agree with
// the max label above it, then the outline over the edges.
static void ImageLegendDrawScale(const IMAGE_LEGEND_TARGET *target)
{
    static const uint8_t black[3] = { 0x00, 0x00, 0x00 };

    const uint8_t (*lut)[3] = FLIRProcess_GetPaletteLUT();
    const int32_t lastRow = IMAGE_LEGEND_SCALE_HEIGHT_PX - 1;
    int32_t row;

    for (row = 0; row < IMAGE_LEGEND_SCALE_HEIGHT_PX; row++)
    {
        // Row 0 is index 255 (hot) and the last row is index 0 (cold), with
        // the rows between spread evenly and rounded to nearest
        uint32_t index = 255u - (uint32_t)((((int32_t)row * 255) + (lastRow / 2)) / lastRow);

        ImageLegendFillRect(target, IMAGE_LEGEND_SCALE_X_PX, IMAGE_LEGEND_SCALE_TOP_Y_PX + row,
                IMAGE_LEGEND_SCALE_WIDTH_PX, 1, lut[index], IMAGE_LEGEND_FILL_ALPHA);
    }

    // 1px outline, drawn over the fill like LVGL's inside-the-bounds border
    ImageLegendFillRect(target, IMAGE_LEGEND_SCALE_X_PX, IMAGE_LEGEND_SCALE_TOP_Y_PX,
            IMAGE_LEGEND_SCALE_WIDTH_PX, 1, black, IMAGE_LEGEND_BORDER_ALPHA);
    ImageLegendFillRect(target, IMAGE_LEGEND_SCALE_X_PX, IMAGE_LEGEND_SCALE_TOP_Y_PX + lastRow,
            IMAGE_LEGEND_SCALE_WIDTH_PX, 1, black, IMAGE_LEGEND_BORDER_ALPHA);
    ImageLegendFillRect(target, IMAGE_LEGEND_SCALE_X_PX, IMAGE_LEGEND_SCALE_TOP_Y_PX,
            1, IMAGE_LEGEND_SCALE_HEIGHT_PX, black, IMAGE_LEGEND_BORDER_ALPHA);
    ImageLegendFillRect(target, IMAGE_LEGEND_SCALE_X_PX + IMAGE_LEGEND_SCALE_WIDTH_PX - 1,
            IMAGE_LEGEND_SCALE_TOP_Y_PX, 1, IMAGE_LEGEND_SCALE_HEIGHT_PX, black,
            IMAGE_LEGEND_BORDER_ALPHA);
}

void ImageLegend_DrawRGB888(void *rgb888, uint32_t width, uint32_t height)
{
    IMAGE_LEGEND_TARGET target;
    char text[16];
    float minCelsius;
    float maxCelsius;
    bool seeded;
    int32_t chipHeight;

    if (rgb888 == NULL) return;

    // Anything smaller than the box would have every blend clipped away; the
    // caller passing the wrong geometry is worth catching here rather than
    // producing an image with a quarter of a legend on it
    if ((width < (uint32_t)(IMAGE_LEGEND_BOX_X_PX + IMAGE_LEGEND_BOX_WIDTH_PX)) ||
        (height < (uint32_t)(IMAGE_LEGEND_BOX_Y_PX + IMAGE_LEGEND_BOX_HEIGHT_PX))) return;

    target.base = (uint8_t *)rgb888;
    target.width = (int32_t)width;
    target.height = (int32_t)height;

    ImageLegendDrawScale(&target);

    chipHeight = lv_font_get_line_height(IMAGE_LEGEND_FONT) +
                 (2 * IMAGE_LEGEND_LABEL_PAD_VER_PX);

    // Identical formatting and identical placeholder to ScreenHome_Refresh():
    // before the AGC window is seeded the numbers are the boot default, which
    // converts to nonsense degrees rather than to "unknown"
    seeded = FLIRProcess_GetAGCWindowCelsius(&minCelsius, &maxCelsius);

    if (seeded) snprintf(text, sizeof(text), "%.1f C", maxCelsius);
    else snprintf(text, sizeof(text), "--.- C");

    ImageLegendDrawChipLabel(&target,
            IMAGE_LEGEND_SCALE_TOP_Y_PX - IMAGE_LEGEND_LABEL_GAP_PX - chipHeight, text);

    if (seeded) snprintf(text, sizeof(text), "%.1f C", minCelsius);
    else snprintf(text, sizeof(text), "--.- C");

    ImageLegendDrawChipLabel(&target,
            IMAGE_LEGEND_SCALE_TOP_Y_PX + IMAGE_LEGEND_SCALE_HEIGHT_PX + IMAGE_LEGEND_LABEL_GAP_PX,
            text);
}
