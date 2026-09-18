/*******************************************************************************
  I2C Slave Status GUI Screen

  File Name:
    screen_i2c.c

  Summary:
    See screen_i2c.h.
*******************************************************************************/

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "gui/screens/screen_i2c.h"
#include "gui/screens/screen_common.h"

#include "gui/gui.h"
#include "gui/lvgl/lvgl.h"

#include "application/error_handler.h"
#include "gpio/pin_macros.h"
#include "i2c/i2c_devices.h"
#include "i2c/device_driver/bq27441.h"
#include "i2c/device_driver/ds1683.h"
#include "i2c/device_driver/gt911.h"
#include "i2c/device_driver/ina231a.h"
#include "i2c/device_driver/mcp9804.h"

// Body panel geometry, matching system_screen.c and screen_sd_card.c so the
// three read as the same instrument.
#define I2C_SCREEN_PANEL_INSET_PX      6
#define I2C_SCREEN_PANEL_PAD_PX        6

// Right-hand lane kept clear for the scrollbar the default theme draws
// inside the panel's right edge -- see system_screen.c, same reasoning.
#define I2C_SCREEN_SCROLLBAR_LANE_PX   14

// The row pitch every other list screen here uses, and the gap between one
// device's block and the next.
#define I2C_SCREEN_LINE_HEIGHT_PX      18
#define I2C_SCREEN_DEVICE_GAP_PX       8

// Where the device list starts, below the summary line at the top
#define I2C_SCREEN_SUMMARY_Y_PX        0
#define I2C_SCREEN_LIST_Y_PX           26

// The action bar's vertical padding is trimmed the same way
// Screen_CreateHeader() trims the header's, so the button inside can be tall
// enough to aim at (36 - 2 = 34px) rather than the 20px the shared bar
// padding would leave.
#define I2C_SCREEN_ACTION_BAR_PAD_PX   1
#define I2C_SCREEN_RESCAN_BUTTON_W_PX  110
#define I2C_SCREEN_RESCAN_BUTTON_H_PX  (SCREEN_BAR_HEIGHT_PX - 2)

// The panel's content box, and the lane reserved at its right for the
// verdict word. Device names run to 30 characters ("POS12 Input Gate Power
// Monitor"), close enough to the space left over that the name label is
// given an explicit width and told to ellipsize -- a label with no width set
// sizes to its text and would silently overlap the verdict.
#define I2C_SCREEN_CONTENT_W_PX \
        (LV_HOR_RES - (2 * I2C_SCREEN_PANEL_INSET_PX) \
                - I2C_SCREEN_PANEL_PAD_PX - I2C_SCREEN_SCROLLBAR_LANE_PX)

#define I2C_SCREEN_VERDICT_LANE_PX     66
#define I2C_SCREEN_NAME_W_PX \
        (I2C_SCREEN_CONTENT_W_PX - I2C_SCREEN_VERDICT_LANE_PX)

// One line of a device's register block, plus the block as a whole. The
// widest real line is the INA231A's conversion-time pair; 56 leaves room.
#define I2C_SCREEN_DETAIL_LINE_CHARS   56

// The register block is indented under its device's heading, and bounded so
// a long line is CLIPPED rather than reflowed. That distinction is
// load-bearing: this screen's layout is computed from a fixed line count per
// device kind, and LV_LABEL_LONG_WRAP would turn one over-long line into two
// and push every device below it out of the position the build loop gave it.
// Clipping costs the tail of one line; wrapping would misalign the rest of
// the screen.
#define I2C_SCREEN_DETAIL_INDENT_PX    8
#define I2C_SCREEN_DETAIL_W_PX \
        (I2C_SCREEN_CONTENT_W_PX - I2C_SCREEN_DETAIL_INDENT_PX)

// Fixed per device kind, which is what makes the layout computable at build
// time: every kind always emits the same number of detail lines, using
// placeholder text where a register could not be read. Raising any of these
// means raising I2C_SCREEN_DETAIL_MAX_LINES with it.
#define I2C_SCREEN_LINES_MCP9804       5
#define I2C_SCREEN_LINES_INA231A       7
#define I2C_SCREEN_LINES_DS1683        5
#define I2C_SCREEN_LINES_GT911         5
#define I2C_SCREEN_LINES_BQ27441       5

#define I2C_SCREEN_DETAIL_MAX_LINES    7

#define I2C_SCREEN_DETAIL_BUF_CHARS \
        (I2C_SCREEN_DETAIL_MAX_LINES * I2C_SCREEN_DETAIL_LINE_CHARS)

// How long between one device's read and the next. Not a poll rate -- the
// scan runs once through the device list and stops (see screen_i2c.h) -- so
// this only sets how fast the list fills in. 60ms x 16 devices is about a
// second, with a single device's worth of bus traffic in any one main-loop
// pass.
#define I2C_SCREEN_SCAN_INTERVAL_MS    60

// Same palette as system_screen.c, plus amber for "reachable but something
// is latched against it" -- neither healthy nor the flat failure that a
// device missing from the bus is.
#define I2C_SCREEN_OK_COLOR            0x30C030
#define I2C_SCREEN_FAULT_COLOR         0xE04040
#define I2C_SCREEN_WARN_COLOR          0xE0A040
#define I2C_SCREEN_DIM_COLOR           0x808080
#define I2C_SCREEN_DETAIL_COLOR        0xC0C0C0

static SCREEN_HEADER header;

static lv_obj_t *summary_label = NULL;

// Per device: the verdict on the heading line, and the register block below
// it. The name and identity labels are written once at build time and never
// change, so they are not kept.
static lv_obj_t *verdict_labels[I2C_DEVICE_COUNT] = { NULL };
static lv_obj_t *detail_labels[I2C_DEVICE_COUNT] = { NULL };

// Scratch the detail text is built in before being compared against what the
// label already shows. One buffer, reused per device, since it is written
// straight into the label.
static char detail_scratch[I2C_SCREEN_DETAIL_BUF_CHARS];

// The scan: which device is next, and whether a pass is in progress. The
// timer runs for the life of the screen but does nothing unless the screen
// is active and a pass is pending.
static lv_timer_t *scan_timer = NULL;
static uint32_t scan_next_device = 0;
static bool scan_in_progress = false;

// Whether the screen was active on the previous tick -- the edge that starts
// a fresh pass, the same worker_was_active idiom screen_saved_images.c uses.
static bool scan_was_active = false;

// How many detail lines a kind always emits. Used by the build loop to place
// the following device, so it must agree with what the formatters below
// actually write.
static uint32_t ScreenI2CDetailLines(I2C_DEVICE_KIND kind)
{
    switch (kind)
    {
        case I2C_DEVICE_KIND_MCP9804: return I2C_SCREEN_LINES_MCP9804;
        case I2C_DEVICE_KIND_INA231A: return I2C_SCREEN_LINES_INA231A;
        case I2C_DEVICE_KIND_DS1683:  return I2C_SCREEN_LINES_DS1683;
        case I2C_DEVICE_KIND_GT911:   return I2C_SCREEN_LINES_GT911;
        case I2C_DEVICE_KIND_BQ27441: return I2C_SCREEN_LINES_BQ27441;
        default:                      return 1;
    }
}

// Writes a label only when the text actually changed. lv_label_set_text()
// invalidates unconditionally, and the detail labels are multi-line, so a
// blind rewrite would reflow and re-render the whole panel into DDR2 in
// competition with the FLIR video path. Same guard, same reasoning, as
// system_screen.c's SystemScreenSetValue().
static void ScreenI2CSetLabel(lv_obj_t *label, const char *text)
{
    if (label == NULL) return;
    if (strcmp(lv_label_get_text(label), text) == 0) return;

    lv_label_set_text(label, text);
}

// Appends one line to detail_scratch. Every formatter below goes through
// this, so the newline handling and the overflow clamp live in one place.
//
// snprintf() returns what it WOULD have written, so advancing by it
// unchecked walks the cursor past the end of the buffer. The buffer is sized
// for the longest block, so this cannot truncate today -- the clamp is here
// so that a longer line added later degrades into cut-off text rather than a
// buffer overrun.
static void ScreenI2CAppendLine(size_t *used, const char *format, ...)
{
    va_list args;
    int written;

    if (*used >= (I2C_SCREEN_DETAIL_BUF_CHARS - 1u)) return;

    if (*used > 0)
    {
        detail_scratch[*used] = '\n';
        (*used)++;
        detail_scratch[*used] = '\0';
    }

    va_start(args, format);
    written = vsnprintf(detail_scratch + *used,
            I2C_SCREEN_DETAIL_BUF_CHARS - *used, format, args);
    va_end(args);

    if (written <= 0) return;

    *used += (size_t)written;

    if (*used >= I2C_SCREEN_DETAIL_BUF_CHARS)
    {
        *used = I2C_SCREEN_DETAIL_BUF_CHARS - 1u;
        detail_scratch[*used] = '\0';
    }
}

// --- Per-kind formatters --------------------------------------------------
// Each writes exactly ScreenI2CDetailLines(kind) lines into detail_scratch,
// substituting a placeholder for any register that could not be read, so the
// block's height never depends on what answered.

// Pads a block out to its kind's fixed line count after an early exit, so a
// device that stopped answering does not shrink and pull every device below
// it up out of the position the build loop placed it at.
static void ScreenI2CPadLines(size_t *used, uint32_t written, uint32_t total)
{
    uint32_t i;

    for (i = written; i < total; i++) ScreenI2CAppendLine(used, " ");
}

static void ScreenI2CFormatMCP9804(uint16_t address, size_t *used)
{
    MCP9804_DIAGNOSTICS diag;

    if (!MCP9804_ReadDiagnostics(address, &diag))
    {
        ScreenI2CAppendLine(used, "no response (I2C error %d)", (int)I2C_ErrorGet());
        ScreenI2CPadLines(used, 1u, I2C_SCREEN_LINES_MCP9804);
        return;
    }

    ScreenI2CAppendLine(used, "Mfr 0x%04X  Dev 0x%02X  Rev 0x%02X  %s",
            diag.manufacturerId, (unsigned int)((diag.deviceId >> 8) & 0xFFu),
            (unsigned int)(diag.deviceId & 0xFFu),
            diag.identified ? "ok" : "BAD");

    if (diag.configValid)
    {
        // SHDN is called out only when it is set: a shut-down sensor still
        // answers every register read while reporting a stale temperature,
        // which is the one config bit that explains a "working" part giving
        // a wrong number.
        ScreenI2CAppendLine(used, "Config 0x%04X %s Hyst %s", diag.config,
                (diag.config & 0x0100u) ? "SHDN" : "",
                MCP9804_HysteresisName(diag.config));
    }
    else ScreenI2CAppendLine(used, "Config: read failed");

    if (diag.resolutionValid)
    {
        // The name alone, without the register value: the decoded string is
        // long enough on its own that adding the raw word pushes this line
        // past the panel (see the width note on the detail label)
        ScreenI2CAppendLine(used, "Res %s", MCP9804_ResolutionName(diag.resolution));
    }
    else ScreenI2CAppendLine(used, "Resolution: read failed");

    if (diag.upperValid && diag.lowerValid && diag.criticalValid)
    {
        ScreenI2CAppendLine(used, "T_UP %.1f  T_LOW %.1f  T_CRIT %.1f C",
                diag.upperLimit, diag.lowerLimit, diag.criticalLimit);
    }
    else ScreenI2CAppendLine(used, "Limits: read failed");

    if (diag.temperatureValid)
    {
        bool any = diag.alerts.aboveCritical || diag.alerts.aboveUpper ||
                   diag.alerts.belowLower;

        ScreenI2CAppendLine(used, "T_A %.3f C  Alerts %s%s%s%s", diag.celsius,
                diag.alerts.aboveCritical ? "CRIT " : "",
                diag.alerts.aboveUpper ? "UPPER " : "",
                diag.alerts.belowLower ? "LOWER " : "",
                any ? "" : "none");
    }
    else ScreenI2CAppendLine(used, "Temperature: read failed");
}

static void ScreenI2CFormatINA231A(uint16_t address, size_t *used)
{
    INA231A_DIAGNOSTICS diag;

    if (!INA231A_ReadDiagnostics(address, &diag))
    {
        ScreenI2CAppendLine(used, "no response (I2C error %d)", (int)I2C_ErrorGet());
        ScreenI2CPadLines(used, 1u, I2C_SCREEN_LINES_INA231A);
        return;
    }

    ScreenI2CAppendLine(used, "Config 0x%04X  %s", diag.config,
            diag.porDefault ? "POR default" : "reconfigured");

    ScreenI2CAppendLine(used, "Mode %s  Avg %s",
            INA231A_ModeName(diag.config), INA231A_AveragingName(diag.config));

    // The two conversion-time fields, shifted to the 3-bit values the name
    // helper indexes by (VBUSCT is bits 8:6, VSHCT bits 5:3)
    ScreenI2CAppendLine(used, "Conv bus %s  shunt %s",
            INA231A_ConversionTimeName((diag.config >> 6) & 0x7u),
            INA231A_ConversionTimeName((diag.config >> 3) & 0x7u));

    if (diag.maskEnableValid)
    {
        ScreenI2CAppendLine(used, "Mask/En 0x%04X%s", diag.maskEnable,
                diag.mathOverflow ? "  MATH OVERFLOW" : "");
    }
    else ScreenI2CAppendLine(used, "Mask/Enable: read failed");

    if (diag.calibrationValid)
    {
        // A zero calibration register is why a rail can read 0 A and 0 W
        // while its bus voltage is perfectly healthy
        ScreenI2CAppendLine(used, "Calib 0x%04X%s", diag.calibration,
                (diag.calibration == 0) ? "  UNCALIBRATED" : "");
    }
    else ScreenI2CAppendLine(used, "Calibration: read failed");

    if (diag.busVoltageValid && diag.shuntVoltageValid)
    {
        ScreenI2CAppendLine(used, "Vbus %.4f V  Vshunt %.6f V",
                diag.busVoltage, diag.shuntVoltage);
    }
    else ScreenI2CAppendLine(used, "Voltages: read failed");

    if (diag.rawCurrentValid && diag.rawPowerValid)
    {
        // Raw codes, not amps and watts: converting needs the current LSB
        // the driver does not retain, and the point here is what the part
        // actually holds
        ScreenI2CAppendLine(used, "Raw I 0x%04X (%d)  P 0x%04X (%u)",
                (unsigned int)(uint16_t)diag.rawCurrent, (int)diag.rawCurrent,
                diag.rawPower, diag.rawPower);
    }
    else ScreenI2CAppendLine(used, "Raw registers: read failed");
}

static void ScreenI2CFormatDS1683(uint16_t address, size_t *used)
{
    DS1683_DIAGNOSTICS diag;

    if (!DS1683_ReadDiagnostics(address, &diag))
    {
        ScreenI2CAppendLine(used, "no response (I2C error %d)", (int)I2C_ErrorGet());
        ScreenI2CPadLines(used, 1u, I2C_SCREEN_LINES_DS1683);
        return;
    }

    ScreenI2CAppendLine(used, "Command 0x%02X  %s", diag.command,
            diag.identified ? "as expected" : "UNEXPECTED");

    if (diag.configValid)
    {
        ScreenI2CAppendLine(used, "Config 0x%02X  ETC alrm %s  EVT alrm %s",
                diag.config,
                (diag.config & 0x01u) ? "on" : "off",
                (diag.config & 0x02u) ? "on" : "off");
    }
    else ScreenI2CAppendLine(used, "Config: read failed");

    if (diag.statusValid)
    {
        ScreenI2CAppendLine(used, "Status 0x%02X  ETC %s  EVT %s  pin %s",
                diag.rawStatus,
                diag.status.etcAlarm ? "ALARM" : "clear",
                diag.status.eventAlarm ? "ALARM" : "clear",
                diag.status.eventPinHigh ? "hi" : "lo");
    }
    else ScreenI2CAppendLine(used, "Status: read failed");

    if (diag.elapsedValid)
    {
        ScreenI2CAppendLine(used, "Elapsed %lud %02lu:%02lu:%02lu (%lu s)",
                (unsigned long)(diag.elapsedSeconds / 86400u),
                (unsigned long)((diag.elapsedSeconds / 3600u) % 24u),
                (unsigned long)((diag.elapsedSeconds / 60u) % 60u),
                (unsigned long)(diag.elapsedSeconds % 60u),
                (unsigned long)diag.elapsedSeconds);
    }
    else ScreenI2CAppendLine(used, "Elapsed time: read failed");

    if (diag.eventCountValid)
    {
        ScreenI2CAppendLine(used, "Event count %u", diag.eventCount);
    }
    else ScreenI2CAppendLine(used, "Event count: read failed");
}

static void ScreenI2CFormatGT911(uint16_t address, size_t *used)
{
    GT911_DIAGNOSTICS diag;

    if (!GT911_ReadDiagnostics(address, &diag))
    {
        ScreenI2CAppendLine(used, "no response (I2C error %d)", (int)I2C_ErrorGet());

        // The pin levels are GPIO, so they are worth showing even when the
        // part is not answering -- a controller held in reset explains the
        // silence
        ScreenI2CAppendLine(used, "RST %s  INT %s",
                LCD_CTP_RESET_PIN ? "hi (released)" : "lo (IN RESET)",
                LCD_CTP_INT_PIN ? "hi" : "lo");

        ScreenI2CPadLines(used, 2u, I2C_SCREEN_LINES_GT911);
        return;
    }

    ScreenI2CAppendLine(used, "Product ID %c%c%c  %s",
            (diag.productId[0] >= 0x20u && diag.productId[0] < 0x7Fu) ? diag.productId[0] : '?',
            (diag.productId[1] >= 0x20u && diag.productId[1] < 0x7Fu) ? diag.productId[1] : '?',
            (diag.productId[2] >= 0x20u && diag.productId[2] < 0x7Fu) ? diag.productId[2] : '?',
            diag.identified ? "as expected" : "UNEXPECTED");

    ScreenI2CAppendLine(used, "Raw ID 0x%02X 0x%02X 0x%02X 0x%02X",
            diag.productId[0], diag.productId[1], diag.productId[2], diag.productId[3]);

    if (diag.configVersionValid && diag.firmwareVersionValid)
    {
        ScreenI2CAppendLine(used, "Config ver 0x%02X  Firmware 0x%02X%02X",
                diag.configVersion, diag.firmwareVersion[0], diag.firmwareVersion[1]);
    }
    else ScreenI2CAppendLine(used, "Versions: read failed");

    if (diag.coordStatusValid)
    {
        // Read, never acknowledged -- see the note in screen_i2c.h about who
        // owns clearing this register
        ScreenI2CAppendLine(used, "Coord 0x%02X  %u point%s%s", diag.coordStatus,
                (unsigned int)(diag.coordStatus & 0x0Fu),
                ((diag.coordStatus & 0x0Fu) == 1u) ? "" : "s",
                (diag.coordStatus & 0x80u) ? "  BUFFER_READY" : "");
    }
    else ScreenI2CAppendLine(used, "Coordinate status: read failed");

    ScreenI2CAppendLine(used, "RST %s  INT %s",
            LCD_CTP_RESET_PIN ? "hi (released)" : "lo (IN RESET)",
            LCD_CTP_INT_PIN ? "hi" : "lo");
}

static void ScreenI2CFormatBQ27441(uint16_t address, size_t *used)
{
    BQ27441_DIAGNOSTICS diag;

    if (!BQ27441_ReadDiagnostics(address, &diag))
    {
        ScreenI2CAppendLine(used, "no response (I2C error %d)", (int)I2C_ErrorGet());
        ScreenI2CPadLines(used, 1u, I2C_SCREEN_LINES_BQ27441);
        return;
    }

    ScreenI2CAppendLine(used, "Device Type 0x%04X  %s", diag.deviceType,
            diag.identified ? "recognized" : "UNRECOGNIZED");

    if (diag.controlStatusValid)
    {
        // SEALED is the one that matters: a sealed gauge silently ignores
        // SET_CFGUPDATE, so the OpConfig line below would show whatever it
        // shipped with rather than what this firmware asked for
        ScreenI2CAppendLine(used, "Control 0x%04X  %s", diag.controlStatus,
                diag.sealed ? "SEALED" : "unsealed");
    }
    else ScreenI2CAppendLine(used, "Control status: read failed");

    if (diag.flagsValid)
    {
        ScreenI2CAppendLine(used, "Flags 0x%04X  %s%s%s%s", diag.rawFlags,
                diag.flags.fullyCharged ? "FC " : "",
                diag.flags.dischargeDetected ? "DSG " : "",
                diag.flags.lowStateOfCharge ? "SOCF " : "",
                diag.flags.configUpdateMode ? "CFGUPMODE" : "");
    }
    else ScreenI2CAppendLine(used, "Flags: read failed");

    if (diag.opConfigValid)
    {
        ScreenI2CAppendLine(used, "OpConfig 0x%04X  TEMPS %u BATLOW %u POL %u",
                diag.opConfig,
                (diag.opConfig & 0x0080u) ? 1u : 0u,
                (diag.opConfig & 0x0004u) ? 1u : 0u,
                (diag.opConfig & 0x0800u) ? 1u : 0u);
    }
    else ScreenI2CAppendLine(used, "OpConfig: read failed");

    if (diag.designCapacityValid)
    {
        ScreenI2CAppendLine(used, "Design capacity %u mAh", diag.designCapacity);
    }
    else ScreenI2CAppendLine(used, "Design capacity: read failed");
}

// Reads and writes one device's register block. This is the only place in
// the screen that touches the bus.
static void ScreenI2CScanDevice(I2C_DEVICE_ID id)
{
    uint16_t address = I2CDevices_GetAddress(id);
    size_t used = 0;

    detail_scratch[0] = '\0';

    // A device that never answered the boot probe is not worth a scan pass:
    // every read would fail and, worse, would latch a fresh I2C error flag
    // against a part that is already known to be missing.
    if (!I2CDevices_IsPresent(id))
    {
        ScreenI2CAppendLine(&used, "not detected at boot -- not scanned");
    }
    else
    {
        switch (I2CDevices_GetKind(id))
        {
            case I2C_DEVICE_KIND_MCP9804: ScreenI2CFormatMCP9804(address, &used); break;
            case I2C_DEVICE_KIND_INA231A: ScreenI2CFormatINA231A(address, &used); break;
            case I2C_DEVICE_KIND_DS1683:  ScreenI2CFormatDS1683(address, &used);  break;
            case I2C_DEVICE_KIND_GT911:   ScreenI2CFormatGT911(address, &used);   break;
            case I2C_DEVICE_KIND_BQ27441: ScreenI2CFormatBQ27441(address, &used); break;
            default: ScreenI2CAppendLine(&used, "no diagnostics for this kind"); break;
        }
    }

    ScreenI2CSetLabel(detail_labels[id], detail_scratch);
}

// One device per tick, and only while the screen is being looked at -- see
// screen_i2c.h.
static void ScreenI2CScanTimer(lv_timer_t *timer)
{
    (void)timer;

    if (!GUI_IsScreenActive(GUI_SCREEN_I2C))
    {
        scan_was_active = false;
        return;
    }

    // Arriving on the screen: whatever is on it is from the last visit, and
    // these are exactly the values that change when something is repaired or
    // reseated in between.
    if (!scan_was_active)
    {
        scan_was_active = true;
        scan_next_device = 0;
        scan_in_progress = true;
        return;
    }

    if (!scan_in_progress) return;

    ScreenI2CScanDevice((I2C_DEVICE_ID)scan_next_device);

    scan_next_device++;

    if (scan_next_device >= I2C_DEVICE_COUNT)
    {
        // One pass and stop. Configuration registers do not move on their
        // own, so repeating forever would be permanent bus traffic for a
        // static answer -- the Rescan button is how a fresh pass is asked
        // for.
        scan_in_progress = false;
        scan_next_device = 0;
    }

    // The summary line carries the scan's progress, so it has to keep up
    // with the pass rather than wait for the 500ms refresh
    ScreenI2C_Refresh();
}

// The verdict for one device: presence now, plus whatever has ever been
// latched against it. Both matter and they are independent -- see the table
// in screen_i2c.h.
static void ScreenI2CRefreshVerdict(I2C_DEVICE_ID id)
{
    lv_obj_t *label = verdict_labels[id];
    bool i2c_error = (ERROR_HANDLER_I2C_DEVICE_FLAG(id) != 0);
    bool config_error = (ERROR_HANDLER_I2C_CONFIG_FLAG(id) != 0);
    const char *text;
    uint32_t color;

    if (label == NULL) return;

    if (!I2CDevices_IsPresent(id))
    {
        // Did not respond to the probe. Its I2C error flag is set too
        // (I2CDevices_InitializeOne() reports one on a verify failure), so
        // this case is checked first -- "ABSENT" is the more useful of the
        // two things that are true.
        text = "ABSENT";
        color = I2C_SCREEN_FAULT_COLOR;
    }
    else if (i2c_error && config_error)
    {
        text = "I2C+CFG";
        color = I2C_SCREEN_WARN_COLOR;
    }
    else if (config_error)
    {
        text = "CFG ERR";
        color = I2C_SCREEN_WARN_COLOR;
    }
    else if (i2c_error)
    {
        text = "I2C ERR";
        color = I2C_SCREEN_WARN_COLOR;
    }
    else
    {
        text = "OK";
        color = I2C_SCREEN_OK_COLOR;
    }

    ScreenI2CSetLabel(label, text);
    lv_obj_set_style_text_color(label, lv_color_hex(color), LV_PART_MAIN);
}

static void ScreenI2CBackClicked(lv_event_t *event)
{
    (void)event;

    GUI_ShowScreen(GUI_SCREEN_MENU, GUI_NAV_BACK);
}

static void ScreenI2CRescanClicked(lv_event_t *event)
{
    (void)event;

    // Nothing is read here -- the tap only arms the timer, so the bus work
    // still happens one device per tick rather than all inside this callback
    scan_next_device = 0;
    scan_in_progress = true;
}

// Builds one device's block: the heading line (name + verdict), the identity
// line, and the multi-line register block. `y` is the top of the block
// inside the panel's content box. Returns false if any label could not be
// created.
static bool ScreenI2CCreateDevice(lv_obj_t *panel, I2C_DEVICE_ID id, int32_t y)
{
    char identity[48];
    lv_obj_t *name_label;
    lv_obj_t *identity_label;

    // --- Heading: descriptive name, and the verdict on the right ----------
    name_label = Screen_CreateLabel(panel, &lv_font_montserrat_14,
            LV_ALIGN_TOP_LEFT, 0, y, I2CDevices_GetName(id));
    if (name_label == NULL) return false;

    // Bounded and ellipsized rather than left to size itself, so a name
    // longer than the lane gets "..." instead of running under the verdict.
    // DOT and not WRAP: wrapping would make this device a line taller than
    // the pitch the following device is placed on.
    lv_obj_set_width(name_label, I2C_SCREEN_NAME_W_PX);
    lv_label_set_long_mode(name_label, LV_LABEL_LONG_DOT);

    verdict_labels[id] = Screen_CreateLabel(panel, &lv_font_montserrat_14,
            LV_ALIGN_TOP_RIGHT, 0, y, "--");
    if (verdict_labels[id] == NULL) return false;

    // --- Identity: refdes, bus address, part number -----------------------
    // Written once: none of it can change while the firmware is running.
    snprintf(identity, sizeof(identity), "%s  0x%02X  %s",
            I2CDevices_GetRefdes(id),
            (unsigned int)I2CDevices_GetAddress(id),
            I2CDevices_GetKindName(id));

    identity_label = Screen_CreateLabel(panel, &lv_font_montserrat_14,
            LV_ALIGN_TOP_LEFT, 0, y + I2C_SCREEN_LINE_HEIGHT_PX, identity);
    if (identity_label == NULL) return false;

    lv_obj_set_style_text_color(identity_label, lv_color_hex(I2C_SCREEN_DIM_COLOR),
            LV_PART_MAIN);

    // --- Register block ---------------------------------------------------
    // One multi-line label rather than a label per line: the line count is
    // fixed per kind, so the layout is still computable, and this is a third
    // as many objects on the splash fast path (GUI_Initialize() runs there).
    detail_labels[id] = Screen_CreateLabel(panel, &lv_font_montserrat_14,
            LV_ALIGN_TOP_LEFT, I2C_SCREEN_DETAIL_INDENT_PX,
            y + (2 * I2C_SCREEN_LINE_HEIGHT_PX), "scanning...");
    if (detail_labels[id] == NULL) return false;

    lv_obj_set_style_text_color(detail_labels[id], lv_color_hex(I2C_SCREEN_DETAIL_COLOR),
            LV_PART_MAIN);

    // Bounded and clipped, never wrapped -- see the note on
    // I2C_SCREEN_DETAIL_W_PX. The height is left to the content so the label
    // still grows to its kind's line count.
    lv_obj_set_width(detail_labels[id], I2C_SCREEN_DETAIL_W_PX);
    lv_label_set_long_mode(detail_labels[id], LV_LABEL_LONG_CLIP);

    return true;
}

lv_obj_t *ScreenI2C_Create(void)
{
    lv_obj_t *screen = Screen_Create();
    lv_obj_t *panel;
    lv_obj_t *action_bar;
    uint32_t id;
    int32_t y = I2C_SCREEN_LIST_Y_PX;

    if (screen == NULL) return NULL;

    if (!Screen_CreateHeader(screen, "I2C Slave Status", &header)) return NULL;

    if (!Screen_AddBackButton(&header, ScreenI2CBackClicked, NULL)) return NULL;

    // --- Body panel -------------------------------------------------------
    panel = lv_obj_create(screen);
    if (panel == NULL) return NULL;

    // Plain pixel arithmetic, NOT LV_PCT() minus an inset -- LV_PCT()
    // returns an encoded special value, so subtracting from it silently
    // changes the percentage instead of insetting anything.
    lv_obj_set_size(panel,
            LV_HOR_RES - (2 * I2C_SCREEN_PANEL_INSET_PX),
            LV_VER_RES - (2 * SCREEN_BAR_HEIGHT_PX) - (2 * I2C_SCREEN_PANEL_INSET_PX));

    // Centered, which leaves the bottom 42px clear for the action bar below
    // (the panel is sized against BOTH bars but only the header is above it)
    lv_obj_align(panel, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(panel, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(panel, SCREEN_BAR_OPACITY, LV_PART_MAIN);
    lv_obj_set_style_border_width(panel, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(panel, I2C_SCREEN_PANEL_PAD_PX, LV_PART_MAIN);
    lv_obj_set_style_pad_right(panel, I2C_SCREEN_SCROLLBAR_LANE_PX, LV_PART_MAIN);

    // Sixteen register blocks is far taller than the panel. Vertical only:
    // a horizontal scroll would let a slightly-off drag skew the list
    // sideways and take the right-aligned verdicts off screen.
    lv_obj_set_scroll_dir(panel, LV_DIR_VER);

    // ON rather than AUTO: AUTO only shows the bar during a scroll, which
    // leaves no hint that there is more below the fold -- and on this screen
    // there is a great deal more.
    lv_obj_set_scrollbar_mode(panel, LV_SCROLLBAR_MODE_ON);

    summary_label = Screen_CreateLabel(panel, &lv_font_montserrat_14,
            LV_ALIGN_TOP_LEFT, 0, I2C_SCREEN_SUMMARY_Y_PX, "--");
    if (summary_label == NULL) return NULL;

    // --- Devices ----------------------------------------------------------
    // In I2C_DEVICE_LIST order, which groups them by function already
    // (temperature sensors, then power monitors, then the one-offs) -- the
    // same order the UART command prints them in. Each block's height is
    // two heading lines plus its kind's fixed detail line count, so the next
    // device's position accumulates rather than being a fixed pitch.
    for (id = 0; id < I2C_DEVICE_COUNT; id++)
    {
        uint32_t lines;

        if (!ScreenI2CCreateDevice(panel, (I2C_DEVICE_ID)id, y)) return NULL;

        lines = 2u + ScreenI2CDetailLines(I2CDevices_GetKind((I2C_DEVICE_ID)id));

        y += (int32_t)(lines * I2C_SCREEN_LINE_HEIGHT_PX) + I2C_SCREEN_DEVICE_GAP_PX;
    }

    // --- Action bar -------------------------------------------------------
    // Outside the panel, so the one control on this screen cannot scroll out
    // of reach behind sixteen register blocks.
    action_bar = Screen_CreateBar(screen, LV_ALIGN_BOTTOM_MID);
    if (action_bar == NULL) return NULL;

    lv_obj_set_style_pad_top(action_bar, I2C_SCREEN_ACTION_BAR_PAD_PX, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(action_bar, I2C_SCREEN_ACTION_BAR_PAD_PX, LV_PART_MAIN);

    if (Screen_CreateButton(action_bar, LV_ALIGN_RIGHT_MID, 0, 0,
            I2C_SCREEN_RESCAN_BUTTON_W_PX, I2C_SCREEN_RESCAN_BUTTON_H_PX,
            "Rescan", ScreenI2CRescanClicked, NULL) == NULL) return NULL;

    // --- Scan timer -------------------------------------------------------
    // Runs for the life of the screen but does nothing unless the screen is
    // active and a pass is pending, so it costs nothing when this screen is
    // not the one being looked at.
    scan_timer = lv_timer_create(ScreenI2CScanTimer, I2C_SCREEN_SCAN_INTERVAL_MS, NULL);
    if (scan_timer == NULL) return NULL;

    ScreenI2C_Refresh();

    return screen;
}

void ScreenI2C_Refresh(void)
{
    char text[48];
    uint32_t responding = 0;
    uint32_t id;

    // ScreenI2C_Create() either finishes or leaves this NULL
    if (summary_label == NULL) return;

    Screen_RefreshHeader(&header);

    for (id = 0; id < I2C_DEVICE_COUNT; id++)
    {
        if (I2CDevices_IsPresent((I2C_DEVICE_ID)id)) responding++;

        ScreenI2CRefreshVerdict((I2C_DEVICE_ID)id);
    }

    // The denominator says how much of the bus the firmware knows about, and
    // the scan state says whether the register blocks below are complete yet
    snprintf(text, sizeof(text), "%lu of %lu responding - %s",
            (unsigned long)responding, (unsigned long)I2C_DEVICE_COUNT,
            scan_in_progress ? "scanning" : "scan complete");

    ScreenI2CSetLabel(summary_label, text);

    lv_obj_set_style_text_color(summary_label,
            lv_color_hex((responding == I2C_DEVICE_COUNT) ? I2C_SCREEN_OK_COLOR
                                                          : I2C_SCREEN_FAULT_COLOR),
            LV_PART_MAIN);
}
