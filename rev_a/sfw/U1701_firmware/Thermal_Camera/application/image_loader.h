/*******************************************************************************
  PNG Image Loader

  File Name:
    image_loader.h

  Summary:
    Decodes a PNG file from either filesystem volume (microSD card or SPI
    flash) into the GLCD frame buffer in DDR2.

  Description:
    Decoding is done by the vendored lodepng library
    (application/lodepng/, zlib license, decode-only build -- see the
    marked configuration block at the top of lodepng.h). This module owns
    lodepng's memory: it provides the lodepng_malloc()/lodepng_realloc()/
    lodepng_free() hooks (enabled by LODEPNG_NO_COMPILE_ALLOCATORS),
    backed by a bump-allocator arena in DDR2 rather than the internal-RAM
    heap -- a decoded 320x240 frame plus zlib's working buffers peak at
    several hundred KB, which does not fit this device's 115KB heap but is
    a rounding error against 32MB of DDR2.

    DDR2 partitioning (this module's reservation, alongside the frame
    buffer reservation documented in glcd/glcd.h): the arena occupies
    physical DDR2 offset +1MB through +5MB, accessed through the CACHED
    (KSEG0) alias for decode speed. That never conflicts with the GLCD's
    DMA, which only reads the frame buffer region (offset 0), and the
    final blit into the frame buffer goes through the UNCACHED (KSEG1)
    alias -- so no cache maintenance is needed anywhere (same coherency
    reasoning as core/ddr2.h's cache note). The arena is reset wholesale
    at the start of every load; nothing in it persists between calls.

    Requirements on the PNG: any color type/bit depth lodepng can decode
    (it converts to 24-bit RGB), but the pixel dimensions must be exactly
    the panel's 320x240 -- scaling/centering is deliberately not
    implemented. Note the filesystems are mounted with FF_USE_LFN = 0
    (sdhc/fatfs/ffconf.h), so filenames must be 8.3 short names (e.g.
    "TEST.PNG"; matching is case-insensitive).

    Incremental: a load is started by ImageLoader_StartPNG() (wired to the
    "Display Image:" USB UART command) and then carried to completion by
    ImageLoader_Tasks(), pumped once per main-loop iteration from main.c
    -- the same cooperative-task shape as SDFileIO_HotSwapTasks() and
    USB_MSD_TimedTasks(). Both are main-loop/command context only, never
    an ISR. The watchdog is kicked around the decode.
*******************************************************************************/

#ifndef IMAGE_LOADER_H
#define IMAGE_LOADER_H

#include <stdint.h>
#include <stdbool.h>

#include "core/ddr2.h"

#ifdef __cplusplus
extern "C" {
#endif

// DDR2 decode arena (lodepng allocator backing store): physical DDR2
// offset +1MB..+5MB, through the CACHED (KSEG0) alias. Public (rather than
// private to image_loader.c) so other modules -- currently just the
// "Storage Usage?" command -- can report this reservation without
// duplicating the constants. See the file header above for the
// partitioning/coherency rationale.
#define IMAGE_LOADER_ARENA_BASE   ((uint8_t *)(DDR2_KSEG0_BASE_ADDRESS + 0x00100000u))
#define IMAGE_LOADER_ARENA_SIZE   0x00400000u

// Which filesystem volume to load from (see sdhc/sd_fileio.h and
// spi/flash_fileio.h for the two volumes' mount lifecycles).
typedef enum
{
    IMAGE_MEDIA_SD_CARD,    // FatFs volume "0:" (microSD card)
    IMAGE_MEDIA_SPI_FLASH,  // FatFs volume "1:" (SST25VF080B SPI flash)
} IMAGE_MEDIA;

// Begins loading `filename` (8.3 name, no volume prefix) from `media`,
// and returns as soon as the file is open and validated -- the read and
// decode are then carried out incrementally by ImageLoader_Tasks().
// Returns false if the load could not be STARTED (already busy, file
// missing, implausible size, arena exhausted); a true return means the
// load is under way, not that it succeeded.
//
// All success/failure diagnostics -- file errors, PNG decode errors via
// lodepng_error_text(), dimension mismatches, and the final timing line
// -- are printed by this module, from whichever call actually detects
// them (so late failures surface out of ImageLoader_Tasks(), after this
// function has already returned true).
bool ImageLoader_StartPNG(IMAGE_MEDIA media, const char *filename);

// Pump once per main-loop iteration. Cheap no-op when idle. Carries an
// in-progress load forward by one step: one file chunk per call during
// the read phase, then the decode+blit as a single step.
//
// This is what keeps a large image from freezing the main loop: a 220KB
// PNG off SPI flash is ~180ms of bus time, which as one read would starve
// USB_Tasks(), the watchdog kick and every other main-loop service for
// that whole window. Note the decode phase is NOT divisible (lodepng is
// vendored and decodes in one call), so it remains a single blocking
// step with watchdog kicks either side.
void ImageLoader_Tasks(void);

// True from a successful ImageLoader_StartPNG() until the load has
// finished or failed. Callers that need to serialize against a load (or
// avoid stacking a second one) should test this first.
bool ImageLoader_IsBusy(void);

#ifdef __cplusplus
}
#endif

#endif /* IMAGE_LOADER_H */
