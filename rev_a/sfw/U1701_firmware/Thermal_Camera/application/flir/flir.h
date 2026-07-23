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
    The sensor boots OFF -- rails down, clock gated, held in reset -- which is
    the GPIO boot state (gpio/pic32mzda_gpio_setup.c), so main() starts nothing.
    FLIR_PowerOn() runs the datasheet bring-up sequence (rails -> master clock ->
    de-assert power-down -> de-assert reset), then FLIR_Tasks() advances the long
    non-blocking waits (the camera needs ~950ms to boot) so the console, USB,
    and telemetry keep running meanwhile. Once booted and configured for RAW14,
    capture starts and every completed frame is AGC'd, colorized, and blitted to
    Layer 0. FLIR_PowerOff() tears the whole thing back down.

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
    FLIR_STATE_STREAMING,   // capturing + rendering frames
    FLIR_STATE_FAULT        // bring-up failed (see the latched flir_* flag)
} FLIR_STATE;

// Runs the fast, deterministic part of the power-up sequence (rails, master
// clock, de-assert power-down and reset) and hands off to the non-blocking
// boot wait in FLIR_Tasks(). Returns false (and latches an error flag) if a
// rail never reached PGOOD; true once the sequence is under way. No-op if the
// sensor is already on.
bool FLIR_PowerOn(void);

// Stops capture and returns the sensor to its off state (held in reset,
// power-down asserted, clock gated, both rails down) and blanks Layer 0.
void FLIR_PowerOff(void);

// Advances the boot/config state machine and renders any newly captured frame.
// Call once per main-loop iteration.
void FLIR_Tasks(void);

// Current state of the driver.
FLIR_STATE FLIR_GetState(void);

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
