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
#include "spi/spi3.h"           // SPI3_DMA_BUFFER_ALIGNMENT (arena payload alignment)
#include "usb_uart/terminal_control.h"

// *****************************************************************************
// Section: DDR2 decode arena (lodepng allocator backing store)
// *****************************************************************************
// IMAGE_LOADER_ARENA_BASE/SIZE now live in image_loader.h (public) -- see
// that header for the partitioning/coherency rationale.

// Refuse absurd input files early (the arena must hold the file, zlib's
// inflated scanlines, and the converted output simultaneously)
#define IMAGE_LOADER_MAX_FILE_BYTES  0x00200000u

// Each allocation is prefixed with a header recording its size, so
// lodepng_realloc() knows how much to copy when it can't grow in place.
//
// The header is padded to SPI3_DMA_BUFFER_ALIGNMENT (16B) -- and
// allocation sizes are rounded to the same -- so every payload comes back
// 16-byte aligned. That matters beyond tidiness: the SPI flash DMA path
// REQUIRES 16-byte alignment (spi3.h) and silently falls back to a
// byte-at-a-time loop otherwise. With the previous 8-byte header the PNG
// file buffer landed at base+8, so the whole image read missed DMA
// entirely. IMAGE_LOADER_ARENA_BASE is 1MB-aligned, so base itself
// already satisfies this.
typedef struct
{
    uint32_t size;
    uint32_t pad[3];
} IMAGE_LOADER_ARENA_HEADER;

#define IMAGE_LOADER_ARENA_ALIGN   SPI3_DMA_BUFFER_ALIGNMENT

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

    size_t total = (sizeof(IMAGE_LOADER_ARENA_HEADER) + size + (IMAGE_LOADER_ARENA_ALIGN - 1u))
            & ~(size_t)(IMAGE_LOADER_ARENA_ALIGN - 1u);
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
        size_t total = (sizeof(IMAGE_LOADER_ARENA_HEADER) + new_size + (IMAGE_LOADER_ARENA_ALIGN - 1u))
                & ~(size_t)(IMAGE_LOADER_ARENA_ALIGN - 1u);
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

// --- Chunked load state machine ---------------------------------------
//
// The file read is broken into IMAGE_LOADER_READ_CHUNK_BYTES pieces, one
// per ImageLoader_Tasks() call, so a large image no longer freezes the
// main loop for the whole read. A 220KB PNG off SPI flash is ~180ms of
// bus time; as one f_read() that was a single 180ms stall during which
// USB_Tasks(), the watchdog kick, and every other main-loop service was
// starved. Chunked, the worst-case stall becomes one chunk.
//
// WHAT THIS DOES NOT DO: it is not true I/O/compute overlap. Each
// f_read() chunk still blocks (FatFs has no async form -- see
// spi/flash_async.h), so the CPU is idle inside a chunk; the win is that
// the loop gets a turn BETWEEN chunks instead of only at the end. The
// decode is likewise a single monolithic lodepng_decode_memory() call
// into a vendored library and cannot be split, so it remains one blocking
// step (with watchdog kicks either side, as before).

#define IMAGE_LOADER_READ_CHUNK_BYTES   4096u

typedef enum
{
    IMAGE_LOADER_STATE_IDLE = 0,
    IMAGE_LOADER_STATE_READING,
    IMAGE_LOADER_STATE_DECODING,
} IMAGE_LOADER_STATE;

static IMAGE_LOADER_STATE loader_state = IMAGE_LOADER_STATE_IDLE;
static char      loader_path[80];
static uint8_t  *loader_file_buffer = NULL;
static UINT      loader_file_bytes = 0;
static UINT      loader_bytes_read = 0;
static uint32_t  loader_start_ticks = 0;

// Common teardown for every exit path: closes the file if the read phase
// still had it open and parks the state machine. The arena is NOT reset
// here -- it is reset at the start of the next load, so a caller can
// still inspect decoded output after completion.
static void ImageLoader_Finish(void)
{
    if (loader_state == IMAGE_LOADER_STATE_READING)
    {
        f_close(&image_file);
    }
    loader_state = IMAGE_LOADER_STATE_IDLE;
    loader_file_buffer = NULL;
}

bool ImageLoader_IsBusy(void)
{
    return (loader_state != IMAGE_LOADER_STATE_IDLE);
}

bool ImageLoader_StartPNG(IMAGE_MEDIA media, const char *filename)
{
    FRESULT open_result;

    if (ImageLoader_IsBusy())
    {
        terminalTextAttributes(YELLOW_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("An image load is already in progress\r\n");
        terminalTextAttributesReset();
        return false;
    }

    snprintf(loader_path, sizeof(loader_path), "%s%s",
            (media == IMAGE_MEDIA_SPI_FLASH) ? "1:" : "0:", filename);

    ImageLoader_ArenaReset();

    open_result = f_open(&image_file, loader_path, FA_READ);
    if (open_result != FR_OK)
    {
        terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("Could not open %s: FRESULT %d (%s)\r\n",
                loader_path, (int)open_result, ImageLoader_DescribeFRESULT(open_result));
        terminalTextAttributesReset();
        return false;
    }

    loader_file_bytes = (UINT)f_size(&image_file);
    if ((loader_file_bytes == 0) || (loader_file_bytes > IMAGE_LOADER_MAX_FILE_BYTES))
    {
        f_close(&image_file);
        terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("%s is %lu bytes -- not a plausible PNG for this display (max %lu)\r\n",
                loader_path, (unsigned long)loader_file_bytes,
                (unsigned long)IMAGE_LOADER_MAX_FILE_BYTES);
        terminalTextAttributesReset();
        return false;
    }

    // Arena payloads are SPI3_DMA_BUFFER_ALIGNMENT-aligned, so chunk
    // reads landing in here qualify for the flash DMA path
    loader_file_buffer = lodepng_malloc(loader_file_bytes);
    if (loader_file_buffer == NULL)
    {
        f_close(&image_file);
        terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("Could not allocate %lu bytes for %s\r\n",
                (unsigned long)loader_file_bytes, loader_path);
        terminalTextAttributesReset();
        return false;
    }

    loader_bytes_read = 0;
    loader_start_ticks = _CP0_GET_COUNT();
    loader_state = IMAGE_LOADER_STATE_READING;

    return true;
}

// Reads one chunk. Returns false (having reported the error and torn
// down) if the read failed or came up short.
static bool ImageLoader_ReadChunk(void)
{
    UINT remaining = loader_file_bytes - loader_bytes_read;
    UINT want = (remaining > IMAGE_LOADER_READ_CHUNK_BYTES)
            ? IMAGE_LOADER_READ_CHUNK_BYTES : remaining;
    UINT got = 0;

    FRESULT fr = f_read(&image_file, loader_file_buffer + loader_bytes_read, want, &got);

    // A short read before EOF means the media went away mid-load (e.g. an
    // SD card pulled, or the USB host taking the flash volume), not just
    // a slow device -- FatFs only returns less than requested at EOF.
    if ((fr != FR_OK) || (got != want))
    {
        terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("Failed reading %s at offset %lu: FRESULT %d (%s)\r\n",
                loader_path, (unsigned long)loader_bytes_read,
                (int)fr, ImageLoader_DescribeFRESULT(fr));
        terminalTextAttributesReset();
        ImageLoader_Finish();
        return false;
    }

    loader_bytes_read += got;
    return true;
}

// Runs the single monolithic decode + blit. Prints its own diagnostics.
static void ImageLoader_DecodeAndBlit(void)
{
    unsigned char *decoded;
    unsigned int decode_error, width, height;
    uint32_t decode_start_ticks;

    // Full inflate pass over the image -- kick the watchdog on both sides
    // rather than assuming the decode fits in the remaining WDT window
    kickTheDog();
    decode_start_ticks = _CP0_GET_COUNT();
    decode_error = lodepng_decode_memory(&decoded, &width, &height,
            loader_file_buffer, loader_file_bytes, LCT_RGB, 8);
    kickTheDog();

    if (decode_error != 0)
    {
        terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("PNG decode of %s failed: %s (lodepng error %u)\r\n",
                loader_path, lodepng_error_text(decode_error), decode_error);
        terminalTextAttributesReset();
        return;
    }

    if ((width != GLCD_FRAMEBUFFER_WIDTH_PX) || (height != GLCD_FRAMEBUFFER_HEIGHT_PX))
    {
        terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("%s is %ux%u -- the frame buffer requires exactly %ux%u\r\n",
                loader_path, width, height,
                (unsigned int)GLCD_FRAMEBUFFER_WIDTH_PX, (unsigned int)GLCD_FRAMEBUFFER_HEIGHT_PX);
        terminalTextAttributesReset();
        return;
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
    printf("Displayed %s (%ux%u, %lu byte file, read in %lu ms, decoded in %lu ms)\r\n",
            loader_path, width, height, (unsigned long)loader_file_bytes,
            (unsigned long)(((uint64_t)(decode_start_ticks - loader_start_ticks) * 2000u) / SYSCLK_INT),
            (unsigned long)(((uint64_t)(_CP0_GET_COUNT() - decode_start_ticks) * 2000u) / SYSCLK_INT));
    terminalTextAttributesReset();
}

void ImageLoader_Tasks(void)
{
    switch (loader_state)
    {
        case IMAGE_LOADER_STATE_READING:
            if (!ImageLoader_ReadChunk())
            {
                return;     // already torn down and reported
            }

            if (loader_bytes_read >= loader_file_bytes)
            {
                // File is fully in the arena -- release it before the
                // decode, so the volume isn't held open across a
                // potentially long inflate
                f_close(&image_file);
                loader_state = IMAGE_LOADER_STATE_DECODING;
            }
            break;

        case IMAGE_LOADER_STATE_DECODING:
            // Not chunkable (see the section comment) -- one blocking
            // step, then done either way
            ImageLoader_DecodeAndBlit();
            ImageLoader_Finish();
            break;

        case IMAGE_LOADER_STATE_IDLE:
        default:
            break;
    }
}
