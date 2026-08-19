/*******************************************************************************
  Saved Images GUI Screen

  File Name:
    screen_saved_images.c

  Summary:
    See screen_saved_images.h for the layout, how the full-screen view takes
    its tap, and why the scan and the previews run off a timer rather than
    out of the refresh.
*******************************************************************************/

#include <stdio.h>
#include <string.h>

#include "gui/screens/screen_saved_images.h"
#include "gui/screens/screen_common.h"

#include "gui/gui.h"
#include "gui/lvgl/lvgl.h"
#include "application/image_catalog.h"
#include "application/image_loader.h"
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
// a preview; the pitch is what the visible-row arithmetic below works in.
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

// How many previews are held at once. Only about four rows fit the panel, so
// this covers everything on screen with room either side for a scroll in
// progress; each one costs 40 x 30 x 3 = 3,600 bytes of the LVGL heap
// (8 x 3,600 = 28,800 bytes), taken once in Create() and never released.
#define SAVED_THUMB_SLOT_COUNT      8u

// How often the worker timer runs. One scan or one preview decode per tick
// -- see screen_saved_images.h. Fast enough that the previews fill in as
// quickly as the decoder can produce them, slow enough to leave the main
// loop and the FLIR video path room between them.
#define SAVED_WORKER_INTERVAL_MS    30

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

// Where a row's children sit in its child list. Screen_CreateButton() makes
// the chip's label first, so it is always child 0 -- for a row that label is
// the filename, and for the bin and the Delete button it is the glyph that
// gets recolored. The preview is added immediately after the row's name.
// Nothing else here reaches into an object by child index.
#define SAVED_BUTTON_CHILD_LABEL    0
#define SAVED_ROW_CHILD_NAME        SAVED_BUTTON_CHILD_LABEL
#define SAVED_ROW_CHILD_THUMB       1

// What a row's preview is doing. PENDING is the initial state of every row
// after a rebuild and the only one the worker acts on.
typedef enum
{
    SAVED_THUMB_PENDING = 0,   // no preview yet; decode it when it scrolls into view
    SAVED_THUMB_READY,         // a slot below holds it and the row is showing it
    SAVED_THUMB_FAILED         // the decode failed; do not keep retrying it
} SAVED_THUMB_STATE;

// One cached preview. `buf` is created once in Create() and reused for
// whatever image the slot is lent to next -- it is both the image source
// LVGL draws through and the memory the decoder writes into, and `pixels` is
// just its pixel pointer kept to hand.
//
// `name` rather than a catalog index is what identifies the image, because
// indices shift the moment a row above is deleted -- see the rebuild.
typedef struct
{
    lv_draw_buf_t *buf;
    uint8_t *pixels;
    char name[IMAGE_SAVER_NAME_MAX];
    lv_obj_t *image;    // the row widget showing it, or NULL if it has none
    uint32_t row;       // that row's catalog index, meaningless if image is NULL
    uint32_t stamp;     // claim order, for the least-recently-used eviction
    bool used;
} SAVED_THUMB_SLOT;

static SCREEN_HEADER header;

static lv_obj_t *panel = NULL;
static lv_obj_t *empty_label = NULL;

static lv_obj_t *confirm_overlay = NULL;
static lv_obj_t *confirm_name_label = NULL;

// The full-screen transparent object every tap lands on while an image is up
// on Layer 2 -- see the catcher note in screen_saved_images.h.
static lv_obj_t *viewer_catcher = NULL;
static bool viewer_showing = false;

static SAVED_THUMB_SLOT thumb_slots[SAVED_THUMB_SLOT_COUNT];
static uint32_t thumb_stamp = 0;

static uint8_t thumb_state[IMAGE_CATALOG_MAX_ENTRIES];

// Which row the delete prompt is currently asking about. Only meaningful
// while confirm_overlay is visible.
static uint32_t confirm_row = 0;

// Set by the event callbacks to ask the worker timer to redraw the list,
// rather than redrawing inline: the rows are what those callbacks were
// dispatched FROM, and deleting the object tree an event is still walking is
// the one way to turn a tap into a crash.
//
// rescan_pending is the stronger of the two -- it re-reads the directory
// first. A delete needs only the redraw (ImageCatalog_Delete() has already
// closed the gap in the table), where a card coming or going invalidates the
// table itself and has to go back to the filesystem.
static bool rebuild_pending = false;
static bool rescan_pending = false;

// Whether the screen was active on the previous tick, and what the card was
// doing -- the two edges that make the list stale (see the worker).
static bool worker_was_active = false;
static bool worker_last_mounted = false;

// *****************************************************************************
// Section: Preview cache
// *****************************************************************************

// Drops whatever a slot is lending out: the row stops drawing it (its buffer
// is about to be overwritten) and goes back to PENDING, so scrolling to it
// again decodes it afresh.
static void ScreenSavedImagesReleaseSlot(SAVED_THUMB_SLOT *slot)
{
    if (slot->image != NULL)
    {
        lv_image_set_src(slot->image, NULL);

        if (slot->row < IMAGE_CATALOG_MAX_ENTRIES)
        {
            thumb_state[slot->row] = SAVED_THUMB_PENDING;
        }
    }

    slot->image = NULL;
    slot->used = false;
    slot->name[0] = '\0';
}

// A slot to decode into: a free one if there is any, otherwise the one lent
// out longest ago. With more slots than rows on screen, the victim is always
// a row that has been scrolled off -- which is what makes taking it cheap.
static SAVED_THUMB_SLOT *ScreenSavedImagesClaimSlot(void)
{
    SAVED_THUMB_SLOT *victim = &thumb_slots[0];
    uint32_t index;

    for (index = 0; index < SAVED_THUMB_SLOT_COUNT; index++)
    {
        if (!thumb_slots[index].used) return &thumb_slots[index];

        if (thumb_slots[index].stamp < victim->stamp) victim = &thumb_slots[index];
    }

    ScreenSavedImagesReleaseSlot(victim);

    return victim;
}

// Points `image` at `slot`'s picture. lv_image_set_src() only stores the
// pointer, which is why the draw buffer belongs to the slot and not to this
// call -- the widget reads it on every redraw.
static void ScreenSavedImagesBindSlot(SAVED_THUMB_SLOT *slot, lv_obj_t *image,
        uint32_t row)
{
    if (image == NULL) return;

    slot->image = image;
    slot->row = row;
    slot->stamp = ++thumb_stamp;
    slot->used = true;

    lv_image_set_src(image, slot->buf);

    thumb_state[row] = SAVED_THUMB_READY;
}

// The preview widget of row `index`, or NULL if there is no such row. Rows
// are children of the panel in list order, so the catalog index IS the child
// index -- the one place that relationship is relied on.
static lv_obj_t *ScreenSavedImagesRowThumb(uint32_t index)
{
    lv_obj_t *row;

    if (panel == NULL) return NULL;
    if (index >= (uint32_t)lv_obj_get_child_count(panel)) return NULL;

    row = lv_obj_get_child(panel, (int32_t)index);

    if (row == NULL) return NULL;

    return lv_obj_get_child(row, SAVED_ROW_CHILD_THUMB);
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

// Builds one row: preview, name, size, bin. Returns false if any widget
// could not be created, which aborts the rebuild rather than leaving a
// half-built list.
static bool ScreenSavedImagesCreateRow(uint32_t index, int32_t width)
{
    const IMAGE_CATALOG_ENTRY *entry = ImageCatalog_GetEntry(index);
    char size_text[16];
    lv_obj_t *row;
    lv_obj_t *thumb;
    lv_obj_t *size_label;
    lv_obj_t *trash;

    if (entry == NULL) return false;

    // The whole row is the tap target that opens the image -- the bin below
    // is a child of it, and children are hit-tested first, so the two do not
    // fight over the press.
    row = Screen_CreateButton(panel, LV_ALIGN_TOP_LEFT, 0,
            (int32_t)(index * SAVED_ROW_PITCH_PX), width, SAVED_ROW_HEIGHT_PX,
            entry->name, ScreenSavedImagesRowClicked, (void *)(uintptr_t)index);

    if (row == NULL) return false;

    // From here on the row exists, so every failure below takes it back down
    // with it -- the caller stops the list at this index, and a row missing
    // half its parts would sit at the bottom of it looking like an image
    // whose name and preview had gone missing.

    // Screen_CreateButton centers its label; this list wants the name beside
    // the preview. lv_obj_get_child(row, SAVED_ROW_CHILD_NAME) is that label.
    lv_obj_align(lv_obj_get_child(row, SAVED_ROW_CHILD_NAME), LV_ALIGN_LEFT_MID,
            SAVED_NAME_X_PX, 0);

    // --- Preview ----------------------------------------------------------
    // Created with no source: the worker decodes it later, and until then the
    // dark fill below stands in for the picture so the row does not look
    // broken. MUST be child SAVED_ROW_CHILD_THUMB -- see that macro.
    thumb = lv_image_create(row);

    if (thumb == NULL)
    {
        lv_obj_delete(row);
        return false;
    }

    lv_obj_set_size(thumb, SAVED_THUMB_WIDTH_PX, SAVED_THUMB_HEIGHT_PX);
    lv_obj_align(thumb, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_color(thumb, lv_color_hex(SAVED_THUMB_EMPTY_COLOR), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(thumb, LV_OPA_COVER, LV_PART_MAIN);

    // Every child of the row would otherwise swallow the press before the row
    // saw it -- the same trap Screen_CreateButton() disarms for its own label
    lv_obj_remove_flag(thumb, LV_OBJ_FLAG_CLICKABLE);

    // --- File size --------------------------------------------------------
    ScreenSavedImagesFormatSize(size_text, sizeof(size_text), entry->size_bytes);

    size_label = Screen_CreateLabel(row, &lv_font_montserrat_14, LV_ALIGN_RIGHT_MID,
            -(SAVED_TRASH_WIDTH_PX + SAVED_SIZE_GAP_PX), 0, size_text);

    if (size_label == NULL)
    {
        lv_obj_delete(row);
        return false;
    }

    lv_obj_set_style_text_color(size_label, lv_color_hex(SAVED_DIM_TEXT_COLOR), LV_PART_MAIN);
    lv_obj_remove_flag(size_label, LV_OBJ_FLAG_CLICKABLE);

    // --- Bin --------------------------------------------------------------
    // Clickable, unlike the two above: this is the one part of the row that
    // does something other than open the image.
    trash = Screen_CreateButton(row, LV_ALIGN_RIGHT_MID, 0, 0,
            SAVED_TRASH_WIDTH_PX, SAVED_TRASH_HEIGHT_PX, LV_SYMBOL_TRASH,
            ScreenSavedImagesTrashClicked, (void *)(uintptr_t)index);

    if (trash == NULL)
    {
        lv_obj_delete(row);
        return false;
    }

    // Red, border and glyph both, because it is the only control on this
    // screen that destroys something
    lv_obj_set_style_border_color(trash, lv_color_hex(SAVED_DANGER_COLOR), LV_PART_MAIN);
    lv_obj_set_style_border_opa(trash, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_text_color(lv_obj_get_child(trash, SAVED_BUTTON_CHILD_LABEL),
            lv_color_hex(SAVED_DANGER_COLOR), LV_PART_MAIN);

    return true;
}

// Empties the list and builds it again from the catalog as it stands now.
//
// Only ever called from the worker timer -- see rebuild_pending. Every row
// widget is destroyed here, so the preview pool is re-pointed at the new
// rows first: slots are matched to rows BY NAME, which is what keeps the
// previews of everything else on screen when one image is deleted (the
// catalog indices below the deleted row all shift by one, so matching on
// those would re-decode the whole visible page).
static void ScreenSavedImagesRebuild(void)
{
    uint32_t count = ImageCatalog_GetCount();
    int32_t row_width;
    int32_t scroll;
    int32_t scroll_max;
    uint32_t index;

    if (panel == NULL) return;

    // Where the reader had scrolled to. Deleting an image rebuilds the list,
    // and snapping back to the top every time would make clearing several
    // images from the middle of a long list unusable.
    scroll = lv_obj_get_scroll_y(panel);
    if (scroll < 0) scroll = 0;   // elastic over-scroll at the top

    // The widgets the slots were lending to are about to be freed. Forget the
    // pointers WITHOUT releasing the slots -- the decoded pictures are still
    // good, and the loop below hands them back to whichever new row wants them.
    for (index = 0; index < SAVED_THUMB_SLOT_COUNT; index++)
    {
        thumb_slots[index].image = NULL;
        thumb_slots[index].row = IMAGE_CATALOG_MAX_ENTRIES;
    }

    lv_obj_clean(panel);

    // The panel's content box: its own width less the left padding and the
    // wider right padding that keeps the scrollbar lane clear
    row_width = LV_HOR_RES - (2 * SAVED_PANEL_INSET_PX)
            - SAVED_PANEL_PAD_PX - SAVED_SCROLLBAR_LANE_PX;

    for (index = 0; index < count; index++)
    {
        const IMAGE_CATALOG_ENTRY *entry = ImageCatalog_GetEntry(index);
        uint32_t slot;

        thumb_state[index] = SAVED_THUMB_PENDING;

        if ((entry == NULL) || !ScreenSavedImagesCreateRow(index, row_width))
        {
            // Out of heap partway through. Keep what was built -- a short
            // list is still usable -- and stop asking for more.
            count = index;
            break;
        }

        // Does the pool still hold this image's preview from before the
        // rebuild? If so the row gets it back without a decode.
        for (slot = 0; slot < SAVED_THUMB_SLOT_COUNT; slot++)
        {
            if (!thumb_slots[slot].used) continue;
            if (thumb_slots[slot].image != NULL) continue;

            if (strcmp(thumb_slots[slot].name, entry->name) == 0)
            {
                ScreenSavedImagesBindSlot(&thumb_slots[slot],
                        ScreenSavedImagesRowThumb(index), index);
                break;
            }
        }
    }

    // Put the reader back where they were, but no further down than the list
    // now reaches -- it is usually one row shorter than it was, and scrolling
    // past the end would leave the panel showing nothing at all. Computed
    // here rather than left to LVGL because the rows were positioned by hand,
    // so their extent is known exactly.
    scroll_max = (int32_t)(count * SAVED_ROW_PITCH_PX) - SAVED_ROW_GAP_PX
            - lv_obj_get_content_height(panel);

    if (scroll_max < 0) scroll_max = 0;
    if (scroll > scroll_max) scroll = scroll_max;

    lv_obj_scroll_to_y(panel, scroll, LV_ANIM_OFF);

    // Slots whose image is gone from the list entirely (it was deleted, or
    // the card changed) are dead weight -- free them for the rows that are
    // actually here
    for (index = 0; index < SAVED_THUMB_SLOT_COUNT; index++)
    {
        if (thumb_slots[index].used && (thumb_slots[index].image == NULL))
        {
            thumb_slots[index].used = false;
            thumb_slots[index].name[0] = '\0';
        }
    }

    // The empty state carries the reason there is nothing to show, since the
    // two reasons want different things done about them
    if (empty_label != NULL)
    {
        if (count > 0)
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

// The catalog indices of the rows currently scrolled into view, inclusive.
// Returns false when the list is empty. Worked out from the panel's scroll
// offset and the fixed row pitch rather than by asking each row whether it
// is visible -- the rows are laid out at explicit offsets, so this is exact
// and does not walk the list.
static bool ScreenSavedImagesVisibleRange(uint32_t *first, uint32_t *last)
{
    uint32_t count = ImageCatalog_GetCount();
    int32_t scroll;
    int32_t view_height;

    if ((panel == NULL) || (count == 0)) return false;

    scroll = lv_obj_get_scroll_y(panel);
    if (scroll < 0) scroll = 0;   // over-scroll bounce at the top

    view_height = lv_obj_get_content_height(panel);

    *first = (uint32_t)(scroll / SAVED_ROW_PITCH_PX);
    *last = (uint32_t)((scroll + view_height) / SAVED_ROW_PITCH_PX);

    if (*first >= count) return false;
    if (*last >= count) *last = count - 1u;

    return true;
}

// Decodes the preview of one visible row that hasn't got one. Returns false
// if every visible row is already resolved, which is the steady state.
static bool ScreenSavedImagesDecodeOnePreview(void)
{
    char path[IMAGE_CATALOG_PATH_MAX];
    uint32_t first, last, index;

    if (!ScreenSavedImagesVisibleRange(&first, &last)) return false;

    for (index = first; index <= last; index++)
    {
        const IMAGE_CATALOG_ENTRY *entry;
        SAVED_THUMB_SLOT *slot;
        lv_obj_t *image;

        if (thumb_state[index] != SAVED_THUMB_PENDING) continue;

        entry = ImageCatalog_GetEntry(index);
        image = ScreenSavedImagesRowThumb(index);

        if ((entry == NULL) || (image == NULL) ||
            !ImageCatalog_BuildPath(index, path, sizeof(path)))
        {
            thumb_state[index] = SAVED_THUMB_FAILED;
            return true;
        }

        slot = ScreenSavedImagesClaimSlot();

        if (!ImageLoader_DecodeThumbnail(IMAGE_MEDIA_SD_CARD, path, slot->pixels,
                SAVED_THUMB_WIDTH_PX, SAVED_THUMB_HEIGHT_PX))
        {
            // A file that will not decode is not going to start: mark it and
            // move on, or the worker would spend every tick on it forever.
            // The row keeps its empty frame.
            thumb_state[index] = SAVED_THUMB_FAILED;
            return true;
        }

        snprintf(slot->name, sizeof(slot->name), "%s", entry->name);
        ScreenSavedImagesBindSlot(slot, image, index);

        return true;
    }

    return false;
}

// One piece of blocking work per tick, and only while the list is the thing
// being looked at -- see screen_saved_images.h.
static void ScreenSavedImagesWorker(lv_timer_t *timer)
{
    bool mounted;

    (void)timer;

    if (!GUI_IsScreenActive(GUI_SCREEN_SAVED_IMAGES))
    {
        worker_was_active = false;
        return;
    }

    // Arriving on the screen: whatever the catalog holds is from the last
    // visit, and the shutter button or a USB host may have changed the card
    // since. Rebuilding beats showing a list that is quietly wrong.
    if (!worker_was_active)
    {
        worker_was_active = true;
        worker_last_mounted = SDFileIO_IsMounted();
        rescan_pending = false;
        rebuild_pending = false;

        // A prompt left open when the screen was navigated away from is a
        // question about a row that no longer exists -- it does not come back
        // with the screen
        if (confirm_overlay != NULL) lv_obj_add_flag(confirm_overlay, LV_OBJ_FLAG_HIDDEN);

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
    // rebuild renumbers those. A card inserted between the tap on the bin and
    // the tap on Delete would otherwise mean deleting a different file than
    // the one named on screen. Nothing here is urgent enough to risk that --
    // it all happens as soon as the question is answered.
    if ((confirm_overlay != NULL) &&
        !lv_obj_has_flag(confirm_overlay, LV_OBJ_FLAG_HIDDEN)) return;

    mounted = SDFileIO_IsMounted();

    if (mounted != worker_last_mounted)
    {
        worker_last_mounted = mounted;
        rescan_pending = true;
    }

    if (rescan_pending || rebuild_pending)
    {
        if (rescan_pending) ImageCatalog_Scan();

        rescan_pending = false;
        rebuild_pending = false;

        ScreenSavedImagesRebuild();
        return;
    }

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
    uintptr_t index = (uintptr_t)lv_event_get_user_data(event);
    char path[IMAGE_CATALOG_PATH_MAX];

    if (!ImageCatalog_BuildPath((uint32_t)index, path, sizeof(path))) return;

    if (!ImageLoader_DisplayPNG(IMAGE_MEDIA_SD_CARD, path))
    {
        // The file the row names is gone or unreadable, so the list is
        // describing a card that no longer looks like that -- re-read it
        // rather than leaving a row that does nothing when tapped. Handed to
        // the worker like every other rebuild, since this is a row's own
        // event callback.
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
    uintptr_t index = (uintptr_t)lv_event_get_user_data(event);
    const IMAGE_CATALOG_ENTRY *entry = ImageCatalog_GetEntry((uint32_t)index);

    if ((entry == NULL) || (confirm_overlay == NULL)) return;

    confirm_row = (uint32_t)index;

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
    // only has to be redrawn -- and that redraw is deferred to the worker,
    // because it destroys the very row whose bin dispatched this event
    if (ImageCatalog_Delete(confirm_row)) rebuild_pending = true;
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

    // --- Preview buffer pool ----------------------------------------------
    // Taken once, here, and never released: a per-decode allocation of this
    // size would churn the LVGL heap the GUI and every PNG decode share, and
    // the pool is small enough (see SAVED_THUMB_SLOT_COUNT) to simply own.
    //
    // lv_draw_buf_create() rather than lv_malloc() behind a hand-filled
    // lv_image_dsc_t, for two reasons that are really one. It aligns the
    // pixels to LV_DRAW_BUF_ALIGN, which lv_malloc() does not promise; and it
    // marks the result LV_IMAGE_FLAGS_ALLOCATED, which makes the image
    // decoder hand the buffer straight to the renderer. A plain descriptor
    // instead gets re-wrapped through lv_draw_buf_init() on EVERY redraw of
    // EVERY preview, and unaligned pixels make that log "Data is not aligned,
    // ignored" each time -- the picture still draws, but the console fills
    // with it.
    //
    // The stride is passed explicitly rather than left to LVGL to derive, so
    // that it cannot start padding rows: ImageLoader_DecodeThumbnail() writes
    // width * 3 bytes per row with no gap between them.
    for (index = 0; index < SAVED_THUMB_SLOT_COUNT; index++)
    {
        SAVED_THUMB_SLOT *slot = &thumb_slots[index];

        slot->buf = lv_draw_buf_create(SAVED_THUMB_WIDTH_PX, SAVED_THUMB_HEIGHT_PX,
                LV_COLOR_FORMAT_RGB888, SAVED_THUMB_STRIDE_BYTES);

        if (slot->buf == NULL) return NULL;

        slot->pixels = slot->buf->data;

        slot->image = NULL;
        slot->row = IMAGE_CATALOG_MAX_ENTRIES;
        slot->name[0] = '\0';
        slot->stamp = 0;
        slot->used = false;
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

    if (header.title_label != NULL) lv_label_set_text(header.title_label, title);
}
