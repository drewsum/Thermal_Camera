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
#include "application/image_saver.h"
#include "glcd/glcd.h"
#include "gui/gui.h"
#include "gui/screens/screen_save_image.h"
#include "usb_uart/terminal_control.h"

// How long to let the save-image screen get itself on the panel before
// starting the encode. LVGL redraws on its own timer (LV_DEF_REFR_PERIOD is
// 33ms in gui/lv_conf.h) and the overlay is double buffered, so "the screen
// has been loaded" and "the screen is visible" are a few frames apart -- and
// the encode blocks straight through them. Four refresh periods is enough for
// the prompt to be up before the board goes quiet.
#define STILL_CAPTURE_SAVE_DELAY_MS   150u

static STILL_CAPTURE_STATE captureState = STILL_CAPTURE_IDLE;
static uint32_t captureShownTickMs = 0;
static char savedName[IMAGE_SAVER_NAME_MAX] = "";

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

// Copies the Layer 0 buffer being scanned out into this module's own, then
// points the controller at the copy. Both are uncached, so the memcpy moves
// words straight through to DDR2 with no cache maintenance -- and the panel
// keeps scanning the ORIGINAL buffer until the flip below is latched at the
// next frame start, so it never sees the copy half-written.
static void StillCaptureFreezeDisplay(void)
{
    memcpy((void *)STILL_CAPTURE_RGB_ADDRESS,
           FLIRProcess_GetDisplayedLayer0Buffer(),
           STILL_CAPTURE_RGB_SIZE);

    GLCD_SetLayer0BaseAddress((const void *)STILL_CAPTURE_RGB_ADDRESS);
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

    StillCaptureFreezeDisplay();

    // Only now is it safe to stop capture: Layer 0 is already pointed at the
    // frozen copy, which is what lets this use the variant that does NOT
    // blank the video layer (see FLIR_StreamOffKeepImage()).
    FLIR_StreamOffKeepImage();

    savedName[0] = '\0';
    captureState = STILL_CAPTURE_SAVING;
    captureShownTickMs = GUI_GetTickMs();

    terminalTextAttributes(MAGENTA_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("Shutter: thermal frame captured and held\r\n");
    terminalTextAttributesReset();

    GUI_ShowSaveImageScreen();

    return true;
}

void StillCapture_Tasks(void)
{
    bool saved;

    if (captureState != STILL_CAPTURE_SAVING) return;

    // Wrap-safe unsigned subtraction, same reasoning as GUI_GetTickMs()'s own
    // comment
    if ((GUI_GetTickMs() - captureShownTickMs) < STILL_CAPTURE_SAVE_DELAY_MS) return;

    // Unconditional for now -- see the note in still_capture.h about where
    // the screen's "Save to SD" / "Cancel" choice will hook in.
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
