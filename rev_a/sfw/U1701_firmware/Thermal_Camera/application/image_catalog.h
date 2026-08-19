/* ************************************************************************** */
/** Saved Image Catalog

  @File Name
    image_catalog.h

  @Summary
    Reads back the directory application/image_saver.c writes into: what
    thermal stills are on the card, and the one operation that removes them.

  @Description
    The read side of the pair. image_saver.c names files FLIRnnnn.PNG in
    IMAGE_SAVER_DIRECTORY and never looks at them again; this module lists
    that directory, so the GUI's Saved Images screen
    (gui/screens/screen_saved_images.c) can show what was taken and delete
    what wasn't wanted. Both modules share the directory and naming macros
    out of image_saver.h rather than each carrying a copy -- if those two
    ever disagreed, the screen would list an empty directory next to a card
    filling up with images.

    A scan is a snapshot, not a live view: ImageCatalog_Scan() walks the
    directory once into the fixed table below, and every other call reads
    that table without touching the card. That is what lets the GUI ask for
    a filename on every redraw. It also means the table goes stale the
    moment anything else writes to the card (a shutter press, the USB host),
    so the caller re-scans on the events that matter to it -- entering the
    screen, a mount/unmount, its own delete.

    Ordering is newest first: the names are fixed-width and zero-padded, so
    a plain descending string compare is descending capture order, and the
    image the user just took is the first row of the list rather than the
    ten-thousandth.

    Capacity: IMAGE_CATALOG_MAX_ENTRIES names, which is the bound on what
    this costs in RAM (the table is static -- 256 x 20 bytes) and on what
    the GUI has to build widgets for. A card holding more than that is not
    an error: the newest IMAGE_CATALOG_MAX_ENTRIES are kept and
    ImageCatalog_GetFoundCount() reports the true total, so the screen can
    say so rather than quietly lying about how many images exist.

    Blocking: a scan is a directory walk over SD, and a delete is a FatFs
    metadata write. Call both from thread/command context, never from an
    ISR -- same rule as image_loader.c and image_saver.c.
 */
/* ************************************************************************** */

#ifndef IMAGE_CATALOG_H
#define IMAGE_CATALOG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "application/image_saver.h"

#ifdef __cplusplus
extern "C" {
#endif

// How many names a scan keeps. See the capacity note in the file header --
// raising it costs 20 bytes of static RAM each, plus the GUI widgets the
// Saved Images screen builds per row.
#define IMAGE_CATALOG_MAX_ENTRIES  256u

// Longest path this module builds: the directory, a separator, an 8.3 name
// and its terminator. Volume-relative (see ImageCatalog_BuildPath()).
#define IMAGE_CATALOG_PATH_MAX \
        (sizeof(IMAGE_SAVER_DIRECTORY_PATH) + 1u + IMAGE_SAVER_NAME_MAX)

// One saved image. `name` is the bare 8.3 filename with no directory, as
// FatFs reported it.
typedef struct
{
    char name[IMAGE_SAVER_NAME_MAX];
    uint32_t size_bytes;
} IMAGE_CATALOG_ENTRY;

// Walks IMAGE_SAVER_DIRECTORY and rebuilds the table, newest first.
// Returns false -- leaving the table EMPTY rather than stale -- if no volume
// is mounted or the directory could not be read. A directory that does not
// exist yet (no image has ever been saved on this card) is not a failure:
// it reports true with a count of zero.
bool ImageCatalog_Scan(void);

// How many entries the last scan kept, i.e. the valid index range for
// everything below. Zero before the first scan.
uint32_t ImageCatalog_GetCount(void);

// How many images the last scan actually found in the directory. Larger than
// ImageCatalog_GetCount() only when the card holds more than
// IMAGE_CATALOG_MAX_ENTRIES, in which case the newest that many were kept.
uint32_t ImageCatalog_GetFoundCount(void);

// The entry at `index`, or NULL if it is out of range. Points into the
// static table, so it is invalidated by the next scan or delete.
const IMAGE_CATALOG_ENTRY *ImageCatalog_GetEntry(uint32_t index);

// Writes the volume-RELATIVE path of `index` ("/FLIR/FLIRnnnn.PNG") into
// `out`. That is the form application/image_loader.c takes -- it prefixes
// the volume itself -- and this module prefixes IMAGE_SAVER_VOLUME onto it
// for its own FatFs calls. Returns false on a bad index or too small a
// buffer (IMAGE_CATALOG_PATH_MAX is always enough).
bool ImageCatalog_BuildPath(uint32_t index, char *out, size_t size);

// Deletes `index` from the card and removes it from the table, so the
// indices above it shift down by one and the count drops -- no re-scan
// needed for the caller to redraw. Returns false, changing nothing, on a bad
// index or if FatFs refused the unlink (card removed, file read-only).
bool ImageCatalog_Delete(uint32_t index);

#ifdef __cplusplus
}
#endif

#endif /* IMAGE_CATALOG_H */

/* *****************************************************************************
 End of File
 */
