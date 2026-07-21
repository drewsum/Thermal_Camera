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
#include "application/lodepng/lodepng.h"
#include "core/device_control.h"
#include "core/ddr2.h"
#include "core/watchdog_timer.h"
#include "glcd/glcd.h"
#include "sdhc/fatfs/ff.h"
#include "usb_uart/terminal_control.h"

// *****************************************************************************
// Section: DDR2 decode arena (lodepng allocator backing store)
// *****************************************************************************
// IMAGE_LOADER_ARENA_BASE/SIZE now live in image_loader.h (public) -- see
// that header for the partitioning/coherency rationale.

// Refuse absurd input files early (the arena must hold the file, zlib's
// inflated scanlines, and the converted output simultaneously)
#define IMAGE_LOADER_MAX_FILE_BYTES  0x00200000u

// Each allocation is prefixed with an 8-byte header recording its size, so
// lodepng_realloc() knows how much to copy when it can't grow in place.
typedef struct
{
    uint32_t size;
    uint32_t pad;   // keeps payloads 8-byte aligned
} IMAGE_LOADER_ARENA_HEADER;

static uint8_t *arena_next = NULL;
static uint8_t *arena_last_payload = NULL;

static void ImageLoader_ArenaReset(void)
{
    arena_next = IMAGE_LOADER_ARENA_BASE;
    arena_last_payload = NULL;
}

// --- lodepng allocator hooks -------------------------------------------------
// Global (non-static) on purpose: lodepng.c declares exactly these three
// symbols when LODEPNG_NO_COMPILE_ALLOCATORS is set (see the marked
// configuration block in application/lodepng/lodepng.h). Simple bump
// allocator: free is a no-op (the whole arena is reset per load), and
// realloc grows the most recent allocation in place -- which is lodepng's
// dominant realloc pattern (it repeatedly doubles its output vectors).

void *lodepng_malloc(size_t size)
{
    if (size == 0) size = 1;

    size_t total = (sizeof(IMAGE_LOADER_ARENA_HEADER) + size + 7u) & ~(size_t)7u;
    size_t remaining = (size_t)((IMAGE_LOADER_ARENA_BASE + IMAGE_LOADER_ARENA_SIZE) - arena_next);

    if (total > remaining) return NULL;

    IMAGE_LOADER_ARENA_HEADER *header = (IMAGE_LOADER_ARENA_HEADER *)arena_next;
    header->size = (uint32_t)size;
    arena_next += total;
    arena_last_payload = (uint8_t *)(header + 1);
    return arena_last_payload;
}

void *lodepng_realloc(void *ptr, size_t new_size)
{
    if (ptr == NULL) return lodepng_malloc(new_size);

    IMAGE_LOADER_ARENA_HEADER *header = ((IMAGE_LOADER_ARENA_HEADER *)ptr) - 1;

    if (new_size <= header->size)
    {
        return ptr;   // shrink: keep block (header->size stays = capacity)
    }

    if (ptr == arena_last_payload)
    {
        // Most recent allocation: grow it in place by moving the bump pointer
        size_t total = (sizeof(IMAGE_LOADER_ARENA_HEADER) + new_size + 7u) & ~(size_t)7u;
        if (((uint8_t *)header + total) > (IMAGE_LOADER_ARENA_BASE + IMAGE_LOADER_ARENA_SIZE)) return NULL;
        header->size = (uint32_t)new_size;
        arena_next = (uint8_t *)header + total;
        return ptr;
    }

    void *grown = lodepng_malloc(new_size);
    if (grown == NULL) return NULL;
    memcpy(grown, ptr, header->size);
    return grown;
}

void lodepng_free(void *ptr)
{
    (void)ptr;   // arena is reset wholesale at the start of each load
}

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
    unsigned char *decoded;
    unsigned int decode_error, width, height;
    uint32_t decode_start_ticks;

    snprintf(path, sizeof(path), "%s%s",
            (media == IMAGE_MEDIA_SPI_FLASH) ? "1:" : "0:", filename);

    ImageLoader_ArenaReset();

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

    file_buffer = lodepng_malloc(file_bytes);   // arena can't fail at this size, but check anyway
    if ((file_buffer == NULL) ||
        (f_read(&image_file, file_buffer, file_bytes, &bytes_read) != FR_OK) ||
        (bytes_read != file_bytes))
    {
        f_close(&image_file);
        terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("Failed reading %s from the filesystem\r\n", path);
        terminalTextAttributesReset();
        return false;
    }
    f_close(&image_file);

    // Full inflate pass over the image -- kick the watchdog on both sides
    // rather than assuming the decode fits in the remaining WDT window
    kickTheDog();
    decode_start_ticks = _CP0_GET_COUNT();
    decode_error = lodepng_decode_memory(&decoded, &width, &height,
            file_buffer, file_bytes, LCT_RGB, 8);
    kickTheDog();

    if (decode_error != 0)
    {
        terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("PNG decode of %s failed: %s (lodepng error %u)\r\n",
                path, lodepng_error_text(decode_error), decode_error);
        terminalTextAttributesReset();
        return false;
    }

    if ((width != GLCD_FRAMEBUFFER_WIDTH_PX) || (height != GLCD_FRAMEBUFFER_HEIGHT_PX))
    {
        terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("%s is %ux%u -- the frame buffer requires exactly %ux%u\r\n",
                path, width, height,
                (unsigned int)GLCD_FRAMEBUFFER_WIDTH_PX, (unsigned int)GLCD_FRAMEBUFFER_HEIGHT_PX);
        terminalTextAttributesReset();
        return false;
    }

    // Blit into the frame buffer through the uncached alias. lodepng's
    // LCT_RGB output is tightly-packed R,G,B rows of width*3 = 960 bytes,
    // which exactly equals GLCD_FRAMEBUFFER_STRIDE_BYTES, so the whole
    // frame is one contiguous copy. Byte order: the GLCD maps R to
    // GD<7:0>, G to GD<15:8>, B to GD<23:16>, so little-endian packed
    // RGB888 memory order is R,G,B -- matching PNG. If a displayed image
    // ever shows red and blue swapped, replace this memcpy with a
    // per-pixel copy that swaps bytes 0 and 2.
    memcpy((void *)GLCD_FRAMEBUFFER_BASE_ADDRESS, decoded, GLCD_FRAMEBUFFER_SIZE_BYTES);

    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("Displayed %s (%ux%u, %lu byte file, decoded in %lu ms)\r\n",
            path, width, height, (unsigned long)file_bytes,
            (unsigned long)(((uint64_t)(_CP0_GET_COUNT() - decode_start_ticks) * 2000u) / SYSCLK_INT));
    terminalTextAttributesReset();

    return true;
}
