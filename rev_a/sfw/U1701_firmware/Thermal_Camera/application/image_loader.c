/*******************************************************************************
  PNG Image Loader

  File Name:
    image_loader.c

  Summary:
    PNG-from-filesystem-to-frame-buffer loader. See image_loader.h for the
    DDR2 arena design and requirements on the PNG.
*******************************************************************************/

#include <xc.h>
#include <stdio.h>
#include <string.h>

#include "application/image_loader.h"
#include "core/device_control.h"
#include "core/ddr2.h"
#include "core/watchdog_timer.h"
#include "glcd/glcd.h"
#include "gui/lvgl/lvgl.h"
#include "gui/lvgl/src/libs/lodepng/lodepng.h"
#include "sdhc/fatfs/ff.h"
#include "usb_uart/terminal_control.h"

// Refuse absurd input files early. The LVGL heap has to hold the file, the
// inflated scanlines and the converted output simultaneously, and it is
// shared with the GUI -- a runaway file must fail here rather than by
// starving the display.
#define IMAGE_LOADER_MAX_FILE_BYTES  0x00200000u

// *****************************************************************************
// Section: Interface Routines
// *****************************************************************************

// FatFs keeps a 512-byte sector window inside FIL -- static rather than on
// the command handler's stack, same as the FIL usage in sd_fileio.c.
static FIL image_file;

// Friendly hints for the FRESULT codes a user is actually likely to hit;
// everything else just prints the numeric code.
static const char *ImageLoader_DescribeFRESULT(FRESULT result)
{
    switch (result)
    {
        case FR_NO_FILE:
        case FR_NO_PATH:        return "file not found (note: 8.3 filenames only)";
        case FR_NOT_READY:      return "media not ready (card removed?)";
        case FR_NOT_ENABLED:
        case FR_INVALID_DRIVE:  return "volume not mounted";
        case FR_INVALID_NAME:   return "invalid filename (8.3 names only)";
        default:                return "see FRESULT in sdhc/fatfs/ff.h";
    }
}

// Builds the volume-qualified path the FatFs calls below take. `filename`
// carries no volume of its own -- see image_loader.h.
static void ImageLoaderBuildPath(IMAGE_MEDIA media, const char *filename,
        char *path, size_t size)
{
    snprintf(path, size, "%s%s",
            (media == IMAGE_MEDIA_SPI_FLASH) ? "1:" : "0:", filename);
}

// Reads all of `path` into a fresh LVGL-heap buffer and returns it, with its
// length in *sizeOut. The caller owns the buffer and releases it with
// lv_free(). NULL on any failure, having printed the reason.
//
// The whole compressed file is read in one go because that is what lodepng's
// memory decoder takes (it does not stream), and it lands in the LVGL heap in
// DDR2 rather than the device's internal-RAM heap because a PNG of this
// panel's size would not fit in the latter -- see image_loader.h.
static uint8_t *ImageLoaderReadFile(const char *path, UINT *sizeOut)
{
    FRESULT open_result;
    UINT file_bytes, bytes_read;
    uint8_t *file_buffer;

    open_result = f_open(&image_file, path, FA_READ);
    if (open_result != FR_OK)
    {
        terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("Could not open %s: FRESULT %d (%s)\r\n",
                path, (int)open_result, ImageLoader_DescribeFRESULT(open_result));
        terminalTextAttributesReset();
        return NULL;
    }

    file_bytes = (UINT)f_size(&image_file);
    if ((file_bytes == 0) || (file_bytes > IMAGE_LOADER_MAX_FILE_BYTES))
    {
        f_close(&image_file);
        terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("%s is %lu bytes -- not a plausible PNG for this display (max %lu)\r\n",
                path, (unsigned long)file_bytes, (unsigned long)IMAGE_LOADER_MAX_FILE_BYTES);
        terminalTextAttributesReset();
        return NULL;
    }

    // lv_malloc() draws on the LVGL heap, which the GUI shares -- this can
    // genuinely fail, so it is checked
    file_buffer = lv_malloc(file_bytes);
    if (file_buffer == NULL)
    {
        f_close(&image_file);
        terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("Not enough room in the LVGL heap for %s (%lu bytes) -- see 'Peripheral Status? GUI'\r\n",
                path, (unsigned long)file_bytes);
        terminalTextAttributesReset();
        return NULL;
    }

    if ((f_read(&image_file, file_buffer, file_bytes, &bytes_read) != FR_OK) ||
        (bytes_read != file_bytes))
    {
        f_close(&image_file);
        lv_free(file_buffer);
        terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("Failed reading %s from the filesystem\r\n", path);
        terminalTextAttributesReset();
        return NULL;
    }

    f_close(&image_file);

    *sizeOut = file_bytes;

    return file_buffer;
}

bool ImageLoader_DisplayPNG(IMAGE_MEDIA media, const char *filename)
{
    char path[80];
    UINT file_bytes;
    uint8_t *file_buffer;
    lv_draw_buf_t *decoded = NULL;   // NOT a pixel pointer -- see the decode call below
    unsigned int decode_error, width, height;
    uint32_t decode_start_ticks;

    ImageLoaderBuildPath(media, filename, path, sizeof(path));

    file_buffer = ImageLoaderReadFile(path, &file_bytes);
    if (file_buffer == NULL) return false;

    // Full inflate pass over the image -- kick the watchdog on both sides
    // rather than assuming the decode fits in the remaining WDT window
    //
    // CAREFUL: LVGL's copy of lodepng is PATCHED and does not honour
    // upstream's documented contract. Upstream sets *out to a malloc'd
    // buffer of raw pixels; LVGL's version instead allocates an
    // lv_draw_buf_t (a descriptor: header, data_size, and two pointers) and
    // casts THAT into the unsigned char ** out-parameter, with the pixels
    // hanging off its ->data member. Treating *out as pixels silently blits
    // the descriptor's own bytes -- heap addresses that move on every
    // allocation, so the same file renders differently every load. Hence
    // the cast here and the ->data access below; mirrors what LVGL itself
    // does in gui/lvgl/src/libs/lodepng/lv_lodepng.c's decode_png_data().
    //
    // LCT_RGBA (not LCT_RGB) because the buffer that patch allocates is
    // declared LV_COLOR_FORMAT_ARGB8888 with a 4-byte-per-pixel stride;
    // asking for 3-byte RGB would leave the contents disagreeing with the
    // descriptor. The 4-to-3 byte packing is done in the blit below.
    kickTheDog();
    decode_start_ticks = _CP0_GET_COUNT();
    decode_error = lodepng_decode_memory((unsigned char **)&decoded, &width, &height,
            file_buffer, file_bytes, LCT_RGBA, 8);
    kickTheDog();

    // The compressed file is dead weight from here on, and the decoded
    // frame it produced is over 300KB on its own
    lv_free(file_buffer);

    if (decode_error != 0)
    {
        // A failed decode can still have left a partly-built draw buffer
        // behind (LVGL's patch allocates before it can fail)
        if (decoded != NULL) lv_draw_buf_destroy(decoded);
        terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("PNG decode of %s failed: %s (lodepng error %u)\r\n",
                path, lodepng_error_text(decode_error), decode_error);
        terminalTextAttributesReset();
        return false;
    }

    if ((width != GLCD_LAYER2_WIDTH_PX) || (height != GLCD_LAYER2_HEIGHT_PX))
    {
        lv_draw_buf_destroy(decoded);
        terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("%s is %ux%u -- the image layer requires exactly %ux%u\r\n",
                path, width, height,
                (unsigned int)GLCD_LAYER2_WIDTH_PX, (unsigned int)GLCD_LAYER2_HEIGHT_PX);
        terminalTextAttributesReset();
        return false;
    }

    // Blit into the Layer 2 image buffer through the uncached alias, packing
    // lodepng's 4-byte R,G,B,A pixels down to the layer's 3-byte R,G,B ones.
    // Byte order: the GLCD maps R to GD<7:0>, G to GD<15:8>, B to GD<23:16>,
    // so little-endian packed RGB888 memory order is R,G,B -- matching the
    // order lodepng emits for LCT_RGBA. If a displayed image ever shows red
    // and blue swapped, reverse the three assignments in the inner loop.
    //
    // Packed a row at a time into a small stack buffer rather than written
    // pixel-by-pixel straight to the buffer: the destination is uncached,
    // where every byte store is its own bus transaction, while memcpy of a
    // whole row moves words.
    {
        const uint8_t *source = decoded->data;
        uint8_t *destination = (uint8_t *)GLCD_LAYER2_BASE_ADDRESS;
        uint8_t row[GLCD_LAYER2_STRIDE_BYTES];
        unsigned int x, y;

        for (y = 0; y < GLCD_LAYER2_HEIGHT_PX; y++)
        {
            for (x = 0; x < GLCD_LAYER2_WIDTH_PX; x++)
            {
                row[(x * 3u) + 0u] = source[(x * 4u) + 0u];   // R
                row[(x * 3u) + 1u] = source[(x * 4u) + 1u];   // G
                row[(x * 3u) + 2u] = source[(x * 4u) + 2u];   // B
                // source[(x * 4) + 3] is alpha, which Layer 2 (opaque
                // RGB888) has no channel for
            }

            memcpy(destination, row, GLCD_LAYER2_STRIDE_BYTES);

            source += (uint32_t)GLCD_LAYER2_WIDTH_PX * 4u;
            destination += GLCD_LAYER2_STRIDE_BYTES;
        }
    }

    lv_draw_buf_destroy(decoded);

    // Reveal the image: Layer 2 is disabled by default so the thermal video
    // (Layer 0) and GUI (Layer 1) show; enabling it composites the image on
    // top. ImageLoader_Clear() hides it again.
    GLCD_Layer2SetEnabled(true);

    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("    Displayed %s (%ux%u, %lu byte file, decoded in %lu ms)\r\n",
            path, width, height, (unsigned long)file_bytes,
            (unsigned long)(((uint64_t)(_CP0_GET_COUNT() - decode_start_ticks) * 2000u) / SYSCLK_INT));
    terminalTextAttributesReset();

    return true;
}

bool ImageLoader_DecodeThumbnail(IMAGE_MEDIA media, const char *filename,
        uint8_t *destination, uint32_t width, uint32_t height)
{
    char path[80];
    UINT file_bytes;
    uint8_t *file_buffer;
    lv_draw_buf_t *decoded = NULL;   // a descriptor, not pixels -- see below
    unsigned int decode_error, source_width, source_height;

    if ((destination == NULL) || (width == 0) || (height == 0)) return false;

    ImageLoaderBuildPath(media, filename, path, sizeof(path));

    file_buffer = ImageLoaderReadFile(path, &file_bytes);
    if (file_buffer == NULL) return false;

    // Same patched-lodepng contract as ImageLoader_DisplayPNG() above: *out
    // comes back as an lv_draw_buf_t descriptor with the pixels hanging off
    // its ->data, always 4-byte ARGB8888 whatever color type is asked for,
    // and it is released with lv_draw_buf_destroy(). Read the long comment
    // on the decode in DisplayPNG() before touching any of this.
    kickTheDog();
    decode_error = lodepng_decode_memory((unsigned char **)&decoded,
            &source_width, &source_height, file_buffer, file_bytes, LCT_RGBA, 8);
    kickTheDog();

    lv_free(file_buffer);

    if (decode_error != 0)
    {
        if (decoded != NULL) lv_draw_buf_destroy(decoded);
        terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("PNG decode of %s failed: %s (lodepng error %u)\r\n",
                path, lodepng_error_text(decode_error), decode_error);
        terminalTextAttributesReset();
        return false;
    }

    // Downscaling only -- see image_loader.h. An upscale would need a
    // resampler this has no reason to carry.
    if ((source_width < width) || (source_height < height))
    {
        lv_draw_buf_destroy(decoded);
        terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("%s is %ux%u -- too small for a %lux%lu thumbnail\r\n",
                path, source_width, source_height,
                (unsigned long)width, (unsigned long)height);
        terminalTextAttributesReset();
        return false;
    }

    // Box average: each output pixel is the mean of the source rectangle it
    // covers. Nearest-neighbour would be cheaper, but it drops 63 of every
    // 64 pixels at this reduction and turns the smooth gradients a thermal
    // palette produces into visible blocking -- and the averaging is a
    // rounding error next to the inflate that just ran.
    //
    // The rectangle edges are computed from the output coordinate rather
    // than from a fixed step, so a source whose size is not a multiple of
    // the thumbnail's still tiles it exactly with no gaps or overlap.
    {
        const uint8_t *source = decoded->data;
        uint32_t x, y;

        for (y = 0; y < height; y++)
        {
            uint32_t y0 = (y * source_height) / height;
            uint32_t y1 = (((y + 1u) * source_height) + height - 1u) / height;

            if (y1 <= y0) y1 = y0 + 1u;

            for (x = 0; x < width; x++)
            {
                uint32_t x0 = (x * source_width) / width;
                uint32_t x1 = (((x + 1u) * source_width) + width - 1u) / width;
                uint32_t red = 0, green = 0, blue = 0, pixels;
                uint32_t sx, sy;

                if (x1 <= x0) x1 = x0 + 1u;

                for (sy = y0; sy < y1; sy++)
                {
                    const uint8_t *row = &source[(uint32_t)sy * source_width * 4u];

                    for (sx = x0; sx < x1; sx++)
                    {
                        red   += row[(sx * 4u) + 0u];
                        green += row[(sx * 4u) + 1u];
                        blue  += row[(sx * 4u) + 2u];
                        // row[(sx * 4) + 3] is alpha, which the opaque
                        // RGB888 output has no channel for
                    }
                }

                pixels = (x1 - x0) * (y1 - y0);

                // B,G,R, NOT R,G,B: this buffer is consumed by LVGL as
                // LV_COLOR_FORMAT_RGB888, whose in-memory order is the
                // reverse of the GLCD layers' -- see image_loader.h.
                destination[0] = (uint8_t)(blue / pixels);
                destination[1] = (uint8_t)(green / pixels);
                destination[2] = (uint8_t)(red / pixels);
                destination += 3;
            }
        }
    }

    lv_draw_buf_destroy(decoded);

    kickTheDog();

    return true;
}

void ImageLoader_Clear(void)
{
    // Hide Layer 2 so the thermal video (Layer 0) and GUI (Layer 1) are visible
    // again. The buffer contents are left as-is (cheap; re-shown only if a new
    // image is loaded).
    GLCD_Layer2SetEnabled(false);
}
