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

bool ImageLoader_DisplayPNG(IMAGE_MEDIA media, const char *filename)
{
    char path[80];
    FRESULT open_result;
    UINT file_bytes, bytes_read;
    uint8_t *file_buffer;
    lv_draw_buf_t *decoded = NULL;   // NOT a pixel pointer -- see the decode call below
    unsigned int decode_error, width, height;
    uint32_t decode_start_ticks;

    snprintf(path, sizeof(path), "%s%s",
            (media == IMAGE_MEDIA_SPI_FLASH) ? "1:" : "0:", filename);

    open_result = f_open(&image_file, path, FA_READ);
    if (open_result != FR_OK)
    {
        terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("Could not open %s: FRESULT %d (%s)\r\n",
                path, (int)open_result, ImageLoader_DescribeFRESULT(open_result));
        terminalTextAttributesReset();
        return false;
    }

    file_bytes = (UINT)f_size(&image_file);
    if ((file_bytes == 0) || (file_bytes > IMAGE_LOADER_MAX_FILE_BYTES))
    {
        f_close(&image_file);
        terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("%s is %lu bytes -- not a plausible PNG for this display (max %lu)\r\n",
                path, (unsigned long)file_bytes, (unsigned long)IMAGE_LOADER_MAX_FILE_BYTES);
        terminalTextAttributesReset();
        return false;
    }

    // The whole compressed file is read into memory in one go (lodepng
    // decodes from a buffer, not a stream). lv_malloc() draws on the LVGL
    // heap in DDR2 -- see image_loader.h -- and can genuinely fail here if
    // the GUI has the heap busy, so it is checked.
    file_buffer = lv_malloc(file_bytes);
    if (file_buffer == NULL)
    {
        f_close(&image_file);
        terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("Not enough room in the LVGL heap for %s (%lu bytes) -- see 'Peripheral Status? GUI'\r\n",
                path, (unsigned long)file_bytes);
        terminalTextAttributesReset();
        return false;
    }

    if ((f_read(&image_file, file_buffer, file_bytes, &bytes_read) != FR_OK) ||
        (bytes_read != file_bytes))
    {
        f_close(&image_file);
        lv_free(file_buffer);
        terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("Failed reading %s from the filesystem\r\n", path);
        terminalTextAttributesReset();
        return false;
    }
    f_close(&image_file);

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

void ImageLoader_Clear(void)
{
    // Hide Layer 2 so the thermal video (Layer 0) and GUI (Layer 1) are visible
    // again. The buffer contents are left as-is (cheap; re-shown only if a new
    // image is loaded).
    GLCD_Layer2SetEnabled(false);
}
