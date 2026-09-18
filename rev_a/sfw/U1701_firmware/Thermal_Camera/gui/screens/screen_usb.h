/*******************************************************************************
  USB GUI Screen

  File Name:
    screen_usb.h

  Summary:
    The panel-side equivalent of the "USB Status?", "USB Detach" and
    "USB Attach" USB UART commands: what the mass-storage device is doing on
    the bus, and the one control that takes it off and puts it back.

  Description:
    Reached from the main menu. A scrolling panel of label/value rows like
    the SD Card Info screen, plus a bottom action bar holding the
    Attach/Detach button -- outside the panel, so the control cannot scroll
    out of reach:

      +--------------------------------------------------+
      | < | USB                               08-18-2026 |
      |                                         14:32:07 |
      | +----------------------------------------------+ |
      | | Bus                                          | |
      | | State               CONFIGURED               | |
      | | Speed               High Speed (480 Mbps)    | |
      | | Bulk Max Packet     512 bytes                | |
      | | Mass Storage                                 | |
      | | Media Owner         USB host                 | |
      | | ...                                          | |
      | +----------------------------------------------+ |
      |                                       [ Detach ] |
      +--------------------------------------------------+

    THE BUTTON IS THE POINT. Detaching is a soft disconnect: the host sees an
    unplug and the SD/flash volumes are remounted for the camera's own use,
    which is what makes the card writable locally again after a PC has had
    it. Attaching re-presents the device. Both were serial-only until now,
    and both are exactly what a user standing in front of the instrument with
    a cable plugged in wants to do. The button's text follows the state --
    "Attach" while detached, "Detach" otherwise -- so it always names what
    the tap will do rather than what the state is.

    WHAT IT IS SAFE TO READ. Everything on this screen comes from state the
    driver already maintains (usb_device_state, usb_counters,
    usb_msd_media_owned_by_host) or from an accessor documented as safe from
    task context. That distinction matters here more than on other screens:
    USBCSR0's interrupt-flag field is CLEAR-ON-READ, so touching that
    register directly from a 500ms refresh would quietly eat bus events the
    ISR needs. USB_IsHighSpeed() is used rather than a register read because
    it goes through usb.c's usbReadCsr0(), which latches anything it
    swallows into usb_pending_csr0 exactly as the ISR would. Nothing here
    reads a USB register any other way, and nothing new should.
*******************************************************************************/

#ifndef SCREEN_USB_H
#define SCREEN_USB_H

#include "gui/lvgl/lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// Builds the USB screen. Returns the screen object, or NULL on failure.
lv_obj_t *ScreenUSB_Create(void);

// Re-reads the bus state, the mass-storage LUN state and the event counters,
// and re-labels the action button to match. Safe to call if Create() failed.
void ScreenUSB_Refresh(void);

#ifdef __cplusplus
}
#endif

#endif /* SCREEN_USB_H */
