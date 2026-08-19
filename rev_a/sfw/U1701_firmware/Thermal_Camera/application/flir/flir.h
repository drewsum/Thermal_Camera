/*******************************************************************************
  FLIR Lepton 3.5 Driver -- Top-Level Control

  File Name:
    flir.h

  Summary:
    Orchestrates the FLIR Lepton 3.5: the power/clock/reset bring-up sequence,
    the CCI boot handshake and video configuration, VoSPI capture, and rendering
    each frame to GLCD Layer 0. This is the façade the UART commands and main()
    call; the sub-drivers (flir_cci.c, flir_vospi.c, flir_process.c) do the work.

  Description:
    Power and streaming are deliberately separate here. The module MUST be
    powered whenever the board is running: with its +2.8V/+1.2V rails down the
    unpowered Lepton clamps SDA/SCL low through its I/O structures and takes
    every other device on I2C1 down with it. So main() calls FLIR_PowerOn()
    early -- before I2C_Initialize() -- and the sensor stays powered, booted,
    and configured from then on. Only the video stream is on demand:
    FLIR_StreamOn()/FLIR_StreamOff() arm and disarm VoSPI capture, and
    FLIR_PowerOff() (a debug-only escape hatch, "FLIR Power Off") is the one
    thing that puts the rails back down.

    FLIR_PowerOn() runs the datasheet bring-up sequence (rails -> master clock ->
    de-assert power-down -> de-assert reset), then FLIR_Tasks() advances the long
    non-blocking waits (the camera needs ~950ms to boot) so the console, USB,
    and telemetry keep running meanwhile. It ends in READY: powered and
    configured for RAW14, VoSPI idle. From STREAMING, every completed frame is
    AGC'd, colorized, and blitted to Layer 0.

    Faults latch into the error handler (flir_* flags, error_handler.h) and drop
    the state machine to FAULT rather than hanging.
*******************************************************************************/

#ifndef FLIR_H
#define FLIR_H

#include <stdbool.h>

#include "application/flir/flir_process.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    FLIR_STATE_OFF = 0,     // rails down, clock gated, held in reset
    FLIR_STATE_BOOTING,     // reset released, waiting for the camera to boot
    FLIR_STATE_CONFIGURING, // booted, applying RAW14/AGC/telemetry config
    FLIR_STATE_READY,       // powered + configured, VoSPI capture idle
    FLIR_STATE_STREAMING,   // capturing + rendering frames
    FLIR_STATE_FAULT        // bring-up failed (see the latched flir_* flag).
                            // The rails stay UP here so the mute module does
                            // not clamp I2C1 -- only a PGOOD failure, where
                            // the rail never came up anyway, powers down.
} FLIR_STATE;

// Runs the fast, deterministic part of the power-up sequence (rails, master
// clock, de-assert power-down and reset) and hands off to the non-blocking
// boot wait in FLIR_Tasks(). Returns false (and latches an error flag) if a
// rail never reached PGOOD; true once the sequence is under way. No-op if the
// sensor is already on. Called by main() before the I2C bus is brought up --
// see the file header for why the rails cannot be left down.
bool FLIR_PowerOn(void);

// Pumps FLIR_Tasks() until the boot wait and the CCI configuration handshake
// have finished, and returns true if the driver reached READY. Blocking, but
// bounded by the poll window in FLIR_Tasks() (~6s worst case from the first
// CCI poll -- sized to ride out the camera's automatic startup FFC, during
// which the CCI reports busy; normally the camera is long booted and this
// returns in milliseconds). Only for boot-time init; everything after that
// should let FLIR_Tasks() advance in the main loop.
bool FLIR_WaitUntilReady(void);

// Arms VoSPI capture: thermal frames start landing on Layer 0. Requires the
// driver to be READY (powered, booted, configured); returns false otherwise.
// No-op (true) if already streaming.
bool FLIR_StreamOn(void);

// Disarms VoSPI capture and blanks Layer 0, leaving the sensor powered,
// booted, and configured so FLIR_StreamOn() can resume immediately. No-op
// unless streaming.
void FLIR_StreamOff(void);

// Same as FLIR_StreamOff(), but leaves the Layer 0 buffers alone instead of
// blanking them. For the still-capture path (application/still_capture.c),
// which has already repointed Layer 0 at its own frozen frame and must not
// have the video layer cleared out from under it -- see the implementation
// for the flip/blank race that makes this a separate entry point.
void FLIR_StreamOffKeepImage(void);

// Stops capture and returns the sensor to its off state (held in reset,
// power-down asserted, clock gated, both rails down) and blanks Layer 0.
// Debug/teardown only: while the rails are down the unpowered module holds
// I2C1 low for every other device on the bus (see the file header).
void FLIR_PowerOff(void);

// Advances the boot/config state machine and renders any newly captured frame.
// Call once per main-loop iteration.
void FLIR_Tasks(void);

// Current state of the driver.
FLIR_STATE FLIR_GetState(void);

// Human-readable name for a state, e.g. "STREAMING". Exposed (rather than
// each caller keeping its own switch) so the console and the GUI cannot
// drift apart on what a state is called -- same reason
// FLIRProcess_PaletteName() exists. Returns "?" for a value outside the enum.
const char *FLIR_StateString(FLIR_STATE state);

// Selects the thermal color palette used for rendering (thin wrapper over
// flir_process.c so the UART command doesn't need that header).
void FLIR_SetPalette(FLIR_PALETTE palette);

// Prints the full FLIR status: state, pin levels, CCI status/temperatures,
// VoSPI statistics, and the current palette/AGC window. Backs the "FLIR
// Status?" command and "Peripheral Status? FLIR".
void FLIR_PrintStatus(void);

#ifdef __cplusplus
}
#endif

#endif /* FLIR_H */
