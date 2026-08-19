/* ************************************************************************** */
/** Saved Image Catalog

  @File Name
    image_catalog.c

  @Summary
    See image_catalog.h for what a scan is a snapshot of and why the table
    is capped.
 */
/* ************************************************************************** */

#include <stdio.h>
#include <string.h>

#include "application/image_catalog.h"
#include "core/watchdog_timer.h"
#include "sdhc/fatfs/ff.h"
#include "sdhc/sd_fileio.h"
#include "usb_uart/terminal_control.h"

// FatFs's DIR and FILINFO are both large enough to be worth keeping off the
// caller's stack -- static here, exactly as image_saver.c keeps its own pair.
// Nothing in this firmware walks two directories at once.
static DIR catalogDir;
static FILINFO catalogInfo;

static IMAGE_CATALOG_ENTRY catalogEntries[IMAGE_CATALOG_MAX_ENTRIES];
static uint32_t catalogCount = 0;
static uint32_t catalogFound = 0;

// ASCII upcase and a case-insensitive suffix test, hand-rolled for the same
// reason image_saver.c hand-rolls its own: toupper() drags in locale tables
// and strcasecmp() is POSIX rather than C, and neither is worth depending on
// to check four characters of filename. FatFs with FF_USE_LFN = 0 hands back
// 8.3 names already upper-cased, but a card written by some other tool is
// still checked properly rather than being silently skipped.
static char ImageCatalogUpcase(char c)
{
    return ((c >= 'a') && (c <= 'z')) ? (char)(c - 'a' + 'A') : c;
}

static bool ImageCatalogIsPNG(const char *name)
{
    static const char extension[] = ".PNG";
    const size_t extensionLength = sizeof(extension) - 1u;
    size_t length = strlen(name);
    size_t i;

    if (length <= extensionLength) return false;

    for (i = 0; i < extensionLength; i++)
    {
        if (ImageCatalogUpcase(name[length - extensionLength + i]) != extension[i])
        {
            return false;
        }
    }

    return true;
}

// Insert one directory entry into the table, keeping it sorted newest first.
//
// The names image_saver.c issues are fixed-width and zero-padded
// (FLIR0007.PNG), so a plain descending strcmp() is descending capture
// order -- no number has to be parsed back out, and a file some other tool
// dropped in the directory still lands somewhere sensible rather than being
// dropped or sorted to a random place.
//
// Once the table is full the entry is only taken if it sorts ABOVE the last
// one, and the last one is pushed off the end. That is what makes the cap
// keep the NEWEST IMAGE_CATALOG_MAX_ENTRIES rather than whichever ones FatFs
// happened to report first.
static void ImageCatalogInsert(const char *name, uint32_t size_bytes)
{
    uint32_t position;
    uint32_t i;

    for (position = 0; position < catalogCount; position++)
    {
        if (strcmp(name, catalogEntries[position].name) > 0) break;
    }

    if (position >= IMAGE_CATALOG_MAX_ENTRIES) return;   // older than everything kept

    // Shift the tail down one, dropping the oldest if the table is already
    // full. Counting downwards so each element is read before it is written.
    if (catalogCount < IMAGE_CATALOG_MAX_ENTRIES) catalogCount++;

    for (i = catalogCount - 1u; i > position; i--)
    {
        catalogEntries[i] = catalogEntries[i - 1u];
    }

    snprintf(catalogEntries[position].name, sizeof(catalogEntries[position].name),
            "%s", name);
    catalogEntries[position].size_bytes = size_bytes;
}

bool ImageCatalog_Scan(void)
{
    FRESULT result;

    // Empty rather than stale on every failure path below: a list that still
    // shows the last card's images after this one was pulled is worse than
    // an empty one, because tapping a row would then try to open a file that
    // is not there.
    catalogCount = 0;
    catalogFound = 0;

    if (!SDFileIO_IsMounted()) return false;

    result = f_opendir(&catalogDir, IMAGE_SAVER_DIRECTORY);

    if (result != FR_OK)
    {
        // No directory means no image has ever been saved on this card, which
        // is an ordinary state and not something to report as a fault --
        // image_saver.c creates it on the first save.
        if ((result == FR_NO_PATH) || (result == FR_NO_FILE)) return true;

        terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("Could not open %s to list saved images: FRESULT %d\r\n",
                IMAGE_SAVER_DIRECTORY, (int)result);
        terminalTextAttributesReset();
        return false;
    }

    for (;;)
    {
        // A directory of several hundred entries is several hundred SD
        // reads, so the walk is not assumed to fit in the remaining watchdog
        // window -- same treatment image_loader.c gives its decode.
        kickTheDog();

        result = f_readdir(&catalogDir, &catalogInfo);

        // An empty name is FatFs's end-of-directory marker
        if ((result != FR_OK) || (catalogInfo.fname[0] == '\0')) break;

        if ((catalogInfo.fattrib & AM_DIR) != 0) continue;
        if (!ImageCatalogIsPNG(catalogInfo.fname)) continue;

        catalogFound++;

        ImageCatalogInsert(catalogInfo.fname, (uint32_t)catalogInfo.fsize);
    }

    f_closedir(&catalogDir);

    if (result != FR_OK)
    {
        catalogCount = 0;
        catalogFound = 0;

        terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("Reading %s stopped early: FRESULT %d\r\n",
                IMAGE_SAVER_DIRECTORY, (int)result);
        terminalTextAttributesReset();
        return false;
    }

    return true;
}

uint32_t ImageCatalog_GetCount(void)
{
    return catalogCount;
}

uint32_t ImageCatalog_GetFoundCount(void)
{
    return catalogFound;
}

const IMAGE_CATALOG_ENTRY *ImageCatalog_GetEntry(uint32_t index)
{
    if (index >= catalogCount) return NULL;

    return &catalogEntries[index];
}

bool ImageCatalog_BuildPath(uint32_t index, char *out, size_t size)
{
    if ((out == NULL) || (size < IMAGE_CATALOG_PATH_MAX)) return false;
    if (index >= catalogCount) return false;

    snprintf(out, size, "%s/%s", IMAGE_SAVER_DIRECTORY_PATH, catalogEntries[index].name);

    return true;
}

bool ImageCatalog_Delete(uint32_t index)
{
    char path[sizeof(IMAGE_SAVER_VOLUME) + IMAGE_CATALOG_PATH_MAX];
    uint32_t i;

    if (index >= catalogCount) return false;

    snprintf(path, sizeof(path), "%s%s/%s", IMAGE_SAVER_VOLUME,
            IMAGE_SAVER_DIRECTORY_PATH, catalogEntries[index].name);

    if (!SDFileIO_DeleteFile(path))
    {
        terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("Could not delete %s\r\n", path);
        terminalTextAttributesReset();
        return false;
    }

    // Close the gap rather than re-scanning: the caller is a GUI that has
    // just been told the delete succeeded and is about to redraw, and a
    // directory walk per delete would put an avoidable second of card
    // traffic behind every tap.
    for (i = index; (i + 1u) < catalogCount; i++)
    {
        catalogEntries[i] = catalogEntries[i + 1u];
    }

    catalogCount--;

    // Only meaningful when the card held no more than the table could keep;
    // past the cap the true total is no longer knowable without a re-scan,
    // and the floor here keeps it from underflowing in that case.
    if (catalogFound > 0u) catalogFound--;

    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("Deleted %s\r\n", path);
    terminalTextAttributesReset();

    return true;
}

/* *****************************************************************************
 End of File
 */
