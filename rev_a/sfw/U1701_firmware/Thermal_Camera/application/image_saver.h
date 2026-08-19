/* ************************************************************************** */
/** PNG Image Saver

  @File Name
    image_saver.h

  @Summary
    Writes a full-screen RGB888 frame buffer to the SD card as a PNG. The
    inverse of application/image_loader.c, and deliberately shaped like it.

  @Description
    Encoding is done by lodepng, the same copy that ships inside LVGL
    (gui/lvgl/src/libs/lodepng/) and that image_loader.c decodes with. The
    encoder half is NOT built by default -- cmake/Thermal_Camera/default/
    user.cmake used to pass LODEPNG_NO_COMPILE_ENCODER to keep it out of the
    2MB program flash; that definition was removed when this module landed.

    Unlike the decoder, the encoder is stock upstream: LVGL's patch only
    touches the decode path (it swaps the out-parameter for an lv_draw_buf_t
    -- see the warning in image_loader.h). So lodepng_encode_memory() here
    behaves exactly as upstream documents: *out comes back as a plain malloc'd
    byte buffer. That allocation still routes through lv_malloc(), so the PNG
    is built in the LVGL heap in DDR2 and freed with lv_free().

    Color type is left to lodepng's auto_convert, which is what makes this
    cheap: a thermal frame is colorized through a 256-entry palette LUT
    (flir_process.c), so the image has at most 256 distinct colors and the
    encoder emits an 8-bit palettized PNG -- a third of the pixel data of
    RGB888, and correspondingly less to deflate.

    That holds only for a bare thermal frame. A frame with the legend drawn
    into it (application/image_legend.h, which the save prompt offers and
    defaults to) carries blended pixels that are not palette entries, so
    auto_convert falls back to RGB888 and both the file and the encode grow
    by roughly the three times above. Deliberate: the legend is worth more
    than the bytes, and the checkbox is there for anyone who disagrees.

    Files are named FLIRnnnn.PNG in a FLIR/ directory at the root of the
    card, both created on demand. FatFs is built with FF_USE_LFN = 0
    (sdhc/fatfs/ffconf.h), so those names are 8.3 and uppercase on disk --
    "FLIR0001.PNG" is exactly 8 + 3. nnnn continues from the highest number
    already in the directory, so numbering survives reboots and card swaps
    without any state kept in the firmware.
 */
/* ************************************************************************** */

#ifndef IMAGE_SAVER_H
#define IMAGE_SAVER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Longest name this module produces, "FLIR9999.PNG" plus its terminator.
#define IMAGE_SAVER_NAME_MAX  13u

// Where the images go, and how their names are built. Public rather than
// private to image_saver.c because application/image_catalog.c reads back the
// very directory this module writes -- the two agreeing about the path and
// the naming scheme is the whole basis of that, so they share one definition
// instead of each carrying a copy that can drift.
//
// IMAGE_SAVER_DIRECTORY carries the volume prefix, for the FatFs calls;
// IMAGE_SAVER_DIRECTORY_PATH is the same directory without it, for the
// callers that add their own (application/image_loader.c prefixes the volume
// itself). The name is short enough to be a valid 8.3 name on its own, since
// FatFs is built here with FF_USE_LFN = 0.
#define IMAGE_SAVER_VOLUME        "0:"
#define IMAGE_SAVER_DIRECTORY_PATH "/FLIR"
#define IMAGE_SAVER_DIRECTORY     IMAGE_SAVER_VOLUME IMAGE_SAVER_DIRECTORY_PATH
#define IMAGE_SAVER_NAME_PREFIX   "FLIR"
#define IMAGE_SAVER_NAME_DIGITS   4u
#define IMAGE_SAVER_MAX_INDEX     9999u

// Encodes `width` x `height` packed RGB888 pixels starting at `rgb888` as a
// PNG and writes it to the SD card as /FLIR/FLIRnnnn.PNG, creating the
// directory and choosing nnnn as described in the file header.
//
// `rgb888` may point into uncached DDR2 (the GLCD Layer 0 buffers and the
// still capture's own copy both do) -- the encoder just reads it more slowly
// there, which is a small share of the total encode time.
//
// On success, copies the bare filename (no directory) into `nameOut` if it is
// non-NULL, so a caller can show the user what was written. Returns false and
// prints the reason to the console on any failure: no card mounted, the LVGL
// heap unable to hold the encode, a full or write-protected volume, or 10,000
// images already in the directory.
//
// Blocking, and not quick: a full-screen encode plus the card write runs into
// hundreds of milliseconds. The watchdog is kicked across the slow parts and
// held off entirely across the encode, which lodepng gives no way to kick
// from -- see the comment on that call.
bool ImageSaver_SaveRGB888ToSD(const void *rgb888, uint32_t width, uint32_t height,
        char *nameOut, size_t nameOutSize);

#ifdef __cplusplus
}
#endif

#endif /* IMAGE_SAVER_H */

/* *****************************************************************************
 End of File
 */
