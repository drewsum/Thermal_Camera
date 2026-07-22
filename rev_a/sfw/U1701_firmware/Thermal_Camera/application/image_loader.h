/*******************************************************************************
  PNG Image Loader

  File Name:
    image_loader.h

  Summary:
    Decodes a PNG file from either filesystem volume (microSD card or SPI
    flash) into the GLCD frame buffer in DDR2.

  Description:
    Decoding is done by lodepng, which is vendored as part of LVGL
    (gui/lvgl/src/libs/lodepng/, zlib license) rather than separately --
    one PNG decoder in the tree, one set of allocators, one place to patch.
    LVGL's copy allocates through lv_malloc(), so a decode comes out of the
    4MB LVGL heap in DDR2 (LV_MEM_ADR/LV_MEM_SIZE in gui/lv_conf.h, mapped
    in gui/gui.h) rather than the device's 115KB internal-RAM heap, which a
    decoded 320x240 frame plus zlib's working buffers would blow through
    several times over.

    WARNING for anyone reading lodepng's own documentation: LVGL's copy is
    patched and its decode entry points do NOT behave as upstream describes.
    lodepng_decode_memory()'s out-parameter is an lv_draw_buf_t descriptor,
    not a pixel buffer; the pixels live at its ->data, it is always
    allocated as LV_COLOR_FORMAT_ARGB8888 (4 bytes/pixel) whatever color
    type is requested, and it must be released with lv_draw_buf_destroy()
    rather than free()/lv_free(). image_loader.c therefore asks for
    LCT_RGBA and packs 4-byte pixels down to the frame buffer's 3-byte
    RGB888 itself.

    Because that heap is a real allocator and not the bump arena this
    module used to own, every buffer taken here is released on every exit
    path -- a leak would eventually starve both the decoder and the GUI.

    The heap is the CACHED (KSEG0) DDR2 alias, for decode speed; the final
    blit into the frame buffer goes through the UNCACHED (KSEG1) alias, so
    no cache maintenance is needed anywhere (same coherency reasoning as
    core/ddr2.h's cache note). GUI_Initialize() (gui/gui.c) must have run
    before any decode, since it is what hands LVGL its heap.

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

#include "core/ddr2.h"

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
