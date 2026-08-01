/* ************************************************************************** */
/** Thermal Still Capture

  @File Name
    still_capture.h

  @Summary
    The shutter button's job: freeze the live thermal video on a single frame,
    keep that frame in DDR2, and offer to write it to the SD card.

  @Description
    Pressing and releasing the SHUTTER button (application/pushbuttons.c
    raises shutter_button_capture_request) runs this sequence:

      1. Snapshot the most recently completed VoSPI frame -- the raw 160x120
         14-bit values, straight from flir_vospi.c's live double buffer -- into
         this module's own DDR2 frame. The raw data is kept rather than only
         the picture because it is the radiometric record: a future "what
         temperature is that pixel" or a re-colorize with a different palette
         needs the counts, which the RGB image has already thrown away.

      2. Snapshot the RGB888 image the panel is scanning out right now
         (flir_process.c owns the Layer 0 flip and reports which buffer that
         is) into a second DDR2 buffer, and point Layer 0 at it. This is what
         freezes the display, and it is an exact copy of what the user was
         looking at -- re-rendering the raw frame instead would re-run the AGC
         and could come out a shade different.

      3. Stop VoSPI capture (FLIR_StreamOffKeepImage(), which unlike
         FLIR_StreamOff() leaves the video layer alone) so nothing overwrites
         the frozen image, and show the save-image screen.

      4. Encode the frozen image to a PNG on the SD card, if one is mounted.

    Step 4 is deliberately deferred a few main-loop passes past step 3: the
    encode-and-write blocks for the better part of a second, and running it
    inline would mean the screen the user is being prompted with does not
    appear until after the operation it is prompting about has finished.

    *** The save is unconditional today. *** The screen's "Save to SD" /
    "Cancel" footer is drawn but not wired -- the panel's GT911 touch
    controller is not up yet -- so the image is saved and the board then stays
    on the save screen indefinitely. Once touch lands, the intended shape is:
    step 4 runs only on "Save to SD", and "Cancel" calls
    StillCapture_Resume().
 */
/* ************************************************************************** */

#ifndef STILL_CAPTURE_H
#define STILL_CAPTURE_H

#include <stdbool.h>
#include <stdint.h>

#include "core/ddr2.h"
#include "glcd/glcd.h"
#include "application/flir/flir_vospi.h"

#ifdef __cplusplus
extern "C" {
#endif

// This module's two DDR2 buffers, at +9MB -- clear of the VoSPI frame buffers
// at +8MB and the reserved 0..8MB region below them. The full partition map
// lives in gui/gui.h; keep it in step with these.
//
// The raw frame is cached (KSEG0) because only the CPU ever reads or writes
// it, exactly like the VoSPI buffers it is copied from. The RGB image is
// uncached (KSEG1) because the GLCD Controller's DMA scans it out as Layer 0,
// the same reason every other layer buffer in glcd.h is uncached.
#define STILL_CAPTURE_RAW_ADDRESS   (DDR2_KSEG0_BASE_ADDRESS + 0x00900000u)
#define STILL_CAPTURE_RAW_SIZE      FLIR_VOSPI_FRAME_SIZE_BYTES
#define STILL_CAPTURE_RGB_ADDRESS   (DDR2_KSEG1_BASE_ADDRESS + 0x00910000u)
#define STILL_CAPTURE_RGB_SIZE      GLCD_FRAMEBUFFER_SIZE_BYTES

// What the module is doing, for the save-image screen's status line.
typedef enum
{
    STILL_CAPTURE_IDLE = 0,    // live video, no still held
    STILL_CAPTURE_SAVING,      // still held, PNG write pending or in progress
    STILL_CAPTURE_SAVED,       // written to the card
    STILL_CAPTURE_FAILED       // nothing written (no card, or the write failed)
} STILL_CAPTURE_STATE;

// Freezes the display on the most recent thermal frame and starts the
// sequence in the file header. Returns false, changing nothing, if there is
// no frame to capture -- the FLIR is not streaming, or it has not completed
// a frame since boot. No-op (true) if a still is already being held.
bool StillCapture_Trigger(void);

// Advances the deferred save. Call once per main-loop iteration; it is a
// single state compare when no capture is in flight.
void StillCapture_Tasks(void);

// Discards the still, returns Layer 0 to the live video buffers and restarts
// VoSPI capture. Nothing calls this yet -- it is what the save screen's
// "Cancel" will do once the touch controller is wired up. No-op if no still
// is being held.
void StillCapture_Resume(void);

// Current state, and the name of the file written (empty until a save
// succeeds). The save-image screen reads both.
STILL_CAPTURE_STATE StillCapture_GetState(void);
const char *StillCapture_GetSavedName(void);

// The held frame, for whatever comes to want the radiometric values.
// FLIR_VOSPI_PIXELS 14-bit counts, row-major, or NULL while idle.
const uint16_t *StillCapture_GetRawFrame(void);

#ifdef __cplusplus
}
#endif

#endif /* STILL_CAPTURE_H */

/* *****************************************************************************
 End of File
 */
