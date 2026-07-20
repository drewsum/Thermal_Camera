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

    Blocking: a load takes filesystem reads plus a full inflate pass --
    call it from thread/command context only (it's wired to the "Display
    Image:" USB UART command), never from an ISR. The watchdog is kicked
    around the decode.
*******************************************************************************/

#ifndef IMAGE_LOADER_H
#define IMAGE_LOADER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Which filesystem volume to load from (see sdhc/sd_fileio.h and
// spi/flash_fileio.h for the two volumes' mount lifecycles).
typedef enum
{
    IMAGE_MEDIA_SD_CARD,    // FatFs volume "0:" (microSD card)
    IMAGE_MEDIA_SPI_FLASH,  // FatFs volume "1:" (SST25VF080B SPI flash)
} IMAGE_MEDIA;

// Reads `filename` (8.3 name, no volume prefix) from `media`, decodes it
// as a PNG, and blits it into the GLCD frame buffer. Prints its own
// colored success/failure diagnostics to the terminal (file errors, PNG
// decode errors via lodepng_error_text(), dimension mismatches). Returns
// true only if the frame buffer was actually updated.
bool ImageLoader_DisplayPNG(IMAGE_MEDIA media, const char *filename);

#ifdef __cplusplus
}
#endif

#endif /* IMAGE_LOADER_H */
