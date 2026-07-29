/*******************************************************************************
  FLIR Lepton 3.5 Driver -- Top-Level Control

  File Name:
    flir.c

  Summary:
    Power/boot state machine and per-frame rendering for the Lepton 3.5. See
    flir.h for the design and the boot sequence rationale.
*******************************************************************************/

#include "application/flir/flir.h"
#include "application/flir/flir_cci.h"
#include "application/flir/flir_vospi.h"
#include "application/flir/flir_process.h"
#include "application/error_handler.h"
#include "core/device_control.h"
#include "core/watchdog_timer.h"
#include "gpio/pin_macros.h"
#include "glcd/glcd.h"
#include "usb_uart/terminal_control.h"

#include <xc.h>
#include <string.h>
#include <stdio.h>

// CP0 Count runs at SYSCLK/2, so this many ticks per millisecond.
#define FLIR_TICKS_PER_MS       ((uint32_t)(SYSCLK_INT / 2u) / 1000u)

// Bring-up timing (see the FLIR Lepton engineering datasheet):
//  - a rail should reach PGOOD almost immediately; bound it so a dead rail
//    faults instead of hanging.
//  - the master clock and the power-down release each need >=5000 master-clock
//    cycles (~200us at 25MHz) to settle; 1ms is a comfortable margin.
//  - the camera needs ~950ms after reset release before the CCI reports boot
//    complete; poll for a further window beyond that before giving up.
#define FLIR_PGOOD_TIMEOUT_MS   100u
#define FLIR_CLK_SETTLE_MS      1u
#define FLIR_PWRDWN_SETTLE_MS   1u
#define FLIR_BOOT_WAIT_MS       1000u
#define FLIR_BOOT_POLL_MS       2000u

static volatile FLIR_STATE flirState = FLIR_STATE_OFF;

// CP0 Count captured when reset was released; the boot waits are measured from
// here in FLIR_Tasks().
static uint32_t flirBootStartTicks;

// Busy-delay in milliseconds using CP0 Count. Short waits only (the long boot
// wait is non-blocking, in FLIR_Tasks()); kicks the watchdog to be safe.
static void FLIR_DelayMs(uint32_t ms)
{
    uint32_t start = _CP0_GET_COUNT();
    uint32_t ticks = ms * FLIR_TICKS_PER_MS;

    while ((_CP0_GET_COUNT() - start) < ticks)
    {
        kickTheDog();
    }
}

// Bounded wait on a PGOOD expression. Implemented as a macro so it can take a
// live pin macro (which reads a volatile SFR bit) rather than a snapshot.
#define FLIR_WAIT_PGOOD(pgood_pin)                                            \
    ({                                                                        \
        uint32_t _start = _CP0_GET_COUNT();                                   \
        uint32_t _ticks = FLIR_PGOOD_TIMEOUT_MS * FLIR_TICKS_PER_MS;          \
        bool _ok = true;                                                      \
        while ((pgood_pin) == LOW) {                                          \
            kickTheDog();                                                     \
            if ((_CP0_GET_COUNT() - _start) > _ticks) { _ok = false; break; } \
        }                                                                     \
        _ok;                                                                  \
    })

// Clears the thermal video layer so the last captured frame doesn't linger
// once capture stops.
static void FLIR_BlankVideoLayer(void)
{
    memset((void *)GLCD_FRAMEBUFFER_BASE_ADDRESS, 0, GLCD_FRAMEBUFFER_SIZE_BYTES);
}

static void FLIR_HardOff(void)
{
    // Assert power-down and reset, gate the master clock, drop both rails.
    // powerDownSensor() in application/power_saving.c calls FLIR_PowerOff()
    // (which lands here) rather than repeating this sequence.
    FLIR_VOSPI_Stop();

    nFLIR_PWR_DWN_PIN = LOW;
    nFLIR_RESET_PIN = LOW;
    FLIR_CLK_EN_PIN = LOW;

    POS2P8_RUN_PIN = LOW;
    POS1P2_RUN_PIN = LOW;
}

// Fault path for a camera that is powered but not talking (boot never
// completed, or the CCI configuration was rejected). Capture is disarmed but
// the rails, master clock, and reset release are deliberately LEFT UP: an
// unpowered Lepton clamps SDA/SCL low and would take the rest of I2C1 down
// with it, so a mute camera must not cost the board its I2C bus. Only a rail
// that never reached PGOOD gets the full FLIR_HardOff() (see FLIR_PowerOn()).
static void FLIR_FaultKeepPowered(void)
{
    FLIR_VOSPI_Stop();
    flirState = FLIR_STATE_FAULT;
}

bool FLIR_PowerOn(void)
{
    if (flirState != FLIR_STATE_OFF && flirState != FLIR_STATE_FAULT)
    {
        return true;   // already on / coming up
    }

    // Start from a known-off state.
    FLIR_HardOff();

    // 1. Enable the rails that serve only the Lepton (+1.2V then +2.8V) and
    //    wait for both to reach PGOOD.
    POS1P2_RUN_PIN = HIGH;
    POS2P8_RUN_PIN = HIGH;

    if (!FLIR_WAIT_PGOOD(POS1P2_PGOOD_PIN) || !FLIR_WAIT_PGOOD(POS2P8_PGOOD_PIN))
    {
        // The one fault that does drop the rails: a regulator that never
        // reaches PGOOD is not powering the module anyway (so the I2C bus is
        // already lost) and may be sitting into a short, which is not
        // something to leave enabled.
        error_handler.flags.flir_rail_pgood_timeout = 1;
        FLIR_HardOff();
        flirState = FLIR_STATE_FAULT;
        return false;
    }

    // 2. Start the master clock (on-board crystal X2401, gated by FLIR_CLK_EN)
    //    and let it settle.
    FLIR_CLK_EN_PIN = HIGH;
    FLIR_DelayMs(FLIR_CLK_SETTLE_MS);

    // 3. Release power-down, settle, then release reset.
    nFLIR_PWR_DWN_PIN = HIGH;
    FLIR_DelayMs(FLIR_PWRDWN_SETTLE_MS);
    nFLIR_RESET_PIN = HIGH;

    // 4. The camera now boots (~950ms); FLIR_Tasks() polls the CCI from here.
    flirBootStartTicks = _CP0_GET_COUNT();
    flirState = FLIR_STATE_BOOTING;
    return true;
}

bool FLIR_WaitUntilReady(void)
{
    // FLIR_Tasks() bounds both waits itself (the boot poll times out into
    // FAULT, the CCI configuration is one-shot), so this cannot spin forever.
    while (flirState == FLIR_STATE_BOOTING || flirState == FLIR_STATE_CONFIGURING)
    {
        kickTheDog();
        FLIR_Tasks();
    }

    return (flirState == FLIR_STATE_READY);
}

bool FLIR_StreamOn(void)
{
    if (flirState == FLIR_STATE_STREAMING)
    {
        return true;
    }

    if (flirState != FLIR_STATE_READY)
    {
        return false;
    }

    FLIR_VOSPI_Start();
    flirState = FLIR_STATE_STREAMING;
    return true;
}

void FLIR_StreamOff(void)
{
    if (flirState != FLIR_STATE_STREAMING)
    {
        return;
    }

    // Capture off only -- rails, master clock, and reset stay where they are,
    // so the sensor keeps its configuration and its I2C pins keep driving.
    FLIR_VOSPI_Stop();
    FLIR_BlankVideoLayer();

    flirState = FLIR_STATE_READY;
}

void FLIR_PowerOff(void)
{
    FLIR_HardOff();

    // Blank Layer 0 so the last thermal frame doesn't linger on screen.
    FLIR_BlankVideoLayer();

    flirState = FLIR_STATE_OFF;
}

void FLIR_Tasks(void)
{
    switch (flirState)
    {
        case FLIR_STATE_BOOTING:
        {
            uint32_t elapsed = _CP0_GET_COUNT() - flirBootStartTicks;

            // Give the camera its boot time before touching the CCI.
            if (elapsed < (FLIR_BOOT_WAIT_MS * FLIR_TICKS_PER_MS))
            {
                break;
            }

            if (FLIR_CCI_IsBooted())
            {
                flirState = FLIR_STATE_CONFIGURING;
            }
            else if (elapsed > ((FLIR_BOOT_WAIT_MS + FLIR_BOOT_POLL_MS) * FLIR_TICKS_PER_MS))
            {
                error_handler.flags.flir_boot_timeout = 1;
                FLIR_FaultKeepPowered();
            }
            break;
        }

        case FLIR_STATE_CONFIGURING:
            if (FLIR_CCI_ConfigureRaw14Video())
            {
                // Powered and configured, but capture stays disarmed until
                // "FLIR Stream On" (FLIR_StreamOn()).
                flirState = FLIR_STATE_READY;
            }
            else
            {
                error_handler.flags.flir_cci_error = 1;
                FLIR_FaultKeepPowered();
            }
            break;

        case FLIR_STATE_STREAMING:
            // Arms capture when the VoSPI /CS idle window expires, and forces
            // a re-alignment if the packet stream stalls.
            FLIR_VOSPI_Tasks();

            if (FLIR_VOSPI_FrameReady())
            {
                const uint16_t *frame = FLIR_VOSPI_TakeFrame();
                if (frame != NULL)
                {
                    FLIRProcess_RenderToLayer0(frame);
                    kickTheDog();
                }
            }
            break;

        case FLIR_STATE_OFF:
        case FLIR_STATE_READY:
        case FLIR_STATE_FAULT:
        default:
            break;
    }
}

FLIR_STATE FLIR_GetState(void)
{
    return flirState;
}

void FLIR_SetPalette(FLIR_PALETTE palette)
{
    FLIRProcess_SetPalette(palette);
}

static const char *FLIR_StateString(FLIR_STATE state)
{
    switch (state)
    {
        case FLIR_STATE_OFF:         return "OFF";
        case FLIR_STATE_BOOTING:     return "BOOTING";
        case FLIR_STATE_CONFIGURING: return "CONFIGURING";
        case FLIR_STATE_READY:       return "READY (video idle)";
        case FLIR_STATE_STREAMING:   return "STREAMING";
        case FLIR_STATE_FAULT:       return "FAULT";
        default:                     return "?";
    }
}

void FLIR_PrintStatus(void)
{
    uint16_t agcMin = 0, agcMax = 0;
    FLIR_VOSPI_STATS vs;

    // Module-level view: state, board control signals, image-processing
    // settings, CCI, and capture health. The MCU SPI4/DMA/INT1 register
    // settings are separate -- "Peripheral Status? FLIR SPI".
    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, BOLD_FONT);
    printf("    --- FLIR Lepton 3.5 ---\n\r");

    if (flirState == FLIR_STATE_FAULT) terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
    else terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("    State: %s\n\r", FLIR_StateString(flirState));

    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("    Control signals: CLK_EN=%u  nPWR_DWN=%u  nRESET=%u   Rails: +1.2V PGOOD=%u  +2.8V PGOOD=%u\n\r",
           (unsigned)FLIR_CLK_EN_PIN, (unsigned)nFLIR_PWR_DWN_PIN, (unsigned)nFLIR_RESET_PIN,
           (unsigned)POS1P2_PGOOD_PIN, (unsigned)POS2P8_PGOOD_PIN);

    printf("    Palette: %s\n\r",
           (FLIRProcess_GetPalette() == FLIR_PALETTE_GRAYSCALE) ? "Grayscale" : "Ironbow");

    // Pixels are 16-bit TLinear (centi-Kelvin), so the AGC window doubles as
    // the scene's temperature span -- a good sanity check on live data.
    FLIRProcess_GetAGCWindow(&agcMin, &agcMax);
    printf("    AGC window: %u .. %u counts (%.1f .. %.1f C)\n\r",
           agcMin, agcMax, ((float)agcMin / 100.0f) - 273.15f,
           ((float)agcMax / 100.0f) - 273.15f);

    // CCI status + temperatures (only meaningful once powered/booted).
    if (flirState != FLIR_STATE_OFF)
    {
        FLIR_CCI_PrintStatus();
    }

    // VoSPI capture health (statistics, not peripheral settings).
    FLIR_VOSPI_GetStats(&vs);
    printf("    Capture: %s  frames=%lu\n\r",
           FLIR_VOSPI_GetCaptureStateString(), (unsigned long)vs.framesCaptured);
    printf("    Packets: %lu captured, %lu discards (%lu carrying data), last %lu ms ago\n\r",
           (unsigned long)vs.packetsCaptured, (unsigned long)vs.discardPackets,
           (unsigned long)(vs.packetsCaptured - vs.discardPackets),
           (unsigned long)FLIR_VOSPI_MsSinceLastPacket());
    printf("    Framing: desyncs=%lu  resyncs=%lu  segment errors=%lu  RX overflows=%lu\n\r",
           (unsigned long)vs.desyncCount, (unsigned long)vs.resyncCount,
           (unsigned long)vs.segmentErrors, (unsigned long)vs.rxOverflows);
    printf("    Progress: segments placed=%lu  longest in-sequence run=%lu packets "
           "(60 = one segment)\n\r",
           (unsigned long)vs.segmentsPlaced, (unsigned long)vs.maxPacketRun);
    printf("    Segment restarts (early packet 0, normal): %lu\n\r",
           (unsigned long)vs.segmentRestarts);

    // Placements per segment. Heavily skewed toward segment 1 means the frame
    // accumulation is being reset between segments rather than continuing.
    printf("    Segments placed by id:");
    {
        uint8_t s;
        for (s = 1u; s <= FLIR_VOSPI_SEGMENTS_PER_FRAME; s++)
            printf("  %u:%lu", s, (unsigned long)vs.segmentsPlacedById[s]);
        printf("\n\r");
    }
    printf("    Stream stalls recovered (partial DMA block): %lu\n\r",
           (unsigned long)vs.stallRecoveries);

    // Distribution of the packet-20 segment field. 0 = "segment not valid"
    // (normal); 1..4 are the real segments; anything in 5..7 should never
    // appear, and an even spread means the field isn't the segment number.
    printf("    Segment IDs seen at packet 20:");
    {
        uint8_t s;
        for (s = 0; s < 8u; s++) printf("  %u:%lu", s, (unsigned long)vs.segmentIdSeen[s]);
        printf("\n\r");
    }

    // vsyncs=0 is normal: the sensor's GPIO3 VSYNC output is not enabled (it
    // needs a protected OEM CCI command), and capture does not use it.
    printf("    VSYNC edges: %lu (sensor VSYNC output not enabled -- informational only)\n\r",
           (unsigned long)vs.vsyncCount);

    terminalTextAttributesReset();
}
