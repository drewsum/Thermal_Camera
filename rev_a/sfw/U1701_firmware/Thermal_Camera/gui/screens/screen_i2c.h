/*******************************************************************************
  I2C Slave Status GUI Screen

  File Name:
    screen_i2c.h

  Summary:
    The panel-side equivalent of the "Platform Status? I2C Slaves" USB UART
    command: every device on I2C1, whether it is talking, and the identity,
    configuration and threshold registers behind it.

  Description:
    Reached from the main menu. One scrolling panel; each device is a heading
    line with a verdict, then its register block, in I2C_DEVICE_LIST order
    (i2c/i2c_devices.h):

      +--------------------------------------------------+
      | < | I2C Slave Status                  08-18-2026 |
      |                                         14:32:07 |
      | +----------------------------------------------+ |
      | | 16 of 16 responding - scan complete           | |
      | |                                              | |
      | | POS12 Input Gate Temp Sensor              OK | |
      | | U302  0x18  MCP9804                          | |
      | |   Mfr 0x0054  Dev 0x02  Rev 0x00             | |
      | |   Config 0x0000  Hyst 0 C (disabled)         | |
      | |   Resolution 0x0003  0.0625 C, 250 ms        | |
      | |   T_UP 100.0  T_LOW 0.0  T_CRIT 125.0 C      | |
      | |   Alerts none                                | |
      | +----------------------------------------------+ |
      +--------------------------------------------------+

    The verdict combines presence with the latched per-device error flags:

      OK        responded at boot, no flag ever latched against it
      ABSENT    did not respond when I2CDevices_Initialize() probed it
      I2C ERR   responded, but has since failed a read (or failed to verify)
      CFG ERR   reachable and identified, but its setup sequence failed --
                a very different fault from not being on the bus at all,
                which is why error_handler.h keeps them as separate flags
      I2C+CFG   both flags latched

    Those flags only ever LATCH, so a device can read OK on the bus right now
    and still show I2C ERR from a failure minutes ago. The Diagnostics
    screen's Clear Errors button is what resets them.

    WHERE THE REGISTER VALUES COME FROM. Each driver exposes a
    *_ReadDiagnostics() that does one pass over its identity/configuration/
    threshold registers and returns them as a struct, and each driver's
    *_PrintStatus() -- what the UART command calls -- is written on top of
    that same function. So the console and this screen cannot drift about
    what a part is reporting; they are reading through one code path.

    What this screen deliberately does NOT repeat is the live measurement
    each device contributes (a rail's volts and amps, the ambient
    temperature, the battery's state of charge). Those are cached by
    telemetryTasks() and already have rows on the system status screen; this
    screen is for the configuration underneath them.

    HOW THE SCAN IS PACED. Reading these registers is real bus traffic --
    roughly five to seven transactions per device, sixteen devices -- and
    doing it all in one refresh would block the main loop, behind the FLIR
    video path, for the whole scan. So the screen owns an LVGL timer that
    reads exactly ONE device per tick and writes that device's block as it
    lands, the same one-piece-of-work-per-tick shape screen_saved_images.c
    uses for its PNG decodes. The list therefore fills in visibly over about
    a second, and nothing is read at all while the screen is not being
    looked at.

    The scan restarts whenever the screen is arrived at, since these are
    exactly the values that change when something is repaired, reseated, or
    reconfigured. It does not repeat while the screen is simply left open --
    configuration registers do not move on their own, and re-reading them
    forever would be permanent bus traffic for a static answer. The Rescan
    button in the action bar is how you ask for a fresh pass.

    THE TOUCH CONTROLLER IS READ, NEVER ACKNOWLEDGED. GT911_ReadTouch()
    acknowledges a report by writing the coordinate status register back to
    zero, and gui/lv_port_indev.c must be the only caller doing that.
    GT911_ReadDiagnostics() only reads, so this screen can show the
    controller's registers without stealing touches from the input driver --
    which would otherwise make the panel unresponsive exactly while this
    screen is open.
*******************************************************************************/

#ifndef SCREEN_I2C_H
#define SCREEN_I2C_H

#include "gui/lvgl/lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// Builds the I2C slave status screen and starts its scan timer. Returns the
// screen object, or NULL on failure.
lv_obj_t *ScreenI2C_Create(void);

// Re-reads presence and the latched per-device flags -- both cached, so this
// costs nothing. The register blocks are filled in by the scan timer, not
// here. Safe to call if Create() failed.
void ScreenI2C_Refresh(void);

#ifdef __cplusplus
}
#endif

#endif /* SCREEN_I2C_H */
