/*******************************************************************************
  System Status GUI Screen

  File Name:
    system_screen.c

  Summary:
    See system_screen.h for the layout.
*******************************************************************************/

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "gui/screens/system_screen.h"
#include "gui/screens/screen_common.h"

#include "gui/gui.h"
#include "gui/lvgl/lvgl.h"
#include "application/error_handler.h"
#include "application/flir/flir.h"
#include "application/flir/flir_cci.h"
#include "application/flir/flir_process.h"
#include "application/flir/flir_vospi.h"
#include "application/heartbeat_services.h"
#include "application/main.h"
#include "application/telemetry.h"
#include "core/device_control.h"
#include "gpio/pin_macros.h"
#include "i2c/i2c_devices.h"

// Body panel geometry: fills the gap the two bars leave, inset slightly so
// the panel edge reads as a distinct surface over the Layer 0 image. The
// content is far taller than the panel, so the panel scrolls (see
// SystemScreen_Create()).
#define SYSTEM_SCREEN_PANEL_INSET_PX   6
#define SYSTEM_SCREEN_PANEL_PAD_PX     6
#define SYSTEM_SCREEN_ROW_HEIGHT_PX    18
#define SYSTEM_SCREEN_SECTION_HEIGHT_PX 20
#define SYSTEM_SCREEN_SECTION_GAP_PX   6
#define SYSTEM_SCREEN_HEAP_BAR_WIDTH   70
#define SYSTEM_SCREEN_HEAP_BAR_HEIGHT  10

// Right-hand lane kept clear for the scrollbar, which the default theme
// draws INSIDE the panel's right edge (4px wide, inset 6px at this display's
// 130 DPI -- LV_DPX_CALC in lv_display.h). The value labels are right
// aligned to the content area, so without this they would sit under it.
#define SYSTEM_SCREEN_SCROLLBAR_LANE_PX 14

// Longest value string this screen formats, plus room to grow. The battery
// status row (a comma-separated flag list) is the one that sets it.
#define SYSTEM_SCREEN_VALUE_MAX_CHARS  48

// The three states a value row can be in. Green and red are what the
// "Errors" row has always used; gray is for a row whose value is not
// knowable right now (a rail's PGOOD before the rail is up, the sensor
// temperature while the Lepton is not booted) -- distinct from red, which
// means the firmware looked and found something wrong.
#define SYSTEM_SCREEN_OK_COLOR         0x30C030
#define SYSTEM_SCREEN_FAULT_COLOR      0xE04040
#define SYSTEM_SCREEN_UNKNOWN_COLOR    0x808080

// Screen_CreateLabel()'s own color, for putting a row back to an ordinary
// reading after it has been gray or red
#define SYSTEM_SCREEN_VALUE_COLOR      0xFFFFFF

// *****************************************************************************
// Section: Row list
// *****************************************************************************
// Every row on the screen, top to bottom, in one list -- the same X-macro
// idiom as I2C_DEVICE_LIST (i2c/i2c_devices.h). SECTION() is a heading with
// no value; VALUE() is a name/value pair whose enum tag names the label that
// SystemScreen_Refresh() writes into.
//
// This covers everything in the telemetry struct (application/telemetry.h):
// the four ADC-derived MCU channels, the ambient sensor, all four quantities
// for each of the six monitored rails, and the full BQ27441 fuel gauge set.
// Adding a row means adding one line here -- the enum, the descriptor table,
// the layout and the row count all come off this list.
#define SYSTEM_SCREEN_ROW_LIST(SECTION, VALUE)                     \
    SECTION("System")                                              \
    VALUE(FIRMWARE,         "Firmware")                            \
    VALUE(UPTIME,           "Uptime")                              \
    VALUE(ERRORS,           "Errors")                              \
    VALUE(HEAP,             "GUI Heap")                            \
    SECTION("Elapsed Time Counter")                                \
    VALUE(ETC_TOTAL,        "Total Board Time")                    \
    VALUE(ETC_CYCLES,       "Power Cycles")                        \
    SECTION("MCU Identity")                                        \
    VALUE(MCU_PART,         "Part")                                \
    VALUE(MCU_DEVICE_ID,    "Device ID")                           \
    VALUE(MCU_REVISION,     "Revision")                            \
    VALUE(MCU_SERIAL,       "Serial Number")                       \
    SECTION("MCU")                                                 \
    VALUE(DIE_TEMP,         "Die Temperature")                     \
    VALUE(MCU_VBAT,         "VBAT Backup Cell")                    \
    VALUE(ADC_VREF,         "ADC Reference")                       \
    VALUE(AMBIENT_TEMP,     "Ambient Temperature")                 \
    SECTION("FLIR Lepton")                                         \
    VALUE(FLIR_STATE,       "State")                               \
    VALUE(FLIR_CAPTURE,     "Capture")                             \
    VALUE(FLIR_PALETTE,     "Palette")                             \
    VALUE(FLIR_SCENE,       "Scene Range")                         \
    VALUE(FLIR_FPA,         "Sensor Temperature")                  \
    VALUE(FLIR_FRAMES,      "Frames Captured")                     \
    VALUE(FLIR_LAST_PACKET, "Last Packet")                         \
    VALUE(FLIR_FRAMING,     "Framing Faults")                      \
    SECTION("+12V Input Gate")                                     \
    VALUE(POS12_V,          "Voltage")                             \
    VALUE(POS12_I,          "Current")                             \
    VALUE(POS12_P,          "Power")                               \
    VALUE(POS12_T,          "Temperature")                         \
    SECTION("+3.0V PSU")                                           \
    VALUE(POS3P0_V,         "Voltage")                             \
    VALUE(POS3P0_I,         "Current")                             \
    VALUE(POS3P0_P,         "Power")                               \
    VALUE(POS3P0_T,         "Temperature")                         \
    SECTION("+1.8V PSU")                                           \
    VALUE(POS1P8_V,         "Voltage")                             \
    VALUE(POS1P8_I,         "Current")                             \
    VALUE(POS1P8_P,         "Power")                               \
    VALUE(POS1P8_T,         "Temperature")                         \
    SECTION("+2.8V PSU")                                           \
    VALUE(POS2P8_V,         "Voltage")                             \
    VALUE(POS2P8_I,         "Current")                             \
    VALUE(POS2P8_P,         "Power")                               \
    VALUE(POS2P8_T,         "Temperature")                         \
    SECTION("+1.2V PSU")                                           \
    VALUE(POS1P2_V,         "Voltage")                             \
    VALUE(POS1P2_I,         "Current")                             \
    VALUE(POS1P2_P,         "Power")                               \
    VALUE(POS1P2_T,         "Temperature")                         \
    SECTION("Backlight PSU")                                       \
    VALUE(BACKLIGHT_V,      "Voltage")                             \
    VALUE(BACKLIGHT_I,      "Current")                             \
    VALUE(BACKLIGHT_P,      "Power")                               \
    VALUE(BACKLIGHT_T,      "Temperature")                         \
    SECTION("Power Good")                                          \
    VALUE(PGOOD_12V,        "+12V")                                \
    VALUE(PGOOD_3P3_USB,    "+3.3V USB")                           \
    VALUE(PGOOD_3P0,        "+3.0V")                               \
    VALUE(PGOOD_2P8,        "+2.8V")                               \
    VALUE(PGOOD_1P8,        "+1.8V")                               \
    VALUE(PGOOD_1P2,        "+1.2V")                               \
    SECTION("Battery")                                             \
    VALUE(BATT_PRESENT,     "Installed")                           \
    VALUE(BATT_V,           "Voltage")                             \
    VALUE(BATT_I,           "Current")                             \
    VALUE(BATT_T,           "Temperature")                         \
    VALUE(BATT_SOC,         "State of Charge")                     \
    VALUE(BATT_SOH,         "State of Health")                     \
    VALUE(BATT_REMCAP,      "Remaining Capacity")                  \
    VALUE(BATT_FULLCAP,     "Full Charge Capacity")                \
    VALUE(BATT_STATUS,      "Status")                              \
    SECTION("Charger (MAX8903)")                                   \
    VALUE(CHG_FAULT,        "Fault")                               \
    VALUE(CHG_DC_INPUT,     "DC Input")                            \
    VALUE(CHG_USB_INPUT,    "USB Input")                           \
    VALUE(CHG_CHARGING,     "Charging")                            \
    VALUE(CHG_ENABLE,       "Charge Enable")                       \
    VALUE(CHG_USB_CURRENT,  "USB Current Limit")                   \
    VALUE(CHG_GAUGE_GPOUT,  "Gauge Low-Battery Pin")

// The value rows only -- this is what indexes value_labels[] below
#define SYSTEM_ROW_ENUM_SECTION(label)
#define SYSTEM_ROW_ENUM_VALUE(name, label)   SYSTEM_ROW_##name,

typedef enum
{
    SYSTEM_SCREEN_ROW_LIST(SYSTEM_ROW_ENUM_SECTION, SYSTEM_ROW_ENUM_VALUE)
    SYSTEM_ROW_COUNT
} SYSTEM_SCREEN_ROW;

// Every row in display order, sections included, so the build loop can lay
// them out in one pass. `value_row` is meaningless when is_section is true.
typedef struct
{
    bool is_section;
    const char *label;
    SYSTEM_SCREEN_ROW value_row;
} SYSTEM_SCREEN_ROW_DEF;

#define SYSTEM_ROW_DEF_SECTION(label)        { true,  label, 0 },
#define SYSTEM_ROW_DEF_VALUE(name, label)    { false, label, SYSTEM_ROW_##name },

static const SYSTEM_SCREEN_ROW_DEF system_rows[] =
{
    SYSTEM_SCREEN_ROW_LIST(SYSTEM_ROW_DEF_SECTION, SYSTEM_ROW_DEF_VALUE)
};

#define SYSTEM_SCREEN_ROW_TOTAL  (sizeof(system_rows) / sizeof(system_rows[0]))

// Maps each monitored rail's four rows to the telemetry struct member they
// read, so the refresh is one loop rather than 24 near-identical blocks.
// Rows are named explicitly rather than derived from the first one, so the
// list above can be reordered without silently reassigning values.
typedef struct
{
    SYSTEM_SCREEN_ROW voltage_row;
    SYSTEM_SCREEN_ROW current_row;
    SYSTEM_SCREEN_ROW power_row;
    SYSTEM_SCREEN_ROW temperature_row;
    volatile const telemetry_parameters_ps_t *source;
} SYSTEM_SCREEN_RAIL;

static const SYSTEM_SCREEN_RAIL system_rails[] =
{
    { SYSTEM_ROW_POS12_V,     SYSTEM_ROW_POS12_I,     SYSTEM_ROW_POS12_P,     SYSTEM_ROW_POS12_T,     &telemetry.pos12     },
    { SYSTEM_ROW_POS3P0_V,    SYSTEM_ROW_POS3P0_I,    SYSTEM_ROW_POS3P0_P,    SYSTEM_ROW_POS3P0_T,    &telemetry.pos3p0    },
    { SYSTEM_ROW_POS1P8_V,    SYSTEM_ROW_POS1P8_I,    SYSTEM_ROW_POS1P8_P,    SYSTEM_ROW_POS1P8_T,    &telemetry.pos1p8    },
    { SYSTEM_ROW_POS2P8_V,    SYSTEM_ROW_POS2P8_I,    SYSTEM_ROW_POS2P8_P,    SYSTEM_ROW_POS2P8_T,    &telemetry.pos2p8    },
    { SYSTEM_ROW_POS1P2_V,    SYSTEM_ROW_POS1P2_I,    SYSTEM_ROW_POS1P2_P,    SYSTEM_ROW_POS1P2_T,    &telemetry.pos1p2    },
    { SYSTEM_ROW_BACKLIGHT_V, SYSTEM_ROW_BACKLIGHT_I, SYSTEM_ROW_BACKLIGHT_P, SYSTEM_ROW_BACKLIGHT_T, &telemetry.backlight },
};

#define SYSTEM_SCREEN_RAIL_COUNT  (sizeof(system_rails) / sizeof(system_rails[0]))

static SCREEN_HEADER header;
static lv_obj_t *value_labels[SYSTEM_ROW_COUNT] = { NULL };
static lv_obj_t *heap_bar = NULL;

// Counts the error handler flags currently latched. The flag array covers
// both the base flags and the per-I2C-device ones (error_handler.h builds
// both from its X-macro lists), so this is every fault the firmware knows
// how to record.
static uint32_t SystemScreen_CountLatchedErrors(void)
{
    uint32_t count = 0;
    uint32_t index;

    for (index = 0; index < ERROR_HANDLER_NUM_FLAGS; index++)
    {
        if (error_handler.flag_array[index]) count++;
    }

    return count;
}

// Writes a row's value, but ONLY when the text actually changed.
//
// This matters much more here than on the other screens: this one has ~40
// live rows, and lv_label_set_text() invalidates unconditionally, so a blind
// rewrite would re-render every row of the panel twice a second -- software
// rendering into DDR2, competing with the FLIR video path. Comparing against
// the label's own stored string costs a strcmp and needs no shadow copy.
// Same principle as the UART live telemetry page, which only sends the rows
// whose text changed.
static void SystemScreenSetValue(SYSTEM_SCREEN_ROW row, const char *text)
{
    lv_obj_t *label = value_labels[row];

    if (label == NULL) return;
    if (strcmp(lv_label_get_text(label), text) == 0) return;

    lv_label_set_text(label, text);
}

// snprintf() rather than lv_label_set_text_fmt(): LVGL's built-in sprintf
// has no floating point conversions, and XC32's does.
static void SystemScreenSetValueFmt(SYSTEM_SCREEN_ROW row, const char *format, ...)
{
    char text[SYSTEM_SCREEN_VALUE_MAX_CHARS];
    va_list args;

    va_start(args, format);
    vsnprintf(text, sizeof(text), format, args);
    va_end(args);

    SystemScreenSetValue(row, text);
}

// Writes a row's value and colors it. Used by the rows whose value is a
// verdict rather than a number -- the PGOOD lines, the charger pins, the
// FLIR state -- where the color is doing as much work as the text.
//
// The color is set unconditionally while the text is not: lv_obj_set_style_*
// only invalidates when the value actually changes, so a repeated write of
// the same color is already free, whereas lv_label_set_text() is not (see
// SystemScreenSetValue()).
static void SystemScreenSetVerdict(SYSTEM_SCREEN_ROW row, const char *text, uint32_t color)
{
    if (value_labels[row] == NULL) return;

    SystemScreenSetValue(row, text);
    lv_obj_set_style_text_color(value_labels[row], lv_color_hex(color), LV_PART_MAIN);
}

// The common case of the above: a two-state signal shown as one of two
// words, green when `ok` and red otherwise.
static void SystemScreenSetOkBad(SYSTEM_SCREEN_ROW row, bool ok,
        const char *ok_text, const char *bad_text)
{
    SystemScreenSetVerdict(row, ok ? ok_text : bad_text,
            ok ? SYSTEM_SCREEN_OK_COLOR : SYSTEM_SCREEN_FAULT_COLOR);
}

// Appends `name` to a comma-separated list in `buffer` when `set` is true.
static void SystemScreenAppendFlag(char *buffer, size_t size, bool set, const char *name)
{
    size_t length;

    if (!set) return;

    length = strlen(buffer);

    snprintf(buffer + length, size - length, "%s%s", (length > 0) ? ", " : "", name);
}

// The MCU identity rows: part number, device ID, silicon revision and the
// 64-bit serial, all read straight out of flash (core/device_control.c).
// Written once from Create() rather than every refresh -- none of it can
// change while the part is running.
static void SystemScreenSetIdentity(void)
{
    uint32_t device_id = getDeviceID();
    uint8_t revision_id = getRevisionID();

    SystemScreenSetValue(SYSTEM_ROW_MCU_PART, getDeviceIDString(device_id));
    SystemScreenSetValueFmt(SYSTEM_ROW_MCU_DEVICE_ID, "0x%07lX", (unsigned long)device_id);
    SystemScreenSetValueFmt(SYSTEM_ROW_MCU_REVISION, "%s (0x%X)",
            getRevisionIDString(revision_id), (unsigned int)revision_id);
    SystemScreenSetValue(SYSTEM_ROW_MCU_SERIAL, getStringSerialNumber());
}

// Elapsed time counter (DS1683, I2C_DEV_ETR_1): total powered-on time and
// power-cycle count, both maintained by the recorder itself across resets
// and power loss.
//
// Unlike everything else on this screen, these are BLOCKING I2C reads --
// there is no queued path for this device -- so they are throttled to the
// counter's own 1-second resolution rather than run at the 500ms refresh
// rate. That keeps the main loop (and the FLIR video path it services) out
// of an avoidable I2C wait every other refresh.
#define SYSTEM_SCREEN_ETC_READ_INTERVAL_MS  1000u

static bool etc_ever_read = false;
static uint32_t etc_last_read_ms = 0;

static void SystemScreenRefreshElapsedTime(void)
{
    uint32_t now = GUI_GetTickMs();
    uint32_t elapsed_seconds;
    uint16_t power_cycles;

    // Guarded on presence: I2CDevices_ReadElapsedSeconds() latches an I2C
    // error flag when it fails, and Create() runs its first refresh during
    // the splash fast path -- well before main.c probes the I2C devices --
    // so an unguarded read would report a fault against a device that simply
    // hasn't been probed yet.
    if (!I2CDevices_IsPresent(I2C_DEV_ETR_1))
    {
        SystemScreenSetValue(SYSTEM_ROW_ETC_TOTAL, "unavailable");
        SystemScreenSetValue(SYSTEM_ROW_ETC_CYCLES, "unavailable");
        return;
    }

    if (etc_ever_read && ((now - etc_last_read_ms) < SYSTEM_SCREEN_ETC_READ_INTERVAL_MS))
    {
        return;
    }

    etc_ever_read = true;
    etc_last_read_ms = now;

    if (I2CDevices_ReadElapsedSeconds(I2C_DEV_ETR_1, &elapsed_seconds))
    {
        SystemScreenSetValueFmt(SYSTEM_ROW_ETC_TOTAL, "%lud %02lu:%02lu:%02lu",
                (unsigned long)(elapsed_seconds / 86400u),
                (unsigned long)((elapsed_seconds / 3600u) % 24u),
                (unsigned long)((elapsed_seconds / 60u) % 60u),
                (unsigned long)(elapsed_seconds % 60u));
    }
    else
    {
        SystemScreenSetValue(SYSTEM_ROW_ETC_TOTAL, "read failed");
    }

    if (I2CDevices_ReadEventCount(I2C_DEV_ETR_1, &power_cycles))
    {
        SystemScreenSetValueFmt(SYSTEM_ROW_ETC_CYCLES, "%u", (unsigned int)power_cycles);
    }
    else
    {
        SystemScreenSetValue(SYSTEM_ROW_ETC_CYCLES, "read failed");
    }
}

// FLIR Lepton: the module-level view of "FLIR Status?" -- state machine,
// capture health and image-processing settings. Everything except the sensor
// temperature is a cached read (a state variable, the VoSPI statistics block,
// the palette and the last render's AGC window), so it costs nothing.
//
// The sensor temperature is the exception: FLIR_CCI_ReadTemperatures() is a
// blocking I2C1 transaction to the camera, so it gets the same treatment as
// the DS1683 above -- throttled well below the refresh rate. The interval is
// longer than the elapsed-time counter's because an FPA temperature moves
// slowly and this bus is shared with the whole telemetry set.
//
// This is the ONLY new periodic hardware access this screen's FLIR section
// added, and the only one that touches the bus the Lepton itself is on while
// VoSPI is streaming. Set the switch to 0 if the video feed ever proves
// sensitive to it: the row then reads "--" and every other FLIR row keeps
// working, since the rest are cached reads.
#define SYSTEM_SCREEN_READ_FPA_TEMPERATURE  1
#define SYSTEM_SCREEN_FPA_READ_INTERVAL_MS  2000u

static bool fpa_ever_read = false;
static uint32_t fpa_last_read_ms = 0;

static void SystemScreenRefreshFlirTemperature(FLIR_STATE state)
{
#if !SYSTEM_SCREEN_READ_FPA_TEMPERATURE

    (void)state;

    SystemScreenSetVerdict(SYSTEM_ROW_FLIR_FPA, "--", SYSTEM_SCREEN_UNKNOWN_COLOR);

#else

    uint32_t now = GUI_GetTickMs();
    float fpa_celsius = 0.0f;
    float aux_celsius = 0.0f;

    // The CCI only answers once the camera has booted and been configured.
    // Asking earlier (or in FAULT, where the bring-up did not finish) would
    // latch an I2C error against a device that is simply not listening yet.
    if ((state != FLIR_STATE_READY) && (state != FLIR_STATE_STREAMING))
    {
        SystemScreenSetVerdict(SYSTEM_ROW_FLIR_FPA, "--", SYSTEM_SCREEN_UNKNOWN_COLOR);

        // Re-read as soon as it does come up, rather than waiting out an
        // interval that started while it was unreachable
        fpa_ever_read = false;
        return;
    }

    if (fpa_ever_read && ((now - fpa_last_read_ms) < SYSTEM_SCREEN_FPA_READ_INTERVAL_MS))
    {
        return;
    }

    fpa_ever_read = true;
    fpa_last_read_ms = now;

    if (FLIR_CCI_ReadTemperatures(&fpa_celsius, &aux_celsius))
    {
        // The FPA (the detector array itself) is the one that matters: it is
        // what the radiometry is referenced to, and a climbing FPA
        // temperature is what precedes a flat-field correction.
        char text[SYSTEM_SCREEN_VALUE_MAX_CHARS];

        snprintf(text, sizeof(text), "%.1f C FPA / %.1f C aux",
                fpa_celsius, aux_celsius);

        SystemScreenSetVerdict(SYSTEM_ROW_FLIR_FPA, text, SYSTEM_SCREEN_VALUE_COLOR);
    }
    else
    {
        SystemScreenSetVerdict(SYSTEM_ROW_FLIR_FPA, "read failed",
                SYSTEM_SCREEN_FAULT_COLOR);
    }

#endif  /* SYSTEM_SCREEN_READ_FPA_TEMPERATURE */
}

static void SystemScreenRefreshFlir(void)
{
    FLIR_STATE state = FLIR_GetState();
    FLIR_VOSPI_STATS stats;
    float scene_min = 0.0f;
    float scene_max = 0.0f;
    uint32_t framing_faults;

    // FAULT is the only state that is wrong in itself; the rest are stages
    // of a working sequence, and STREAMING is the one that means video is
    // actually arriving.
    SystemScreenSetVerdict(SYSTEM_ROW_FLIR_STATE, FLIR_StateString(state),
            (state == FLIR_STATE_FAULT)     ? SYSTEM_SCREEN_FAULT_COLOR :
            (state == FLIR_STATE_STREAMING) ? SYSTEM_SCREEN_OK_COLOR :
                                              SYSTEM_SCREEN_UNKNOWN_COLOR);

    SystemScreenSetValue(SYSTEM_ROW_FLIR_CAPTURE, FLIR_VOSPI_GetCaptureStateString());
    SystemScreenSetValue(SYSTEM_ROW_FLIR_PALETTE,
            FLIRProcess_PaletteName(FLIRProcess_GetPalette()));

    // The AGC window doubles as the scene's temperature span, since the
    // pixels are TLinear centi-Kelvin -- so this row is a live sanity check
    // on the data behind the image.
    if (FLIRProcess_GetAGCWindowCelsius(&scene_min, &scene_max))
    {
        SystemScreenSetValueFmt(SYSTEM_ROW_FLIR_SCENE, "%.1f .. %.1f C",
                scene_min, scene_max);
    }
    else
    {
        SystemScreenSetValue(SYSTEM_ROW_FLIR_SCENE, "--");
    }

    FLIR_VOSPI_GetStats(&stats);

    SystemScreenSetValueFmt(SYSTEM_ROW_FLIR_FRAMES, "%lu",
            (unsigned long)stats.framesCaptured);

    // Time since the last packet is what separates "not streaming" from
    // "streaming but the link has died" -- the frame counter alone looks the
    // same in both cases once it stops moving.
    if (state == FLIR_STATE_STREAMING)
    {
        SystemScreenSetValueFmt(SYSTEM_ROW_FLIR_LAST_PACKET, "%lu ms ago",
                (unsigned long)FLIR_VOSPI_MsSinceLastPacket());
    }
    else
    {
        SystemScreenSetValue(SYSTEM_ROW_FLIR_LAST_PACKET, "--");
    }

    // The three counters that mean the VoSPI stream is not healthy, summed
    // into one row: individually they are a driver author's breakdown (that
    // is what "FLIR Status?" is for), but any of them climbing is the same
    // message to someone looking at this screen.
    //
    // These are exactly the three the fault string below names. Do not add a
    // counter to this sum without adding it there too, or the row can say
    // there are faults while showing three zeros.
    framing_faults = stats.desyncCount + stats.segmentErrors + stats.rxOverflows;

    if (framing_faults == 0)
    {
        SystemScreenSetVerdict(SYSTEM_ROW_FLIR_FRAMING, "none", SYSTEM_SCREEN_OK_COLOR);
    }
    else
    {
        char text[SYSTEM_SCREEN_VALUE_MAX_CHARS];

        snprintf(text, sizeof(text), "%lu desync / %lu seg / %lu ovf",
                (unsigned long)stats.desyncCount,
                (unsigned long)stats.segmentErrors,
                (unsigned long)stats.rxOverflows);

        SystemScreenSetVerdict(SYSTEM_ROW_FLIR_FRAMING, text, SYSTEM_SCREEN_FAULT_COLOR);
    }

    // Throttled, blocking I2C -- kept last so the cheap rows are already
    // written whatever the bus does
    SystemScreenRefreshFlirTemperature(state);
}

// Power Good: the six discrete PGOOD inputs printPGOODStatus() prints, all
// of them plain GPIO reads. These sit alongside the measured rail voltages
// above deliberately -- a rail can read a plausible voltage on the INA231A
// while its regulator has dropped PGOOD, and the pair is what tells those
// apart.
static void SystemScreenRefreshPGOOD(void)
{
    SystemScreenSetOkBad(SYSTEM_ROW_PGOOD_12V,     POS12_PGOOD_PIN,     "good", "FAULT");
    SystemScreenSetOkBad(SYSTEM_ROW_PGOOD_3P3_USB, POS3P3_USB_PGOOD_PIN, "good", "FAULT");
    SystemScreenSetOkBad(SYSTEM_ROW_PGOOD_3P0,     POS3P0_PGOOD_PIN,    "good", "FAULT");
    SystemScreenSetOkBad(SYSTEM_ROW_PGOOD_2P8,     POS2P8_PGOOD_PIN,    "good", "FAULT");
    SystemScreenSetOkBad(SYSTEM_ROW_PGOOD_1P8,     POS1P8_PGOOD_PIN,    "good", "FAULT");
    SystemScreenSetOkBad(SYSTEM_ROW_PGOOD_1P2,     POS1P2_PGOOD_PIN,    "good", "FAULT");
}

// MAX8903G charger pins, the same block printBatteryControlPins() prints.
// Every one of these is ACTIVE LOW (the leading 'n' in the pin macro), so
// the sense is inverted here -- getting that backwards would report a
// faulted charger as healthy, which is the one direction that matters.
static void SystemScreenRefreshCharger(void)
{
    // Fault is the only genuinely bad one; the input/charging lines are
    // states, not faults, so they are shown in neutral gray rather than
    // colored as if "not charging" were a problem.
    SystemScreenSetOkBad(SYSTEM_ROW_CHG_FAULT, nBATT_FLT_PIN, "none", "FAULTED");

    SystemScreenSetVerdict(SYSTEM_ROW_CHG_DC_INPUT,
            nBATT_DOK_PIN ? "not present" : "stable",
            nBATT_DOK_PIN ? SYSTEM_SCREEN_UNKNOWN_COLOR : SYSTEM_SCREEN_OK_COLOR);

    SystemScreenSetVerdict(SYSTEM_ROW_CHG_USB_INPUT,
            nBATT_UOK_PIN ? "not present" : "stable",
            nBATT_UOK_PIN ? SYSTEM_SCREEN_UNKNOWN_COLOR : SYSTEM_SCREEN_OK_COLOR);

    SystemScreenSetVerdict(SYSTEM_ROW_CHG_CHARGING,
            nBATT_CHG_PIN ? "no" : "yes",
            nBATT_CHG_PIN ? SYSTEM_SCREEN_UNKNOWN_COLOR : SYSTEM_SCREEN_OK_COLOR);

    // CEN and IUSB are LAT reads, not PORT: they are outputs this firmware
    // drives, so this row reports what the MCU is asking the charger to do
    SystemScreenSetVerdict(SYSTEM_ROW_CHG_ENABLE,
            nBATT_CEN_PIN ? "disabled" : "enabled",
            nBATT_CEN_PIN ? SYSTEM_SCREEN_UNKNOWN_COLOR : SYSTEM_SCREEN_OK_COLOR);

    SystemScreenSetValue(SYSTEM_ROW_CHG_USB_CURRENT, BATT_IUSB_PIN ? "500 mA" : "100 mA");

    // NOT a MAX8903 signal despite sitting in the same pin block: this is
    // the BQ27441 fuel gauge's GPOUT, configured to mirror the gauge's SOC1
    // low-charge threshold with GPIOPOL=0, so it reads LOW when the battery
    // is low. It can legitimately disagree with the "Low" entry in the
    // battery Status row above, which is polled over I2C against the
    // different SOCF threshold -- see i2c/device_driver/bq27441.c.
    SystemScreenSetOkBad(SYSTEM_ROW_CHG_GAUGE_GPOUT, BATT_LOWBATT_PIN,
            "battery ok", "battery LOW");
}

static void SystemScreenBackClicked(lv_event_t *event)
{
    (void)event;

    GUI_ShowScreen(GUI_SCREEN_MENU, GUI_NAV_BACK);
}

lv_obj_t *SystemScreen_Create(void)
{
    lv_obj_t *screen = Screen_Create();
    lv_obj_t *panel;
    uint32_t index;
    int32_t y = 0;
    int32_t heap_row_y = 0;

    if (screen == NULL) return NULL;

    if (!Screen_CreateHeader(screen, "System Status", &header)) return NULL;

    // Reached from the main menu, so back returns there rather than to home
    if (!Screen_AddBackButton(&header, SystemScreenBackClicked, NULL)) return NULL;

    // --- Body panel -------------------------------------------------------
    panel = lv_obj_create(screen);
    if (panel == NULL) return NULL;

    // Plain pixel sizes, NOT LV_PCT(100) minus an inset: LV_PCT() returns an
    // encoded special value rather than a number of pixels, so subtracting
    // from it silently changes the percentage instead of insetting anything.
    lv_obj_set_size(panel,
            LV_HOR_RES - (2 * SYSTEM_SCREEN_PANEL_INSET_PX),
            LV_VER_RES - (2 * SCREEN_BAR_HEIGHT_PX) - (2 * SYSTEM_SCREEN_PANEL_INSET_PX));
    lv_obj_align(panel, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(panel, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(panel, SCREEN_BAR_OPACITY, LV_PART_MAIN);
    lv_obj_set_style_border_width(panel, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(panel, SYSTEM_SCREEN_PANEL_PAD_PX, LV_PART_MAIN);
    lv_obj_set_style_pad_right(panel, SYSTEM_SCREEN_SCROLLBAR_LANE_PX, LV_PART_MAIN);

    // Scrollable, unlike every other panel in this GUI: the row list is far
    // taller than the ~156px the two bars leave. LVGL derives the scrollable
    // extent from the children's own coordinates, so the explicit y-offsets
    // below are all it needs -- no layout engine involved.
    //
    // Vertical only: the value labels are right-aligned, and letting the
    // panel scroll horizontally as well would make a slightly-off vertical
    // drag skew the whole list sideways.
    lv_obj_set_scroll_dir(panel, LV_DIR_VER);

    // ON rather than AUTO: AUTO only shows the bar while a scroll is in
    // progress, which leaves no hint that there is anything below the fold.
    lv_obj_set_scrollbar_mode(panel, LV_SCROLLBAR_MODE_ON);

    // --- Rows -------------------------------------------------------------
    // Name on the left, value on the right, one row per line, with section
    // headings between groups. Explicit y-offsets rather than a layout
    // engine: the row set is fixed at build time and this keeps flex out of
    // the flash budget.
    for (index = 0; index < SYSTEM_SCREEN_ROW_TOTAL; index++)
    {
        const SYSTEM_SCREEN_ROW_DEF *row = &system_rows[index];

        if (row->is_section)
        {
            lv_obj_t *section;

            // Breathing room above each heading except the first, which sits
            // at the top of the panel already
            if (index != 0) y += SYSTEM_SCREEN_SECTION_GAP_PX;

            section = Screen_CreateLabel(panel, &lv_font_montserrat_14,
                    LV_ALIGN_TOP_LEFT, 0, y, row->label);

            if (section == NULL) return NULL;

            // Tinted to separate the groups without needing a second font
            // (Montserrat 20 is the only other one built in, and it would
            // cost another 20px of row height everywhere)
            lv_obj_set_style_text_color(section, lv_color_hex(0x60C0FF), LV_PART_MAIN);

            y += SYSTEM_SCREEN_SECTION_HEIGHT_PX;
        }
        else
        {
            if (Screen_CreateLabel(panel, &lv_font_montserrat_14,
                    LV_ALIGN_TOP_LEFT, 0, y, row->label) == NULL) return NULL;

            value_labels[row->value_row] = Screen_CreateLabel(panel,
                    &lv_font_montserrat_14, LV_ALIGN_TOP_RIGHT, 0, y, "--");

            if (value_labels[row->value_row] == NULL) return NULL;

            if (row->value_row == SYSTEM_ROW_HEAP) heap_row_y = y;

            y += SYSTEM_SCREEN_ROW_HEIGHT_PX;
        }
    }

    // The heap row gets a gauge as well as a percentage, sitting just left
    // of its value label. Positioned from the y the loop recorded rather
    // than from a row index, since sections make the two differ.
    heap_bar = lv_bar_create(panel);
    if (heap_bar == NULL) return NULL;

    lv_obj_set_size(heap_bar, SYSTEM_SCREEN_HEAP_BAR_WIDTH, SYSTEM_SCREEN_HEAP_BAR_HEIGHT);
    lv_obj_align(heap_bar, LV_ALIGN_TOP_RIGHT, -44, heap_row_y + 4);
    lv_bar_set_range(heap_bar, 0, 100);
    lv_bar_set_value(heap_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(heap_bar, lv_color_hex(0x404040), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(heap_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(heap_bar, lv_color_hex(0x3080E0), LV_PART_INDICATOR);

    // Static for the life of the run, so it never needs revisiting
    SystemScreenSetIdentity();

    SystemScreen_Refresh();

    return screen;
}

void SystemScreen_Refresh(void)
{
    uint32_t seconds;
    uint32_t errors;
    uint32_t index;

    // SystemScreen_Create() either finishes or leaves these NULL
    if ((value_labels[SYSTEM_ROW_ERRORS] == NULL) || (heap_bar == NULL)) return;

    Screen_RefreshHeader(&header);

    // --- System -----------------------------------------------------------
    SystemScreenSetValueFmt(SYSTEM_ROW_FIRMWARE, "%s / Rev %s",
            FIRMWARE_VERSION_STR, PLATFORM_REVISION_STR);

    // device_on_time_counter is bumped once a second by heartbeatServices()
    seconds = device_on_time_counter;
    SystemScreenSetValueFmt(SYSTEM_ROW_UPTIME, "%lud %02lu:%02lu:%02lu",
            (unsigned long)(seconds / 86400u),
            (unsigned long)((seconds / 3600u) % 24u),
            (unsigned long)((seconds / 60u) % 60u),
            (unsigned long)(seconds % 60u));

    errors = SystemScreen_CountLatchedErrors();

    // The Diagnostics screen (gui/screens/screen_errors.c) is where this
    // count is broken out into which flags they are, and where they can be
    // cleared
    if (errors == 0)
    {
        SystemScreenSetVerdict(SYSTEM_ROW_ERRORS, "none", SYSTEM_SCREEN_OK_COLOR);
    }
    else
    {
        char text[SYSTEM_SCREEN_VALUE_MAX_CHARS];

        snprintf(text, sizeof(text), "%lu latched", (unsigned long)errors);

        SystemScreenSetVerdict(SYSTEM_ROW_ERRORS, text, SYSTEM_SCREEN_FAULT_COLOR);
    }

    // Heap: the same DDR2 pool "Peripheral Status? GUI" reports, and the one
    // every PNG decode also draws on (application/image_loader.c) -- so this
    // row is where a decode leak would show up as a creeping percentage
    {
        lv_mem_monitor_t monitor;

        lv_mem_monitor(&monitor);

        lv_bar_set_value(heap_bar, (int32_t)monitor.used_pct, LV_ANIM_OFF);
        SystemScreenSetValueFmt(SYSTEM_ROW_HEAP, "%u%%", (unsigned int)monitor.used_pct);
    }

    // --- Elapsed time counter (throttled, blocking I2C) --------------------
    SystemScreenRefreshElapsedTime();

    // --- MCU / ambient ----------------------------------------------------
    SystemScreenSetValueFmt(SYSTEM_ROW_DIE_TEMP,     "%.1f C", telemetry.mcu_die_temp);
    SystemScreenSetValueFmt(SYSTEM_ROW_MCU_VBAT,     "%.3f V", telemetry.mcu_battery_voltage);
    SystemScreenSetValueFmt(SYSTEM_ROW_ADC_VREF,     "%.3f V", telemetry.adc_vref_voltage);
    SystemScreenSetValueFmt(SYSTEM_ROW_AMBIENT_TEMP, "%.1f C", telemetry.ambient_temperature);

    // --- FLIR Lepton (cached, plus one throttled blocking CCI read) --------
    SystemScreenRefreshFlir();

    // --- Rails ------------------------------------------------------------
    for (index = 0; index < SYSTEM_SCREEN_RAIL_COUNT; index++)
    {
        const SYSTEM_SCREEN_RAIL *rail = &system_rails[index];

        SystemScreenSetValueFmt(rail->voltage_row,     "%.3f V",  rail->source->voltage);
        SystemScreenSetValueFmt(rail->current_row,     "%.3f A",  rail->source->current);
        SystemScreenSetValueFmt(rail->power_row,       "%.3f W",  rail->source->power);
        SystemScreenSetValueFmt(rail->temperature_row, "%.1f C",  rail->source->temperature);
    }

    // --- Power Good (plain GPIO reads) ------------------------------------
    SystemScreenRefreshPGOOD();

    // --- Battery ----------------------------------------------------------
    // `present` is latched once at boot from a voltage heuristic (the fuel
    // gauge's own BAT_DET can't tell "installed" from "absent" on this
    // board -- see main.c), so it is a boot-time fact, not a live reading.
    SystemScreenSetValue(SYSTEM_ROW_BATT_PRESENT,
            telemetry.battery.present ? "yes" : "no");

    SystemScreenSetValueFmt(SYSTEM_ROW_BATT_V,       "%.3f V",   telemetry.battery.voltage);
    SystemScreenSetValueFmt(SYSTEM_ROW_BATT_I,       "%.3f A",   telemetry.battery.current);
    SystemScreenSetValueFmt(SYSTEM_ROW_BATT_T,       "%.1f C",   telemetry.battery.temperature);
    SystemScreenSetValueFmt(SYSTEM_ROW_BATT_SOC,     "%.0f %%",  telemetry.battery.state_of_charge);
    SystemScreenSetValueFmt(SYSTEM_ROW_BATT_SOH,     "%.0f %%",  telemetry.battery.state_of_health);
    SystemScreenSetValueFmt(SYSTEM_ROW_BATT_REMCAP,  "%.0f mAh", telemetry.battery.remaining_capacity);
    SystemScreenSetValueFmt(SYSTEM_ROW_BATT_FULLCAP, "%.0f mAh", telemetry.battery.full_charge_capacity);

    // The six Flags() bits as one comma-separated list rather than six
    // yes/no rows -- the interesting state is which of them are SET, and
    // five "no"s in a column buries the one that isn't.
    {
        char status[SYSTEM_SCREEN_VALUE_MAX_CHARS];

        status[0] = '\0';

        SystemScreenAppendFlag(status, sizeof(status), telemetry.battery.charging,          "Charging");
        SystemScreenAppendFlag(status, sizeof(status), telemetry.battery.discharging,       "Discharging");
        SystemScreenAppendFlag(status, sizeof(status), telemetry.battery.fully_charged,     "Full");
        SystemScreenAppendFlag(status, sizeof(status), telemetry.battery.over_temperature,  "Over Temp");
        SystemScreenAppendFlag(status, sizeof(status), telemetry.battery.under_temperature, "Under Temp");
        SystemScreenAppendFlag(status, sizeof(status), telemetry.battery.low_battery,       "Low");

        SystemScreenSetValue(SYSTEM_ROW_BATT_STATUS, (status[0] == '\0') ? "idle" : status);
    }

    // --- Charger (plain GPIO reads) ---------------------------------------
    SystemScreenRefreshCharger();
}
