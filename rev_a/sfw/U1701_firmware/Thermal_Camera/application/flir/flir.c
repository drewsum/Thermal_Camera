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

static void FLIR_HardOff(void)
{
    // Assert power-down and reset, gate the master clock, drop both rails.
    // Mirrors powerDownIndicatorsAndSensor() in application/power_saving.c --
    // keep the two in sync.
    FLIR_VOSPI_Stop();

    nFLIR_PWR_DWN_PIN = LOW;
    nFLIR_RESET_PIN = LOW;
    FLIR_CLK_EN_PIN = LOW;

    POS2P8_RUN_PIN = LOW;
    POS1P2_RUN_PIN = LOW;
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

void FLIR_PowerOff(void)
{
    FLIR_HardOff();

    // Blank Layer 0 so the last thermal frame doesn't linger on screen.
    memset((void *)GLCD_FRAMEBUFFER_BASE_ADDRESS, 0, GLCD_FRAMEBUFFER_SIZE_BYTES);

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
                FLIR_HardOff();
                flirState = FLIR_STATE_FAULT;
            }
            break;
        }

        case FLIR_STATE_CONFIGURING:
            if (FLIR_CCI_ConfigureRaw14Video())
            {
                FLIR_VOSPI_Start();
                flirState = FLIR_STATE_STREAMING;
            }
            else
            {
                error_handler.flags.flir_cci_error = 1;
                FLIR_HardOff();
                flirState = FLIR_STATE_FAULT;
            }
            break;

        case FLIR_STATE_STREAMING:
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

    FLIRProcess_GetAGCWindow(&agcMin, &agcMax);
    printf("    AGC window (14-bit counts): %u .. %u\n\r", agcMin, agcMax);

    // CCI status + temperatures (only meaningful once powered/booted).
    if (flirState != FLIR_STATE_OFF)
    {
        FLIR_CCI_PrintStatus();
    }

    // VoSPI capture health (statistics, not peripheral settings).
    FLIR_VOSPI_GetStats(&vs);
    printf("    Capture: frames=%lu  desyncs=%lu  segment errors=%lu  vsyncs=%lu\n\r",
           (unsigned long)vs.framesCaptured, (unsigned long)vs.desyncCount,
           (unsigned long)vs.segmentErrors, (unsigned long)vs.vsyncCount);

    terminalTextAttributesReset();
}
