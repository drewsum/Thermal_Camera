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
    IMAGE_MEDIA_SPI_FLASH,  // FatFs volume "1:" (W25Q128JV SPI flash)
} IMAGE_MEDIA;

// Reads `filename` (8.3 name, no volume prefix) from `media`, decodes it
// as a PNG, blits it into the GLCD Layer 2 (still-image) buffer, and enables
// that layer so the image shows on top of the thermal video and GUI. Prints
// its own colored success/failure diagnostics to the terminal (file errors,
// PNG decode errors via lodepng_error_text(), dimension mismatches). Returns
// true only if the image layer was actually updated.
bool ImageLoader_DisplayPNG(IMAGE_MEDIA media, const char *filename);

// Hides the still-image layer (disables GLCD Layer 2), revealing the thermal
// video (Layer 0) and GUI (Layer 1) again. Safe to call when nothing is shown.
void ImageLoader_Clear(void);

// Reads and decodes `filename` from `media` exactly as above, but instead of
// going to the image layer it box-averages the picture down to
// `width` x `height` and writes it into `destination` -- which the caller
// owns and which must hold width * height * 3 bytes with no row padding.
// This is what puts a preview next to each row of the GUI's Saved Images
// list (gui/screens/screen_saved_images.c).
//
// Any source dimensions are accepted, since nothing here has to line up with
// a hardware layer; a thumbnail larger than its source is refused, because
// box-averaging cannot invent pixels and a caller asking for one has a bug.
//
// BYTE ORDER: the output is LVGL's LV_COLOR_FORMAT_RGB888, whose memory
// order is B,G,R -- the REVERSE of the R,G,B the GLCD layers use (see
// lv_color_t in gui/lvgl/src/misc/lv_color.h). A thumbnail that comes out
// with red and blue swapped is this, not the panel.
//
// Quiet on success and one line on failure, unlike the loader above: this
// runs once per file in a directory listing, where the per-image chatter
// that suits a UART command would bury the console.
//
// Blocking: a full inflate pass over the source image, the same cost as a
// display load. Thread/command context only, and the watchdog is kicked
// around the decode.
bool ImageLoader_DecodeThumbnail(IMAGE_MEDIA media, const char *filename,
        uint8_t *destination, uint32_t width, uint32_t height);

#ifdef __cplusplus
}
#endif

#endif /* IMAGE_LOADER_H */
