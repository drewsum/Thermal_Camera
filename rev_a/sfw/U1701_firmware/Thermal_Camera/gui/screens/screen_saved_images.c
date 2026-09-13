/*******************************************************************************
  Saved Images GUI Screen

  File Name:
    screen_saved_images.c

  Summary:
    See screen_saved_images.h for the layout, how the full-screen view takes
    its tap, why the scan and the previews run off a timer rather than out of
    the refresh, and why the list is a small pool of recycled rows over a
    DDR2 thumbnail cache rather than one widget tree per image.
*******************************************************************************/

#include <stdio.h>
#include <string.h>

#include "gui/screens/screen_saved_images.h"
#include "gui/screens/screen_common.h"

#include "gui/gui.h"
#include "gui/lvgl/lvgl.h"
#include "application/image_catalog.h"
#include "application/image_loader.h"
#include "application/still_capture.h"
#include "core/ddr2.h"
#include "sdhc/sd_fileio.h"

// gui/lv_conf.h keeps the widget set trimmed to what the screens actually
// use, and this is the only screen that draws a bitmap. Catch it being
// turned back off here rather than as a pile of implicit-declaration
// warnings and an undefined reference at link time -- same guard, same
// reason, as the slider check in screen_brightness.c.
#if !LV_USE_IMAGE
#error "LV_USE_IMAGE (gui/lv_conf.h) must be 1 for gui/screens/screen_saved_images.c"
#endif

// The previews are handed to LVGL as LV_COLOR_FORMAT_RGB888 buffers, which
// the software renderer only knows how to blit when that format is compiled
// in. Without this the previews would silently draw as nothing.
#if !LV_DRAW_SW_SUPPORT_RGB888
#error "LV_DRAW_SW_SUPPORT_RGB888 (gui/lv_conf.h) must be 1 for the saved-image previews"
#endif

// Body panel geometry, matching the menu and SD card screens so the three
// read as the same instrument.
#define SAVED_PANEL_INSET_PX        6
#define SAVED_PANEL_PAD_PX          6

// Right-hand lane kept clear for the scrollbar the default theme draws
// inside the panel's right edge -- see system_screen.c, same reasoning.
#define SAVED_SCROLLBAR_LANE_PX     14

// Row geometry. Taller than the menu's rows because a row here has to hold
// a preview; the pitch is what the visible-row and row-placement arithmetic
// below works in -- a row for catalog index i always sits at i * pitch.
#define SAVED_ROW_HEIGHT_PX         34
#define SAVED_ROW_GAP_PX            4
#define SAVED_ROW_PITCH_PX          (SAVED_ROW_HEIGHT_PX + SAVED_ROW_GAP_PX)

// Preview size, and where the rest of the row sits relative to it. 4:3 like
// the 320x240 source, so the picture is not stretched.
#define SAVED_THUMB_WIDTH_PX        40
#define SAVED_THUMB_HEIGHT_PX       30
#define SAVED_THUMB_STRIDE_BYTES    (SAVED_THUMB_WIDTH_PX * 3)

#define SAVED_NAME_X_PX             (SAVED_THUMB_WIDTH_PX + 6)

// The bin at the right-hand end of every row, and the gap the file size
// keeps from it.
#define SAVED_TRASH_WIDTH_PX        30
#define SAVED_TRASH_HEIGHT_PX       30
#define SAVED_SIZE_GAP_PX           6

#define SAVED_THUMB_PIXEL_BYTES     (SAVED_THUMB_WIDTH_PX * SAVED_THUMB_HEIGHT_PX * 3)

// How many row widgets exist. The panel's content box is 144px tall, so at
// most five rows (three whole, two partial) are ever on screen at once; the
// pool covers that plus one row above and a couple below, so a row being
// scrolled into view is already built and labelled by the time it arrives.
// This is the whole cost of the list in widgets, however many images the
// card holds -- see the row pool note in screen_saved_images.h.
#define SAVED_ROW_POOL_COUNT        8u

// A pool row that is not currently standing in for any catalog entry
#define SAVED_ROW_UNBOUND           UINT32_MAX

// A catalog entry whose preview has no cache cell yet (only ever seen between
// the two passes of ScreenSavedImagesSyncThumbs())
#define SAVED_THUMB_NO_CELL         UINT16_MAX

// How often the worker timer runs. One scan or one preview decode per tick
// -- see screen_saved_images.h. Fast enough that the previews fill in as
// quickly as the decoder can produce them, slow enough to leave the main
// loop and the FLIR video path room between them.
#define SAVED_WORKER_INTERVAL_MS    30

// How long the touchscreen has to have been left alone before a preview is
// decoded. A decode blocks the main loop, and a press or a flick that lands
// inside one is read late -- which is what made this screen feel laggy.
// Long enough to cover the gap between the flicks of someone paging down a
// long list, short enough that the previews still seem to arrive on their
// own.
#define SAVED_DECODE_SETTLE_MS      250

// How far past the visible rows, in each direction, previews are decoded
// ahead of being needed once the visible ones are done. About a page, so the
// next scroll usually lands on rows that are already drawn; bounded so the
// screen goes quiet soon after it is opened, instead of working through the
// whole card with the main loop stalled one decode at a time.
#define SAVED_PREFETCH_ROWS         5u

// Colors, all already in use elsewhere in this GUI:
//   0xE04040  the save-image screen's failure red -- the bin, and Delete
//   0x808080  the home screen's secondary text -- file sizes
//   0x202020  a shade of the panel fill, for an empty preview frame
#define SAVED_DANGER_COLOR          0xE04040
#define SAVED_DIM_TEXT_COLOR        0x808080
#define SAVED_THUMB_EMPTY_COLOR     0x202020

// How dark the list goes behind the delete prompt. Enough that the prompt is
// unmistakably modal, light enough that the list is still recognizable
// underneath -- the question is about one of its rows.
#define SAVED_CONFIRM_DIM_OPACITY   LV_OPA_70

// Delete prompt geometry.
#define SAVED_CONFIRM_PANEL_W_PX    248
#define SAVED_CONFIRM_PANEL_H_PX    116
#define SAVED_CONFIRM_BUTTON_W_PX   100
#define SAVED_CONFIRM_BUTTON_H_PX   30
#define SAVED_CONFIRM_BUTTON_X_PX   54
#define SAVED_CONFIRM_BUTTON_Y_PX   28

// Screen_CreateButton() makes the chip's label first, so it is always child
// 0 -- for a row that label is the filename, and for the bin and the Delete
// button it is the glyph that gets recolored. Nothing else here reaches into
// an object by child index; a row's other parts are kept in SAVED_ROW.
#define SAVED_BUTTON_CHILD_LABEL    0

// The cache is one fixed cell per catalog entry, so a cell has to be able to
// hold a preview, and the whole cache has to stay inside the unreserved DDR2
// it was given -- clear of the still-capture image below it and of the end
// of the part. Sizes are spelled out from pixel geometry rather than taken
// from the *_SIZE macros, which carry casts #if cannot evaluate (the same
// reason as gui.c's heap guards).
#if SAVED_THUMB_PIXEL_BYTES > SCREEN_SAVED_IMAGES_THUMB_CELL_BYTES
    #error "A saved-image preview no longer fits one thumbnail cache cell (screen_saved_images.h)"
#endif

#if SCREEN_SAVED_IMAGES_THUMB_CACHE_OFFSET < \
        ((STILL_CAPTURE_RGB_ADDRESS - DDR2_KSEG1_BASE_ADDRESS) + (320 * 240 * 3))
    #error "The saved-image thumbnail cache (screen_saved_images.h) starts inside the still-capture image buffer"
#endif

#if (SCREEN_SAVED_IMAGES_THUMB_CACHE_OFFSET \
        + (IMAGE_CATALOG_MAX_ENTRIES * SCREEN_SAVED_IMAGES_THUMB_CELL_BYTES)) > DDR2_SIZE_BYTES
    #error "The saved-image thumbnail cache (screen_saved_images.h) runs off the end of DDR2"
#endif

// What a catalog entry's preview is doing. PENDING is where every entry new
// to the list starts, and the only state the worker acts on.
typedef enum
{
    SAVED_THUMB_PENDING = 0,   // no preview yet; decode it once it is near the view
    SAVED_THUMB_READY,         // its cell holds the decoded preview
    SAVED_THUMB_FAILED         // the decode failed; not retried until the screen is re-entered
} SAVED_THUMB_STATE;

// The preview of one catalog entry, kept in catalog order: thumbs[i] is
// entry i. The file's name, size and timestamp are copied in because they
// are what identify it once the catalog changes underneath -- indices shift
// the moment a row above is deleted, and a name alone is not enough either,
// since image_saver.c re-issues the newest image's number after it is
// deleted. See ScreenSavedImagesSyncThumbs().
typedef struct
{
    char name[IMAGE_SAVER_NAME_MAX];
    uint32_t size_bytes;
    uint32_t timestamp;
    uint16_t cell;      // which DDR2 cache cell holds the pixels
    uint8_t state;      // SAVED_THUMB_STATE
} SAVED_THUMB;

// One recycled row. `index` is the catalog entry it is currently drawn as,
// and the row's event callbacks read it from here rather than being handed
// an index when the row was built -- the same widgets stand for different
// images as the list scrolls.
typedef struct
{
    lv_obj_t *row;      // the tappable chip; its label is `name`
    lv_obj_t *name;
    lv_obj_t *thumb;    // hidden while there is no preview to show -- see ScreenSavedImagesShowThumb()
    lv_obj_t *size;
    uint32_t index;     // catalog index, or SAVED_ROW_UNBOUND (and hidden)
} SAVED_ROW;

static SCREEN_HEADER header;

static lv_obj_t *panel = NULL;
static lv_obj_t *empty_label = NULL;

// A 1x1, style-less object parked at the bottom edge of the last row. With
// only SAVED_ROW_POOL_COUNT rows in the panel, this is what gives it the
// scroll extent of the whole list -- LVGL sizes the scroll range from where
// the children are, and the rows alone only reach as far as the view.
static lv_obj_t *list_end = NULL;

static SAVED_ROW rows[SAVED_ROW_POOL_COUNT];

static lv_obj_t *confirm_overlay = NULL;
static lv_obj_t *confirm_name_label = NULL;

// The full-screen transparent object every tap lands on while an image is up
// on Layer 2 -- see the catcher note in screen_saved_images.h.
static lv_obj_t *viewer_catcher = NULL;
static bool viewer_showing = false;

// The preview cache. thumbs[] follows the catalog; the descriptors are one
// per DDR2 cell and never move, because an lv_image keeps the pointer it was
// given rather than a copy. All of it is .bss, so every entry starts empty
// at boot and whatever DDR2 held before a reset is never shown.
static SAVED_THUMB thumbs[IMAGE_CATALOG_MAX_ENTRIES];
static SAVED_THUMB thumbs_prev[IMAGE_CATALOG_MAX_ENTRIES];   // scratch for the sync
static uint32_t thumb_count = 0;
static lv_image_dsc_t thumb_dsc[IMAGE_CATALOG_MAX_ENTRIES];

// Which row the delete prompt is currently asking about. Only meaningful
// while confirm_overlay is visible.
static uint32_t confirm_row = 0;

// Set to ask the worker timer to re-read the directory and redraw the list
// -- by a row that found its file gone, and by the card coming or going. Not
// done inline because a scan is blocking card traffic, and the row asking is
// the one whose own tap is still being dispatched.
static bool rescan_pending = false;

// When the touchscreen was last seen in use (pressed, scrolling, or the list
// still animating) -- see SAVED_DECODE_SETTLE_MS
static uint32_t last_busy_tick = 0;

// Whether the screen was active on the previous tick, and what the card was
// doing -- the two edges that make the list stale (see the worker).
static bool worker_was_active = false;
static bool worker_last_mounted = false;

// *****************************************************************************
// Section: Preview cache
// *****************************************************************************

// Where cell `cell` of the cache lives: the DDR2 memory the decoder writes
// into, and that the cell's descriptor points LVGL at
static uint8_t *ScreenSavedImagesCellPixels(uint32_t cell)
{
    return (uint8_t *)(uintptr_t)(SCREEN_SAVED_IMAGES_THUMB_CACHE_ADDRESS
            + (cell * SCREEN_SAVED_IMAGES_THUMB_CELL_BYTES));
}

// What the row for catalog entry `index` should be showing: its preview if
// one has been decoded, otherwise nothing, which leaves the row's dark frame.
static const lv_image_dsc_t *ScreenSavedImagesThumbSource(uint32_t index)
{
    if (index >= thumb_count) return NULL;
    if (thumbs[index].state != SAVED_THUMB_READY) return NULL;
    if (thumbs[index].cell >= IMAGE_CATALOG_MAX_ENTRIES) return NULL;

    return &thumb_dsc[thumbs[index].cell];
}

// Re-lines thumbs[] up with the catalog after a scan or a delete, keeping
// every preview whose file is still there and handing the cells of the ones
// that are gone to the entries that are new.
//
// Both lists are sorted the same way -- descending by name, which is how
// image_catalog.c keeps its table and therefore how thumbs[] was left last
// time -- so this is one merge walk rather than a search per entry. An entry
// keeps its preview only if its size and timestamp match as well as its
// name: a different card, or a new capture issued a deleted image's number,
// is a different picture under the same name.
//
// There is a cell for every entry the catalog can hold, so nothing is ever
// evicted and the second pass can never run out of cells.
static void ScreenSavedImagesSyncThumbs(void)
{
    static bool cell_taken[IMAGE_CATALOG_MAX_ENTRIES];
    uint32_t count = ImageCatalog_GetCount();
    uint32_t prev_count = thumb_count;
    uint32_t old = 0;
    uint32_t next_cell = 0;
    uint32_t index;

    memcpy(thumbs_prev, thumbs, prev_count * sizeof(thumbs[0]));
    memset(cell_taken, 0, sizeof(cell_taken));

    for (index = 0; index < count; index++)
    {
        const IMAGE_CATALOG_ENTRY *entry = ImageCatalog_GetEntry(index);
        SAVED_THUMB *thumb = &thumbs[index];

        memcpy(thumb->name, entry->name, sizeof(thumb->name));
        thumb->size_bytes = entry->size_bytes;
        thumb->timestamp = entry->timestamp;
        thumb->cell = SAVED_THUMB_NO_CELL;
        thumb->state = SAVED_THUMB_PENDING;

        // Old entries that sort above this one are no longer on the card
        while ((old < prev_count) &&
               (strcmp(thumbs_prev[old].name, entry->name) > 0))
        {
            old++;
        }

        // Nothing old left, or nothing old by this name: new to the list
        if ((old >= prev_count) || (strcmp(thumbs_prev[old].name, entry->name) != 0)) continue;

        if ((thumbs_prev[old].size_bytes == entry->size_bytes) &&
            (thumbs_prev[old].timestamp == entry->timestamp) &&
            (thumbs_prev[old].cell < IMAGE_CATALOG_MAX_ENTRIES))
        {
            thumb->cell = thumbs_prev[old].cell;
            thumb->state = thumbs_prev[old].state;
            cell_taken[thumb->cell] = true;
        }

        old++;
    }

    for (index = 0; index < count; index++)
    {
        if (thumbs[index].cell != SAVED_THUMB_NO_CELL) continue;

        while ((next_cell < IMAGE_CATALOG_MAX_ENTRIES) && cell_taken[next_cell]) next_cell++;

        if (next_cell >= IMAGE_CATALOG_MAX_ENTRIES) break;   // cannot happen, see above

        thumbs[index].cell = (uint16_t)next_cell;
        cell_taken[next_cell] = true;
    }

    thumb_count = count;
}

// *****************************************************************************
// Section: Row list
// *****************************************************************************

// Formats a file size the way a row has room for. Sizes on this card run
// from a few KB to a few tens of KB, so KB with no decimal is both enough
// precision and short enough to sit next to the name.
static void ScreenSavedImagesFormatSize(char *text, size_t size, uint32_t bytes)
{
    if (bytes >= (1024u * 1024u))
    {
        snprintf(text, size, "%.1f MB", (double)bytes / (1024.0 * 1024.0));
    }
    else
    {
        // Round up rather than down, so a file that exists never reads "0 KB"
        snprintf(text, size, "%lu KB", (unsigned long)((bytes + 1023u) / 1024u));
    }
}

static void ScreenSavedImagesRowClicked(lv_event_t *event);
static void ScreenSavedImagesTrashClicked(lv_event_t *event);

// Builds one pool row: preview, name, size, bin -- empty and hidden, since
// it stands for no image until ScreenSavedImagesBindRows() gives it one. Its
// callbacks are handed the SAVED_ROW rather than an index; see that struct.
static bool ScreenSavedImagesCreateRow(SAVED_ROW *slot, int32_t width)
{
    lv_obj_t *frame;
    lv_obj_t *trash;

    slot->index = SAVED_ROW_UNBOUND;

    // The whole row is the tap target that opens the image -- the bin below
    // is a child of it, and children are hit-tested first, so the two do not
    // fight over the press.
    slot->row = Screen_CreateButton(panel, LV_ALIGN_TOP_LEFT, 0, 0,
            width, SAVED_ROW_HEIGHT_PX, "", ScreenSavedImagesRowClicked, slot);

    if (slot->row == NULL) return false;

    // Screen_CreateButton centers its label; this list wants the name beside
    // the preview.
    slot->name = lv_obj_get_child(slot->row, SAVED_BUTTON_CHILD_LABEL);
    lv_obj_align(slot->name, LV_ALIGN_LEFT_MID, SAVED_NAME_X_PX, 0);

    // --- Preview ----------------------------------------------------------
    // A dark frame, with the image drawn inside it once its preview has been
    // decoded -- the frame stands in for the picture until then so the row
    // does not look broken. Two objects rather than one lv_image carrying the
    // fill itself: an lv_image with no source logs "image source is NULL" on
    // every redraw, so the image is kept hidden instead, and something else
    // has to draw the frame while it is.
    frame = lv_obj_create(slot->row);
    if (frame == NULL) return false;

    lv_obj_remove_style_all(frame);
    lv_obj_set_size(frame, SAVED_THUMB_WIDTH_PX, SAVED_THUMB_HEIGHT_PX);
    lv_obj_align(frame, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_color(frame, lv_color_hex(SAVED_THUMB_EMPTY_COLOR), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(frame, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_remove_flag(frame, LV_OBJ_FLAG_SCROLLABLE);

    // Every child of the row would otherwise swallow the press before the row
    // saw it -- the same trap Screen_CreateButton() disarms for its own label
    lv_obj_remove_flag(frame, LV_OBJ_FLAG_CLICKABLE);

    slot->thumb = lv_image_create(frame);
    if (slot->thumb == NULL) return false;

    lv_obj_set_size(slot->thumb, SAVED_THUMB_WIDTH_PX, SAVED_THUMB_HEIGHT_PX);
    lv_obj_align(slot->thumb, LV_ALIGN_CENTER, 0, 0);
    lv_obj_remove_flag(slot->thumb, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(slot->thumb, LV_OBJ_FLAG_HIDDEN);

    // --- File size --------------------------------------------------------
    slot->size = Screen_CreateLabel(slot->row, &lv_font_montserrat_14, LV_ALIGN_RIGHT_MID,
            -(SAVED_TRASH_WIDTH_PX + SAVED_SIZE_GAP_PX), 0, "");

    if (slot->size == NULL) return false;

    lv_obj_set_style_text_color(slot->size, lv_color_hex(SAVED_DIM_TEXT_COLOR), LV_PART_MAIN);
    lv_obj_remove_flag(slot->size, LV_OBJ_FLAG_CLICKABLE);

    // --- Bin --------------------------------------------------------------
    // Clickable, unlike the two above: this is the one part of the row that
    // does something other than open the image.
    trash = Screen_CreateButton(slot->row, LV_ALIGN_RIGHT_MID, 0, 0,
            SAVED_TRASH_WIDTH_PX, SAVED_TRASH_HEIGHT_PX, LV_SYMBOL_TRASH,
            ScreenSavedImagesTrashClicked, slot);

    if (trash == NULL) return false;

    // Red, border and glyph both, because it is the only control on this
    // screen that destroys something
    lv_obj_set_style_border_color(trash, lv_color_hex(SAVED_DANGER_COLOR), LV_PART_MAIN);
    lv_obj_set_style_border_opa(trash, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_text_color(lv_obj_get_child(trash, SAVED_BUTTON_CHILD_LABEL),
            lv_color_hex(SAVED_DANGER_COLOR), LV_PART_MAIN);

    lv_obj_add_flag(slot->row, LV_OBJ_FLAG_HIDDEN);

    return true;
}

// The catalog indices of the rows currently scrolled into view, inclusive.
// Returns false when the list is empty. Worked out from the panel's scroll
// offset and the fixed row pitch rather than by asking each row whether it
// is visible -- the rows are laid out at explicit offsets, so this is exact.
static bool ScreenSavedImagesVisibleRange(uint32_t *first, uint32_t *last)
{
    int32_t scroll;
    int32_t view_height;

    if ((panel == NULL) || (thumb_count == 0)) return false;

    scroll = lv_obj_get_scroll_y(panel);
    if (scroll < 0) scroll = 0;   // over-scroll bounce at the top

    view_height = lv_obj_get_content_height(panel);

    *first = (uint32_t)(scroll / SAVED_ROW_PITCH_PX);
    *last = (uint32_t)((scroll + view_height) / SAVED_ROW_PITCH_PX);

    if (*first >= thumb_count) return false;
    if (*last >= thumb_count) *last = thumb_count - 1u;

    return true;
}

// Puts entry `index`'s preview on `row`, or hides the image (leaving just its
// frame) if there is no preview yet. Never hands the image a NULL source to
// draw -- see the preview note in ScreenSavedImagesCreateRow().
static void ScreenSavedImagesShowThumb(SAVED_ROW *row, uint32_t index)
{
    const lv_image_dsc_t *src = ScreenSavedImagesThumbSource(index);

    if (src == NULL)
    {
        if (!lv_obj_has_flag(row->thumb, LV_OBJ_FLAG_HIDDEN))
        {
            lv_obj_add_flag(row->thumb, LV_OBJ_FLAG_HIDDEN);
        }

        return;
    }

    lv_image_set_src(row->thumb, src);

    if (lv_obj_has_flag(row->thumb, LV_OBJ_FLAG_HIDDEN))
    {
        lv_obj_remove_flag(row->thumb, LV_OBJ_FLAG_HIDDEN);
    }
}

// Turns pool row `row` into the row for catalog entry `index`: moves it to
// that entry's place in the list and relabels it.
static void ScreenSavedImagesFillRow(SAVED_ROW *row, uint32_t index)
{
    const IMAGE_CATALOG_ENTRY *entry = ImageCatalog_GetEntry(index);
    char size_text[16];

    if (entry == NULL) return;

    row->index = index;

    lv_obj_set_y(row->row, (int32_t)(index * SAVED_ROW_PITCH_PX));
    lv_label_set_text(row->name, entry->name);

    ScreenSavedImagesFormatSize(size_text, sizeof(size_text), entry->size_bytes);
    lv_label_set_text(row->size, size_text);

    ScreenSavedImagesShowThumb(row, index);

    if (lv_obj_has_flag(row->row, LV_OBJ_FLAG_HIDDEN))
    {
        lv_obj_remove_flag(row->row, LV_OBJ_FLAG_HIDDEN);
    }
}

// Points the pool at whatever part of the list is scrolled into view.
//
// Runs on every LV_EVENT_SCROLL, so it does next to nothing until the view
// has moved a whole row: a row still inside the window keeps its binding
// untouched, and only the rows that fell out of it are relabelled for the
// entries coming in. The window starts one row above the first visible one,
// so a drag back up finds that row already built.
static void ScreenSavedImagesBindRows(void)
{
    uint32_t first, last;
    uint32_t start = 0;
    uint32_t end = 0;
    uint32_t index, r;

    if (panel == NULL) return;

    if (ScreenSavedImagesVisibleRange(&first, &last))
    {
        start = (first > 0u) ? (first - 1u) : 0u;

        // Near the end, slide the window back so the whole pool is still in
        // use rather than leaving rows idle past the last entry
        if (thumb_count <= SAVED_ROW_POOL_COUNT) start = 0u;
        else if (start > (thumb_count - SAVED_ROW_POOL_COUNT)) start = thumb_count - SAVED_ROW_POOL_COUNT;

        end = start + SAVED_ROW_POOL_COUNT;
        if (end > thumb_count) end = thumb_count;
    }

    for (r = 0; r < SAVED_ROW_POOL_COUNT; r++)
    {
        if ((rows[r].index != SAVED_ROW_UNBOUND) &&
            ((rows[r].index < start) || (rows[r].index >= end)))
        {
            rows[r].index = SAVED_ROW_UNBOUND;
        }
    }

    for (index = start; index < end; index++)
    {
        SAVED_ROW *free_row = NULL;
        bool bound = false;

        for (r = 0; r < SAVED_ROW_POOL_COUNT; r++)
        {
            if (rows[r].index == index)
            {
                bound = true;
                break;
            }

            if ((rows[r].index == SAVED_ROW_UNBOUND) && (free_row == NULL)) free_row = &rows[r];
        }

        if (bound) continue;

        // The window is never wider than the pool, so a free row is always
        // there; this only keeps a logic error from dereferencing NULL
        if (free_row == NULL) break;

        ScreenSavedImagesFillRow(free_row, index);
    }

    // Rows left over are hidden, not just emptied: a hidden object is left
    // out of the panel's scroll extent, where a stale one parked past the end
    // of a shrunken list would hold the scroll range open
    for (r = 0; r < SAVED_ROW_POOL_COUNT; r++)
    {
        if ((rows[r].index == SAVED_ROW_UNBOUND) &&
            !lv_obj_has_flag(rows[r].row, LV_OBJ_FLAG_HIDDEN))
        {
            lv_obj_add_flag(rows[r].row, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void ScreenSavedImagesPanelScrolled(lv_event_t *event)
{
    (void)event;

    ScreenSavedImagesBindRows();
}

// Redraws the list from the catalog as it stands now, after a scan or a
// delete. No widget is created or destroyed -- the row pool is re-pointed --
// so this costs the same however long the list is, and is safe to call from
// an event callback.
static void ScreenSavedImagesRebuild(void)
{
    int32_t scroll;
    int32_t scroll_max;
    uint32_t r;

    if (panel == NULL) return;

    // Where the reader had scrolled to. Deleting an image rebuilds the list,
    // and snapping back to the top every time would make clearing several
    // images from the middle of a long list unusable.
    scroll = lv_obj_get_scroll_y(panel);
    if (scroll < 0) scroll = 0;   // elastic over-scroll at the top

    ScreenSavedImagesSyncThumbs();

    // Every binding is stale now -- the entry a row names may have moved or
    // gone -- so all of them are dropped and the bind below starts over. The
    // rows are hidden too, so none is left holding the scroll extent past the
    // new end of the list while it is re-measured.
    for (r = 0; r < SAVED_ROW_POOL_COUNT; r++)
    {
        rows[r].index = SAVED_ROW_UNBOUND;
        lv_obj_add_flag(rows[r].row, LV_OBJ_FLAG_HIDDEN);
    }

    if (thumb_count > 0)
    {
        lv_obj_set_y(list_end, (int32_t)(thumb_count * SAVED_ROW_PITCH_PX)
                - SAVED_ROW_GAP_PX - 1);
        lv_obj_remove_flag(list_end, LV_OBJ_FLAG_HIDDEN);
    }
    else
    {
        lv_obj_add_flag(list_end, LV_OBJ_FLAG_HIDDEN);
    }

    // LVGL bounds a scroll against where the children currently ARE, and the
    // end marker above has so far only been told where it is going to be
    lv_obj_update_layout(panel);

    // Put the reader back where they were, but no further down than the list
    // now reaches -- it is usually one row shorter than it was, and scrolling
    // past the end would leave the panel showing nothing at all.
    scroll_max = (int32_t)(thumb_count * SAVED_ROW_PITCH_PX) - SAVED_ROW_GAP_PX
            - lv_obj_get_content_height(panel);

    if (scroll_max < 0) scroll_max = 0;
    if (scroll > scroll_max) scroll = scroll_max;

    lv_obj_scroll_to_y(panel, scroll, LV_ANIM_OFF);

    ScreenSavedImagesBindRows();

    // The empty state carries the reason there is nothing to show, since the
    // two reasons want different things done about them
    if (empty_label != NULL)
    {
        if (thumb_count > 0)
        {
            lv_obj_add_flag(empty_label, LV_OBJ_FLAG_HIDDEN);
        }
        else
        {
            lv_label_set_text(empty_label, SDFileIO_IsMounted()
                    ? "No saved images\n\nPress the shutter button\nto capture one"
                    : "No SD card");
            lv_obj_remove_flag(empty_label, LV_OBJ_FLAG_HIDDEN);
        }
    }

    ScreenSavedImages_Refresh();
}

// *****************************************************************************
// Section: Worker
// *****************************************************************************

// Whether the user is in the middle of doing something with the screen: a
// finger down, a scroll still coasting after the finger left, the list
// springing back from an over-scroll, or the screen still sliding in. Any
// blocking work started now would be felt as the list freezing under them.
static bool ScreenSavedImagesUserBusy(void)
{
    lv_indev_t *indev = NULL;

    while ((indev = lv_indev_get_next(indev)) != NULL)
    {
        if (lv_indev_get_state(indev) == LV_INDEV_STATE_PRESSED) return true;

        // Stays set through the throw that follows a flick, until it stops
        if (lv_indev_get_scroll_obj(indev) != NULL) return true;
    }

    if (lv_anim_get(panel, NULL) != NULL) return true;
    if (lv_anim_get(lv_obj_get_screen(panel), NULL) != NULL) return true;

    return false;
}

// Decodes the preview of catalog entry `index` if it still needs one, and
// shows it on the row currently drawn for that entry, if there is one.
// Returns true if a decode was attempted, whether or not it worked -- either
// way the tick's one piece of blocking work has been spent.
static bool ScreenSavedImagesDecodeThumb(uint32_t index)
{
    char path[IMAGE_CATALOG_PATH_MAX];
    SAVED_THUMB *thumb;
    uint32_t r;

    if (index >= thumb_count) return false;

    thumb = &thumbs[index];

    if (thumb->state != SAVED_THUMB_PENDING) return false;

    if ((thumb->cell >= IMAGE_CATALOG_MAX_ENTRIES) ||
        !ImageCatalog_BuildPath(index, path, sizeof(path)) ||
        !ImageLoader_DecodeThumbnail(IMAGE_MEDIA_SD_CARD, path,
                ScreenSavedImagesCellPixels(thumb->cell),
                SAVED_THUMB_WIDTH_PX, SAVED_THUMB_HEIGHT_PX))
    {
        // A file that will not decode is not going to start: mark it and
        // move on, or the worker would spend every tick on it forever.
        // The row keeps its empty frame.
        thumb->state = SAVED_THUMB_FAILED;
        return true;
    }

    thumb->state = SAVED_THUMB_READY;

    for (r = 0; r < SAVED_ROW_POOL_COUNT; r++)
    {
        if (rows[r].index == index)
        {
            ScreenSavedImagesShowThumb(&rows[r], index);
        }
    }

    return true;
}

// One preview decode: a visible row's first, top to bottom, and once those
// are all drawn, the rows just past the view, nearest first and below before
// above (the list is newest first, so reading it means scrolling down).
// Returns false once there is nothing left in reach to decode, which is the
// steady state.
static bool ScreenSavedImagesDecodeOnePreview(void)
{
    uint32_t first, last, index, distance;

    if (!ScreenSavedImagesVisibleRange(&first, &last)) return false;

    for (index = first; index <= last; index++)
    {
        if (ScreenSavedImagesDecodeThumb(index)) return true;
    }

    for (distance = 1u; distance <= SAVED_PREFETCH_ROWS; distance++)
    {
        if (ScreenSavedImagesDecodeThumb(last + distance)) return true;

        if ((first >= distance) && ScreenSavedImagesDecodeThumb(first - distance)) return true;
    }

    return false;
}

// One piece of blocking work per tick, and only while the list is the thing
// being looked at and is being left alone -- see screen_saved_images.h.
static void ScreenSavedImagesWorker(lv_timer_t *timer)
{
    bool mounted;
    uint32_t index;

    (void)timer;

    if (!GUI_IsScreenActive(GUI_SCREEN_SAVED_IMAGES))
    {
        worker_was_active = false;
        return;
    }

    // Arriving on the screen: whatever the catalog holds is from the last
    // visit, and the shutter button or a USB host may have changed the card
    // since. Rebuilding beats showing a list that is quietly wrong -- and it
    // is cheap, because the previews of every file still there are kept.
    if (!worker_was_active)
    {
        worker_was_active = true;
        worker_last_mounted = SDFileIO_IsMounted();
        rescan_pending = false;
        last_busy_tick = lv_tick_get();

        // A prompt left open when the screen was navigated away from is a
        // question about a row that no longer exists -- it does not come back
        // with the screen
        if (confirm_overlay != NULL) lv_obj_add_flag(confirm_overlay, LV_OBJ_FLAG_HIDDEN);

        // A preview that failed last visit gets another try each visit: the
        // likely causes (the LVGL heap briefly too fragmented for the decode,
        // a card read error) are not permanent
        for (index = 0; index < thumb_count; index++)
        {
            if (thumbs[index].state == SAVED_THUMB_FAILED) thumbs[index].state = SAVED_THUMB_PENDING;
        }

        ImageCatalog_Scan();
        ScreenSavedImagesRebuild();
        return;
    }

    // Nothing on this screen is visible while an image covers it, so there is
    // nothing worth decoding -- and a rebuild would pull the list out from
    // under the view the user came back to
    if (viewer_showing) return;

    // Likewise while the delete prompt is up, and for a sharper reason: the
    // prompt is a question about one row, held as its catalog index, and a
    // rescan renumbers those. A card inserted between the tap on the bin and
    // the tap on Delete would otherwise mean deleting a different file than
    // the one named on screen. Nothing here is urgent enough to risk that --
    // it all happens as soon as the question is answered.
    if ((confirm_overlay != NULL) &&
        !lv_obj_has_flag(confirm_overlay, LV_OBJ_FLAG_HIDDEN)) return;

    // And while a finger is on the glass or the list is still moving. This
    // also holds back the rescan below: it re-points the rows, and the row
    // under a finger must not turn into a different image before the tap
    // lands.
    if (ScreenSavedImagesUserBusy())
    {
        last_busy_tick = lv_tick_get();
        return;
    }

    mounted = SDFileIO_IsMounted();

    if (mounted != worker_last_mounted)
    {
        worker_last_mounted = mounted;
        rescan_pending = true;
    }

    if (rescan_pending)
    {
        rescan_pending = false;

        ImageCatalog_Scan();
        ScreenSavedImagesRebuild();
        return;
    }

    if (lv_tick_elaps(last_busy_tick) < SAVED_DECODE_SETTLE_MS) return;

    (void)ScreenSavedImagesDecodeOnePreview();
}

// *****************************************************************************
// Section: Events
// *****************************************************************************

void ScreenSavedImages_DismissViewer(void)
{
    if (!viewer_showing) return;

    viewer_showing = false;

    ImageLoader_Clear();

    if (viewer_catcher != NULL) lv_obj_add_flag(viewer_catcher, LV_OBJ_FLAG_HIDDEN);
}

// Opens the tapped image full-screen on GLCD Layer 2. Blocking -- a whole
// PNG decode -- which is why the row stays visibly pressed until it lands.
static void ScreenSavedImagesRowClicked(lv_event_t *event)
{
    const SAVED_ROW *row = lv_event_get_user_data(event);
    char path[IMAGE_CATALOG_PATH_MAX];

    if ((row == NULL) || (row->index == SAVED_ROW_UNBOUND)) return;

    if (!ImageCatalog_BuildPath(row->index, path, sizeof(path))) return;

    if (!ImageLoader_DisplayPNG(IMAGE_MEDIA_SD_CARD, path))
    {
        // The file the row names is gone or unreadable, so the list is
        // describing a card that no longer looks like that -- re-read it
        // rather than leaving a row that does nothing when tapped. Handed to
        // the worker, since a scan is blocking and this is a row's own event
        // callback.
        rescan_pending = true;
        return;
    }

    viewer_showing = true;

    // Layer 2 now covers the GUI. The catcher is what the next tap lands on.
    if (viewer_catcher != NULL) lv_obj_remove_flag(viewer_catcher, LV_OBJ_FLAG_HIDDEN);
}

static void ScreenSavedImagesViewerClicked(lv_event_t *event)
{
    (void)event;

    ScreenSavedImages_DismissViewer();
}

// Asks before deleting. Nothing touches the card here -- the prompt is the
// whole of this callback's effect.
static void ScreenSavedImagesTrashClicked(lv_event_t *event)
{
    const SAVED_ROW *row = lv_event_get_user_data(event);
    const IMAGE_CATALOG_ENTRY *entry;

    if ((row == NULL) || (row->index == SAVED_ROW_UNBOUND)) return;

    entry = ImageCatalog_GetEntry(row->index);

    if ((entry == NULL) || (confirm_overlay == NULL)) return;

    confirm_row = row->index;

    // Naming the file in the prompt is the point of it: the bins are 30px
    // apart and the row that was tapped is about to be hidden behind the dim
    if (confirm_name_label != NULL) lv_label_set_text(confirm_name_label, entry->name);

    lv_obj_remove_flag(confirm_overlay, LV_OBJ_FLAG_HIDDEN);
}

static void ScreenSavedImagesConfirmDeleteClicked(lv_event_t *event)
{
    (void)event;

    if (confirm_overlay != NULL) lv_obj_add_flag(confirm_overlay, LV_OBJ_FLAG_HIDDEN);

    // ImageCatalog_Delete() closes the gap in the table itself, so the list
    // only has to be redrawn, not re-scanned. Done right here rather than
    // deferred: the rebuild only re-points the row pool, it destroys nothing,
    // and this button is not one of the rows anyway.
    if (ImageCatalog_Delete(confirm_row)) ScreenSavedImagesRebuild();
}

static void ScreenSavedImagesConfirmCancelClicked(lv_event_t *event)
{
    (void)event;

    if (confirm_overlay != NULL) lv_obj_add_flag(confirm_overlay, LV_OBJ_FLAG_HIDDEN);
}

static void ScreenSavedImagesBackClicked(lv_event_t *event)
{
    (void)event;

    GUI_ShowScreen(GUI_SCREEN_MENU, GUI_NAV_BACK);
}

// *****************************************************************************
// Section: Construction
// *****************************************************************************

// The modal that the bin raises: a dimmed sheet over the whole screen with
// the question on it. Built as a plain object tree because gui/lv_conf.h
// leaves LV_USE_MSGBOX off -- see the widget note in screen_common.h.
static bool ScreenSavedImagesCreateConfirm(lv_obj_t *screen)
{
    lv_obj_t *dialog;

    confirm_overlay = lv_obj_create(screen);
    if (confirm_overlay == NULL) return false;

    lv_obj_set_size(confirm_overlay, LV_HOR_RES, LV_VER_RES);
    lv_obj_align(confirm_overlay, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(confirm_overlay, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(confirm_overlay, SAVED_CONFIRM_DIM_OPACITY, LV_PART_MAIN);
    lv_obj_set_style_border_width(confirm_overlay, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(confirm_overlay, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(confirm_overlay, 0, LV_PART_MAIN);
    lv_obj_remove_flag(confirm_overlay, LV_OBJ_FLAG_SCROLLABLE);

    // Clickable so a tap that misses both buttons is absorbed here instead of
    // reaching a row under the dim and opening an image behind the prompt.
    // No callback: a miss should do nothing at all, since the two answers are
    // the only ways out of a question about deleting a file.
    lv_obj_add_flag(confirm_overlay, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_add_flag(confirm_overlay, LV_OBJ_FLAG_HIDDEN);

    dialog = lv_obj_create(confirm_overlay);
    if (dialog == NULL) return false;

    lv_obj_set_size(dialog, SAVED_CONFIRM_PANEL_W_PX, SAVED_CONFIRM_PANEL_H_PX);
    lv_obj_align(dialog, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(dialog, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(dialog, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(dialog, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(dialog, lv_color_hex(0xA0A0A0), LV_PART_MAIN);
    lv_obj_set_style_radius(dialog, 6, LV_PART_MAIN);
    lv_obj_remove_flag(dialog, LV_OBJ_FLAG_SCROLLABLE);

    if (Screen_CreateLabel(dialog, &lv_font_montserrat_14, LV_ALIGN_CENTER, 0, -34,
            "Delete this image?") == NULL) return false;

    confirm_name_label = Screen_CreateLabel(dialog, &lv_font_montserrat_20,
            LV_ALIGN_CENTER, 0, -10, "--");

    if (confirm_name_label == NULL) return false;

    // Delete on the left in the destructive red, Cancel on the right in the
    // ordinary chip styling -- the safe answer is the one that looks like
    // every other button in this GUI
    {
        lv_obj_t *delete_button = Screen_CreateButton(dialog, LV_ALIGN_CENTER,
                -SAVED_CONFIRM_BUTTON_X_PX, SAVED_CONFIRM_BUTTON_Y_PX,
                SAVED_CONFIRM_BUTTON_W_PX, SAVED_CONFIRM_BUTTON_H_PX,
                LV_SYMBOL_TRASH "  Delete", ScreenSavedImagesConfirmDeleteClicked, NULL);

        lv_obj_t *cancel_button = Screen_CreateButton(dialog, LV_ALIGN_CENTER,
                SAVED_CONFIRM_BUTTON_X_PX, SAVED_CONFIRM_BUTTON_Y_PX,
                SAVED_CONFIRM_BUTTON_W_PX, SAVED_CONFIRM_BUTTON_H_PX,
                "Cancel", ScreenSavedImagesConfirmCancelClicked, NULL);

        if ((delete_button == NULL) || (cancel_button == NULL)) return false;

        lv_obj_set_style_border_color(delete_button, lv_color_hex(SAVED_DANGER_COLOR),
                LV_PART_MAIN);
        lv_obj_set_style_border_opa(delete_button, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_text_color(lv_obj_get_child(delete_button, SAVED_BUTTON_CHILD_LABEL),
                lv_color_hex(SAVED_DANGER_COLOR), LV_PART_MAIN);
    }

    return true;
}

lv_obj_t *ScreenSavedImages_Create(void)
{
    lv_obj_t *screen = Screen_Create();
    int32_t row_width;
    uint32_t index;

    if (screen == NULL) return NULL;

    if (!Screen_CreateHeader(screen, "Saved Images", &header)) return NULL;

    // Reached from the main menu, so back returns there rather than to home
    if (!Screen_AddBackButton(&header, ScreenSavedImagesBackClicked, NULL)) return NULL;

    // --- Body panel -------------------------------------------------------
    panel = lv_obj_create(screen);
    if (panel == NULL) return NULL;

    // Plain pixel sizes, NOT LV_PCT(100) minus an inset: LV_PCT() returns an
    // encoded special value rather than a number of pixels, so subtracting
    // from it silently changes the percentage instead of insetting anything.
    lv_obj_set_size(panel,
            LV_HOR_RES - (2 * SAVED_PANEL_INSET_PX),
            LV_VER_RES - (2 * SCREEN_BAR_HEIGHT_PX) - (2 * SAVED_PANEL_INSET_PX));
    lv_obj_align(panel, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(panel, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(panel, SCREEN_BAR_OPACITY, LV_PART_MAIN);
    lv_obj_set_style_border_width(panel, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(panel, SAVED_PANEL_PAD_PX, LV_PART_MAIN);
    lv_obj_set_style_pad_right(panel, SAVED_SCROLLBAR_LANE_PX, LV_PART_MAIN);

    // Vertical only, so a slightly-off drag along a row cannot skew the list
    // sideways -- same treatment as the SD card screen's row list
    lv_obj_set_scroll_dir(panel, LV_DIR_VER);

    // ON rather than AUTO: AUTO only shows the bar while a scroll is in
    // progress, which leaves no hint that there is anything below the fold.
    lv_obj_set_scrollbar_mode(panel, LV_SCROLLBAR_MODE_ON);

    // Re-points the row pool as the list moves -- see
    // ScreenSavedImagesBindRows()
    lv_obj_add_event_cb(panel, ScreenSavedImagesPanelScrolled, LV_EVENT_SCROLL, NULL);

    // --- Row pool ---------------------------------------------------------
    // The panel's content box: its own width less the left padding and the
    // wider right padding that keeps the scrollbar lane clear
    row_width = LV_HOR_RES - (2 * SAVED_PANEL_INSET_PX)
            - SAVED_PANEL_PAD_PX - SAVED_SCROLLBAR_LANE_PX;

    for (index = 0; index < SAVED_ROW_POOL_COUNT; index++)
    {
        if (!ScreenSavedImagesCreateRow(&rows[index], row_width)) return NULL;
    }

    // Draws nothing and takes no taps: remove_style_all() strips the theme's
    // fill and border, and removing the flag stops it catching a press. See
    // list_end.
    list_end = lv_obj_create(panel);
    if (list_end == NULL) return NULL;

    lv_obj_remove_style_all(list_end);
    lv_obj_set_size(list_end, 1, 1);
    lv_obj_remove_flag(list_end, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(list_end, LV_OBJ_FLAG_HIDDEN);

    // --- Empty state ------------------------------------------------------
    // A child of the SCREEN, not of the panel: the rebuild empties the panel
    // wholesale, and a label living in there would be destroyed with the
    // rows it exists to stand in for.
    empty_label = Screen_CreateLabel(screen, &lv_font_montserrat_14,
            LV_ALIGN_CENTER, 0, 0, "No saved images");

    if (empty_label == NULL) return NULL;

    lv_obj_set_style_text_align(empty_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(empty_label, lv_color_hex(SAVED_DIM_TEXT_COLOR), LV_PART_MAIN);
    lv_obj_add_flag(empty_label, LV_OBJ_FLAG_HIDDEN);

    // --- Delete prompt ----------------------------------------------------
    // After the panel, so it draws over the list it dims (LVGL's z-order is
    // child order)
    if (!ScreenSavedImagesCreateConfirm(screen)) return NULL;

    // --- Full-screen view catcher -----------------------------------------
    // Last of all, so it is above even the delete prompt: while Layer 2 is up
    // it must be the thing every tap lands on, whatever else is on screen.
    // Draws nothing -- no fill, no border -- it is pure hit area.
    viewer_catcher = lv_obj_create(screen);
    if (viewer_catcher == NULL) return NULL;

    lv_obj_set_size(viewer_catcher, LV_HOR_RES, LV_VER_RES);
    lv_obj_align(viewer_catcher, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_opa(viewer_catcher, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(viewer_catcher, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(viewer_catcher, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(viewer_catcher, 0, LV_PART_MAIN);
    lv_obj_remove_flag(viewer_catcher, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(viewer_catcher, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(viewer_catcher, ScreenSavedImagesViewerClicked,
            LV_EVENT_CLICKED, NULL);

    // --- Preview cache descriptors ----------------------------------------
    // One per DDR2 cell, pointing at it for good. A plain lv_image_dsc_t,
    // NOT an lv_draw_buf_t marked LV_IMAGE_FLAGS_ALLOCATED: that flag tells
    // LVGL it owns the memory and may hand it back to lv_malloc's allocator,
    // which for a pointer into DDR2 outside the LVGL heap would corrupt the
    // heap. The price is that the image decoder wraps the descriptor in a
    // draw buffer on each redraw -- a few field copies per 40x30 preview --
    // and that wrap checks alignment, which a cell boundary always passes
    // (4096 is a multiple of LV_DRAW_BUF_ALIGN), so it does not log.
    //
    // The stride is set explicitly rather than left to LVGL to derive, so
    // that it cannot start padding rows: ImageLoader_DecodeThumbnail() writes
    // width * 3 bytes per row with no gap between them.
    for (index = 0; index < IMAGE_CATALOG_MAX_ENTRIES; index++)
    {
        lv_image_dsc_t *dsc = &thumb_dsc[index];

        memset(dsc, 0, sizeof(*dsc));
        dsc->header.magic = LV_IMAGE_HEADER_MAGIC;
        dsc->header.cf = LV_COLOR_FORMAT_RGB888;
        dsc->header.w = SAVED_THUMB_WIDTH_PX;
        dsc->header.h = SAVED_THUMB_HEIGHT_PX;
        dsc->header.stride = SAVED_THUMB_STRIDE_BYTES;
        dsc->data_size = SAVED_THUMB_PIXEL_BYTES;
        dsc->data = ScreenSavedImagesCellPixels(index);
    }

    // The list is deliberately NOT scanned here: Create() runs at boot, where
    // the card may not be mounted yet and where a directory walk would sit in
    // front of the splash screen. The worker builds it on the first tick
    // after the screen is actually opened.
    if (lv_timer_create(ScreenSavedImagesWorker, SAVED_WORKER_INTERVAL_MS, NULL) == NULL)
    {
        return NULL;
    }

    ScreenSavedImages_Refresh();

    return screen;
}

void ScreenSavedImages_Refresh(void)
{
    char title[32];
    uint32_t count;
    uint32_t found;

    // ScreenSavedImages_Create() either finishes or leaves this NULL
    if (panel == NULL) return;

    Screen_RefreshHeader(&header);

    count = ImageCatalog_GetCount();
    found = ImageCatalog_GetFoundCount();

    // The count belongs next to the title because it is the answer to the
    // question the screen gets opened with. Both numbers are shown only when
    // the card holds more images than the catalog can list, so that the list
    // running out is visible rather than looking like the card is emptier
    // than it is.
    if (found > count)
    {
        snprintf(title, sizeof(title), "Saved Images (%lu/%lu)",
                (unsigned long)count, (unsigned long)found);
    }
    else
    {
        snprintf(title, sizeof(title), "Saved Images (%lu)", (unsigned long)count);
    }

    // Only on a change: lv_label_set_text() redraws the header every time
    // it is called, and this runs twice a second
    if ((header.title_label != NULL) &&
        (strcmp(lv_label_get_text(header.title_label), title) != 0))
    {
        lv_label_set_text(header.title_label, title);
    }
}
