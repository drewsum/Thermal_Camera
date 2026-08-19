/*******************************************************************************
  Thermal Still Capture

  File Name:
    still_capture.c

  Summary:
    Shutter-button still capture. See still_capture.h for the sequence and for
    what is deliberately left unwired until the touch controller works.
*******************************************************************************/

#include <xc.h>
#include <stdio.h>
#include <string.h>

#include "application/still_capture.h"

#include "application/flir/flir.h"
#include "application/flir/flir_process.h"
#include "application/flir/flir_vospi.h"
#include "application/image_legend.h"
#include "application/image_saver.h"
#include "glcd/glcd.h"
#include "gui/gui.h"
#include "gui/screens/screen_save_image.h"
#include "usb_uart/terminal_control.h"

// How long to let the "Saving to SD card..." status reach the panel before
// starting the encode. LVGL redraws on its own timer (LV_DEF_REFR_PERIOD is
// 33ms in gui/lv_conf.h) and the overlay is double buffered, so "the label
// has been set" and "the label is visible" are a few frames apart -- and the
// encode blocks straight through them. Four refresh periods is enough for the
// status to be up before the board goes quiet.
//
// It also doubles as a cancel window: Cancel during it returns the state to
// IDLE and StillCapture_Tasks() then never runs the write.
#define STILL_CAPTURE_SAVE_DELAY_MS   150u

static STILL_CAPTURE_STATE captureState = STILL_CAPTURE_IDLE;
static uint32_t captureSaveTickMs = 0;
static char savedName[IMAGE_SAVER_NAME_MAX] = "";

// Whether the palette legend gets baked into the frozen image. Reset to true
// on every capture (see StillCapture_Trigger()), so the save prompt always
// opens with its checkbox ticked rather than carrying a previous shot's
// choice forward into one the user has not looked at yet.
static bool saveLegend = true;

// Copies the live VoSPI frame out from under the capture path. The frame
// buffers are double buffered, so the copy is racing capture only if a frame
// completes mid-memcpy -- and even then it would take TWO more completions to
// reach the buffer being read here. At the Lepton's ~8.7fps that is over
// 200ms against a ~38KB cached copy, so the frame cannot tear in practice;
// capture is stopped immediately afterwards regardless.
static bool StillCaptureSnapshotRaw(void)
{
    const uint16_t *live = FLIR_VOSPI_PeekFrame();

    if (live == NULL) return false;

    memcpy((void *)STILL_CAPTURE_RAW_ADDRESS, live, STILL_CAPTURE_RAW_SIZE);

    return true;
}

// Copies the Layer 0 buffer being scanned out into this module's own, draws
// the legend into the copy if it is wanted, then points the controller at
// it. Both buffers are uncached, so the memcpy moves words straight through
// to DDR2 with no cache maintenance -- and the panel keeps scanning the
// ORIGINAL buffer until the flip below is latched at the next frame start,
// so it never sees the copy half-written or half-decorated.
static void StillCaptureFreezeDisplay(void)
{
    memcpy((void *)STILL_CAPTURE_RGB_ADDRESS,
           FLIRProcess_GetDisplayedLayer0Buffer(),
           STILL_CAPTURE_RGB_SIZE);

    if (saveLegend)
    {
        ImageLegend_DrawRGB888((void *)STILL_CAPTURE_RGB_ADDRESS,
                GLCD_FRAMEBUFFER_WIDTH_PX, GLCD_FRAMEBUFFER_HEIGHT_PX);
    }

    GLCD_SetLayer0BaseAddress((const void *)STILL_CAPTURE_RGB_ADDRESS);
}

// Repaints just the legend's bounding box in the held image, after
// saveLegend has changed.
//
// The clean copy it restores from is the video buffer the still was taken
// off: VoSPI is stopped for as long as a still is held, so flir_process.c
// cannot render over it, and it still holds the undecorated frame. That is
// what makes an unticked legend genuinely removable rather than needing a
// second full-frame copy kept around for the purpose.
//
// The panel IS scanning this buffer by now (unlike the freeze above), so a
// toggle can tear for a frame. The box is ~72x160, which is well under a
// panel frame's worth of pixels, and the alternative -- waiting out a
// vertical blank on a control the user is tapping -- costs more than the
// one-frame seam it would avoid.
static void StillCaptureRedrawLegend(void)
{
    const uint8_t *clean = (const uint8_t *)FLIRProcess_GetDisplayedLayer0Buffer();
    uint8_t *held = (uint8_t *)STILL_CAPTURE_RGB_ADDRESS;
    uint32_t row;

    for (row = 0; row < IMAGE_LEGEND_BOX_HEIGHT_PX; row++)
    {
        uint32_t offset = (((uint32_t)IMAGE_LEGEND_BOX_Y_PX + row) * GLCD_FRAMEBUFFER_STRIDE_BYTES) +
                          ((uint32_t)IMAGE_LEGEND_BOX_X_PX * GLCD_FRAMEBUFFER_BYTES_PER_PIXEL);

        memcpy(held + offset, clean + offset,
                IMAGE_LEGEND_BOX_WIDTH_PX * GLCD_FRAMEBUFFER_BYTES_PER_PIXEL);
    }

    if (saveLegend)
    {
        ImageLegend_DrawRGB888((void *)STILL_CAPTURE_RGB_ADDRESS,
                GLCD_FRAMEBUFFER_WIDTH_PX, GLCD_FRAMEBUFFER_HEIGHT_PX);
    }
}

bool StillCapture_Trigger(void)
{
    if (captureState != STILL_CAPTURE_IDLE) return true;

    if (FLIR_GetState() != FLIR_STATE_STREAMING)
    {
        terminalTextAttributes(YELLOW_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("Shutter ignored -- thermal video is not streaming\r\n");
        terminalTextAttributesReset();
        return false;
    }

    if (!StillCaptureSnapshotRaw())
    {
        terminalTextAttributes(YELLOW_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("Shutter ignored -- no complete thermal frame captured yet\r\n");
        terminalTextAttributesReset();
        return false;
    }

    // Every capture starts with the legend on -- the checkbox on the save
    // screen reads this back, so it opens ticked
    saveLegend = true;

    StillCaptureFreezeDisplay();

    // Only now is it safe to stop capture: Layer 0 is already pointed at the
    // frozen copy, which is what lets this use the variant that does NOT
    // blank the video layer (see FLIR_StreamOffKeepImage()).
    FLIR_StreamOffKeepImage();

    savedName[0] = '\0';

    // Held, but nothing is written until the user picks Save on the prompt
    captureState = STILL_CAPTURE_PROMPTING;

    terminalTextAttributes(MAGENTA_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("Shutter: thermal frame captured and held\r\n");
    terminalTextAttributesReset();

    GUI_ShowSaveImageScreen();

    return true;
}

void StillCapture_ConfirmSave(void)
{
    // Guarded on PROMPTING, so a second tap on "Save to SD" while the first
    // is still pending cannot restart the timer or queue a second write
    if (captureState != STILL_CAPTURE_PROMPTING) return;

    captureState = STILL_CAPTURE_SAVING;
    captureSaveTickMs = GUI_GetTickMs();

    // Put "Saving to SD card..." up now; the encode below starts a few
    // main-loop passes from here and then blocks straight through the redraw
    ScreenSaveImage_Refresh();
}

void StillCapture_SetSaveLegend(bool include)
{
    // Guarded on PROMPTING for the same reason ConfirmSave() is: the frozen
    // image is only this module's to redraw while the user is still being
    // asked about it. Once the encode has started, the image is what was
    // saved and a late toggle must not rewrite it.
    if (captureState != STILL_CAPTURE_PROMPTING) return;

    if (saveLegend == include) return;

    saveLegend = include;

    // The panel is showing the held image, so this is also the preview: what
    // the user sees after the tap is what the PNG will contain
    StillCaptureRedrawLegend();
}

bool StillCapture_GetSaveLegend(void)
{
    return saveLegend;
}

void StillCapture_Tasks(void)
{
    bool saved;

    if (captureState != STILL_CAPTURE_SAVING) return;

    // Wrap-safe unsigned subtraction, same reasoning as GUI_GetTickMs()'s own
    // comment
    if ((GUI_GetTickMs() - captureSaveTickMs) < STILL_CAPTURE_SAVE_DELAY_MS) return;

    // Reached only via StillCapture_ConfirmSave(), i.e. only because the user
    // pressed "Save to SD". Cancelling inside the delay window above puts the
    // state back to IDLE, so this never runs for a declined capture.
    saved = ImageSaver_SaveRGB888ToSD((const void *)STILL_CAPTURE_RGB_ADDRESS,
            GLCD_FRAMEBUFFER_WIDTH_PX, GLCD_FRAMEBUFFER_HEIGHT_PX,
            savedName, sizeof(savedName));

    captureState = saved ? STILL_CAPTURE_SAVED : STILL_CAPTURE_FAILED;

    // Repaint the status line with the outcome rather than waiting for the
    // next 500ms heartbeat refresh -- the save just took long enough that a
    // further half second of "Saving..." reads as a hang
    ScreenSaveImage_Refresh();
}

void StillCapture_Resume(void)
{
    if (captureState == STILL_CAPTURE_IDLE) return;

    captureState = STILL_CAPTURE_IDLE;
    savedName[0] = '\0';

    // Hand Layer 0 back to the video buffers before capture restarts, so the
    // first rendered frame is what appears rather than the frozen still
    // lingering until the first flip
    GLCD_SetLayer0BaseAddress(FLIRProcess_GetDisplayedLayer0Buffer());

    FLIR_StreamOn();
}

STILL_CAPTURE_STATE StillCapture_GetState(void)
{
    return captureState;
}

const char *StillCapture_GetSavedName(void)
{
    return savedName;
}

const uint16_t *StillCapture_GetRawFrame(void)
{
    if (captureState == STILL_CAPTURE_IDLE) return NULL;

    return (const uint16_t *)STILL_CAPTURE_RAW_ADDRESS;
}
