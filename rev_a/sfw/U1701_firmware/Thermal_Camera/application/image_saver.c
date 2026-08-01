/*******************************************************************************
  PNG Image Saver

  File Name:
    image_saver.c

  Summary:
    RGB888-frame-buffer-to-PNG-on-SD writer. See image_saver.h for the naming
    scheme and the note on which half of lodepng is patched.
*******************************************************************************/

#include <xc.h>
#include <stdio.h>

#include "application/image_saver.h"
#include "core/device_control.h"
#include "core/watchdog_timer.h"
#include "gui/lvgl/lvgl.h"
#include "gui/lvgl/src/libs/lodepng/lodepng.h"
#include "sdhc/fatfs/ff.h"
#include "sdhc/sd_fileio.h"
#include "usb_uart/terminal_control.h"

// Where the images go, and how their names are built. The directory name is
// short enough to be a valid 8.3 name on its own (FatFs has no LFN here).
#define IMAGE_SAVER_DIRECTORY     "0:/FLIR"
#define IMAGE_SAVER_NAME_PREFIX   "FLIR"
#define IMAGE_SAVER_NAME_DIGITS   4u
#define IMAGE_SAVER_MAX_INDEX     9999u

// FatFs keeps a 512-byte sector window inside FIL, and DIR is not small
// either -- static rather than on the caller's stack, same as the FIL in
// image_loader.c and sd_fileio.c.
static FIL saveFile;
static DIR saveDir;
static FILINFO saveInfo;

// Parses "FLIRnnnn.PNG" and returns nnnn, or 0 for anything that isn't one of
// this module's own files (0 is never issued -- numbering starts at 1 -- so
// it doubles as "no match" without a separate out-parameter). FatFs with
// FF_USE_LFN = 0 reports 8.3 names already upper-cased, but the comparison is
// done case-insensitively anyway so a card written by some other tool can't
// quietly produce a duplicate number.
// ASCII upcase. Hand-rolled rather than toupper()/strcasecmp(): the former
// drags in locale tables and the latter is POSIX, not C, so neither is worth
// depending on for four characters of filename.
static char ImageSaverUpcase(char c)
{
    return ((c >= 'a') && (c <= 'z')) ? (char)(c - 'a' + 'A') : c;
}

static bool ImageSaverMatchesIgnoringCase(const char *name, const char *pattern)
{
    uint32_t i;

    for (i = 0; pattern[i] != '\0'; i++)
    {
        if (ImageSaverUpcase(name[i]) != pattern[i]) return false;
    }

    return (name[i] == '\0');
}

static uint32_t ImageSaverParseIndex(const char *name)
{
    static const char prefix[] = IMAGE_SAVER_NAME_PREFIX;
    const uint32_t prefixLength = (uint32_t)(sizeof(prefix) - 1u);
    uint32_t index = 0;
    uint32_t digit;

    for (digit = 0; digit < prefixLength; digit++)
    {
        if (ImageSaverUpcase(name[digit]) != prefix[digit]) return 0;
    }

    for (digit = 0; digit < IMAGE_SAVER_NAME_DIGITS; digit++)
    {
        char c = name[prefixLength + digit];

        if ((c < '0') || (c > '9')) return 0;

        index = (index * 10u) + (uint32_t)(c - '0');
    }

    if (!ImageSaverMatchesIgnoringCase(&name[prefixLength + IMAGE_SAVER_NAME_DIGITS], ".PNG"))
    {
        return 0;
    }

    return index;
}

// Highest FLIRnnnn.PNG number already in the directory, or 0 if there are
// none. Writes false to *ok if the directory could not be read at all, which
// the caller must treat as a failure rather than as "empty" -- starting over
// at 1 against an unreadable directory would overwrite existing images.
static uint32_t ImageSaverHighestIndex(bool *ok)
{
    uint32_t highest = 0;
    FRESULT result;

    *ok = false;

    result = f_opendir(&saveDir, IMAGE_SAVER_DIRECTORY);
    if (result != FR_OK) return 0;

    for (;;)
    {
        uint32_t index;

        result = f_readdir(&saveDir, &saveInfo);

        // An empty name is FatFs's end-of-directory marker
        if ((result != FR_OK) || (saveInfo.fname[0] == '\0')) break;

        if ((saveInfo.fattrib & AM_DIR) != 0) continue;

        index = ImageSaverParseIndex(saveInfo.fname);
        if (index > highest) highest = index;
    }

    f_closedir(&saveDir);

    *ok = (result == FR_OK);

    return highest;
}

bool ImageSaver_SaveRGB888ToSD(const void *rgb888, uint32_t width, uint32_t height,
        char *nameOut, size_t nameOutSize)
{
    char name[IMAGE_SAVER_NAME_MAX];
    char path[sizeof(IMAGE_SAVER_DIRECTORY) + IMAGE_SAVER_NAME_MAX + 1u];
    unsigned char *png = NULL;
    size_t pngSize = 0;
    unsigned int encodeError;
    uint32_t index;
    bool scanned;
    FRESULT result;
    UINT written = 0;
    uint32_t encodeStartTicks;

    if ((rgb888 == NULL) || (width == 0) || (height == 0)) return false;

    if (!SDFileIO_IsMounted())
    {
        terminalTextAttributes(YELLOW_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("No SD card mounted -- image not saved\r\n");
        terminalTextAttributesReset();
        return false;
    }

    // Create the directory if this is the first image on this card. FR_EXIST
    // is the normal case and not an error.
    result = f_mkdir(IMAGE_SAVER_DIRECTORY);
    if ((result != FR_OK) && (result != FR_EXIST))
    {
        terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("Could not create %s: FRESULT %d\r\n", IMAGE_SAVER_DIRECTORY, (int)result);
        terminalTextAttributesReset();
        return false;
    }

    index = ImageSaverHighestIndex(&scanned) + 1u;
    if (!scanned)
    {
        terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("Could not read %s to pick the next image number\r\n", IMAGE_SAVER_DIRECTORY);
        terminalTextAttributesReset();
        return false;
    }

    if (index > IMAGE_SAVER_MAX_INDEX)
    {
        terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("%s already holds %s%04lu.PNG -- delete some images to keep saving\r\n",
                IMAGE_SAVER_DIRECTORY, IMAGE_SAVER_NAME_PREFIX,
                (unsigned long)IMAGE_SAVER_MAX_INDEX);
        terminalTextAttributesReset();
        return false;
    }

    snprintf(name, sizeof(name), "%s%04lu.PNG", IMAGE_SAVER_NAME_PREFIX, (unsigned long)index);
    snprintf(path, sizeof(path), "%s/%s", IMAGE_SAVER_DIRECTORY, name);

    // Full encode pass over the image: filter, optional palettization, and
    // deflate. Hundreds of milliseconds, so the watchdog is kicked on both
    // sides rather than assuming it fits in the remaining window (same
    // treatment as the decode in image_loader.c).
    //
    // LCT_RGB/8 describes the INPUT. What lands in the file is chosen by the
    // encoder's auto_convert, which palettizes a <=256-color image -- see
    // image_saver.h.
    kickTheDog();
    encodeStartTicks = _CP0_GET_COUNT();
    encodeError = lodepng_encode_memory(&png, &pngSize, (const unsigned char *)rgb888,
            (unsigned int)width, (unsigned int)height, LCT_RGB, 8);
    kickTheDog();

    if (encodeError != 0)
    {
        // lodepng frees its own working buffers on failure, but *out can
        // still hold a partial allocation
        if (png != NULL) lv_free(png);
        terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("PNG encode failed: %s (lodepng error %u)\r\n",
                lodepng_error_text(encodeError), encodeError);
        terminalTextAttributesReset();
        return false;
    }

    if ((png == NULL) || (pngSize == 0))
    {
        if (png != NULL) lv_free(png);
        terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("PNG encode produced nothing -- see 'Peripheral Status? GUI' for heap state\r\n");
        terminalTextAttributesReset();
        return false;
    }

    result = f_open(&saveFile, path, FA_WRITE | FA_CREATE_NEW);
    if (result != FR_OK)
    {
        // FA_CREATE_NEW rather than FA_CREATE_ALWAYS: the number was picked
        // to be unused, so a collision here means the directory scan and the
        // card disagree, and silently overwriting someone's image is the one
        // outcome worth refusing.
        lv_free(png);
        terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("Could not create %s: FRESULT %d\r\n", path, (int)result);
        terminalTextAttributesReset();
        return false;
    }

    result = f_write(&saveFile, png, (UINT)pngSize, &written);
    f_close(&saveFile);
    kickTheDog();

    lv_free(png);

    if ((result != FR_OK) || (written != (UINT)pngSize))
    {
        terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("Only %lu of %lu bytes of %s reached the card\r\n",
                (unsigned long)written, (unsigned long)pngSize, path);
        terminalTextAttributesReset();

        // A truncated PNG is worse than none -- it looks like a real image
        // until something tries to decode it
        f_unlink(path);
        return false;
    }

    if ((nameOut != NULL) && (nameOutSize > 0))
    {
        snprintf(nameOut, nameOutSize, "%s", name);
    }

    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("Saved %s (%lux%lu, %lu byte PNG, encoded in %lu ms)\r\n",
            path, (unsigned long)width, (unsigned long)height, (unsigned long)pngSize,
            (unsigned long)(((uint64_t)(_CP0_GET_COUNT() - encodeStartTicks) * 2000u) / SYSCLK_INT));
    terminalTextAttributesReset();

    return true;
}
