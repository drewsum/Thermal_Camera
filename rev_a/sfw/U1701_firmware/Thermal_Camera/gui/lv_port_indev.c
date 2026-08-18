/*******************************************************************************
  LVGL Input Device Port (GT911 Capacitive Touch)

  File Name:
    lv_port_indev.c

  Summary:
    See lv_port_indev.h, especially the coordinate-mapping note.
*******************************************************************************/

#include <stdio.h>

#include "gui/lv_port_indev.h"

#include "gui/lvgl/lvgl.h"
#include "i2c/device_driver/gt911.h"
#include "i2c/i2c_devices.h"
#include "usb_uart/terminal_control.h"

static lv_indev_t *touch_indev = NULL;

// Last state reported to LVGL. Held across calls because the GT911 only
// posts a report when something CHANGES: between reports the finger is
// still wherever it was, and forcing a release in that gap would turn every
// press into a stream of taps.
static bool touch_pressed = false;
static lv_point_t touch_point = { 0, 0 };

// Diagnostics for lv_port_indev_PrintStatus(). raw_* is what the controller
// reported before mapping, so a bad axis mapping is visible by comparing the
// two against where the finger actually was.
static uint16_t touch_raw_x = 0;
static uint16_t touch_raw_y = 0;
static uint32_t touch_press_count = 0;
static uint32_t touch_error_count = 0;

// Applies the swap/invert switches from lv_port_indev.h and clamps into the
// display, so a controller reporting a slightly out-of-range edge coordinate
// can't hand LVGL a point outside its own screen.
static void PortIndevMapPoint(uint16_t raw_x, uint16_t raw_y, lv_point_t *point)
{
    int32_t hor_res = lv_display_get_horizontal_resolution(NULL);
    int32_t ver_res = lv_display_get_vertical_resolution(NULL);
    int32_t x;
    int32_t y;

    // Both return 0 when there is no default display. Can't happen in
    // practice (the display port is initialized first), but the clamp below
    // would turn a zero resolution into a -1 coordinate.
    if ((hor_res <= 0) || (ver_res <= 0)) return;

#if LV_PORT_INDEV_SWAP_XY
    x = (int32_t)raw_y;
    y = (int32_t)raw_x;
#else
    x = (int32_t)raw_x;
    y = (int32_t)raw_y;
#endif

#if LV_PORT_INDEV_INVERT_X
    x = (hor_res - 1) - x;
#endif

#if LV_PORT_INDEV_INVERT_Y
    y = (ver_res - 1) - y;
#endif

    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x > (hor_res - 1)) x = hor_res - 1;
    if (y > (ver_res - 1)) y = ver_res - 1;

    point->x = x;
    point->y = y;
}

// LVGL's read callback, called from its input timer. Deliberately does no
// work at all when the panel isn't populated: I2CDevices_IsPresent() is a
// plain array read, so the whole callback costs nothing on a board with no
// LCD module rather than generating a NACKing transaction 30 times a second.
static void PortIndevRead(lv_indev_t *indev, lv_indev_data_t *data)
{
    GT911_TOUCH touch;

    (void)indev;

    if (I2CDevices_IsPresent(I2C_DEV_CTP_1))
    {
        switch (GT911_ReadTouch(I2CDevices_GetAddress(I2C_DEV_CTP_1), &touch))
        {
            case GT911_TOUCH_UPDATED:
                if (touch.pressed)
                {
                    // Count edges, not reports: a finger held down produces
                    // a report per poll, and counting those would say
                    // "12000 presses" after a ten-second press.
                    if (!touch_pressed) touch_press_count++;

                    touch_raw_x = touch.x;
                    touch_raw_y = touch.y;
                    PortIndevMapPoint(touch.x, touch.y, &touch_point);
                }

                // On release the point is deliberately NOT moved -- LVGL
                // needs the release to land on the same widget the press
                // did, or the click event never fires.
                touch_pressed = touch.pressed;
                break;

            case GT911_TOUCH_ERROR:
                // Latch it against the CTP's own flag, the same one every
                // other read of this device reports into, and drop the press
                // rather than leaving a phantom finger down on a dead bus.
                I2CDevices_ReportI2CError(I2C_DEV_CTP_1);
                touch_error_count++;
                touch_pressed = false;
                break;

            case GT911_TOUCH_NO_NEW_DATA:
            default:
                // Nothing changed since the last poll: hold the last state.
                break;
        }
    }
    else
    {
        touch_pressed = false;
    }

    data->point = touch_point;
    data->state = touch_pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

bool lv_port_indev_init(void)
{
    if (touch_indev != NULL) return true;

    touch_indev = lv_indev_create();

    if (touch_indev == NULL) return false;

    lv_indev_set_type(touch_indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(touch_indev, PortIndevRead);

    // No cursor object is set: this is a touch screen, and drawing a mouse
    // pointer on an overlay the user is touching directly would be noise.

    return true;
}

void lv_port_indev_PrintStatus(void)
{
    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, BOLD_FONT);
    printf("    --- Touch Input (GT911 -> LVGL) ---\n\r");

    if (touch_indev == NULL)
    {
        terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("    Not initialized\n\r");
        terminalTextAttributesReset();
        return;
    }

    if (!I2CDevices_IsPresent(I2C_DEV_CTP_1))
    {
        terminalTextAttributes(YELLOW_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("    Touch controller not detected -- input disabled\n\r");
        terminalTextAttributesReset();
        return;
    }

    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("    State: %s\n\r", touch_pressed ? "PRESSED" : "released");
    printf("    Last Raw Point:    %u, %u  (controller coordinates)\n\r",
            (unsigned int)touch_raw_x, (unsigned int)touch_raw_y);
    printf("    Last Mapped Point: %ld, %ld  (panel pixels)\n\r",
            (long)touch_point.x, (long)touch_point.y);
    printf("    Mapping: swap_xy=%u invert_x=%u invert_y=%u (gui/lv_port_indev.h)\n\r",
            (unsigned int)LV_PORT_INDEV_SWAP_XY,
            (unsigned int)LV_PORT_INDEV_INVERT_X,
            (unsigned int)LV_PORT_INDEV_INVERT_Y);
    printf("    Presses: %lu, I2C errors: %lu\n\r",
            (unsigned long)touch_press_count, (unsigned long)touch_error_count);

    terminalTextAttributesReset();
}
