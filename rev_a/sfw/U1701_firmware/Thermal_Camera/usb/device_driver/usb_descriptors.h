/*******************************************************************************
  USB Descriptor Tables

  File Name:
    usb_descriptors.h

  Summary:
    Descriptor tables for the USB mass storage device -- kept out of
    usb.c/usb_msd.c so the protocol code stays readable and the
    device-identity knobs (VID/PID, strings) live in one obvious place.

  Description:
    The device is a single-configuration, single-interface MSC
    (class 0x08, subclass 0x06 SCSI-transparent, protocol 0x50
    Bulk-Only Transport) device with bulk IN endpoint 0x81 and bulk OUT
    endpoint 0x01. It runs Hi-Speed (512B bulk endpoints) when the host
    negotiates it and Full-Speed (64B) otherwise, so both a
    configuration and an other-speed-configuration descriptor exist;
    usb.c serves whichever pair matches the negotiated speed
    (USBCSR0.HSMODE).

    The serial-number string is generated at runtime from the MCU's
    factory device serial (DEVSN0/DEVSN1) -- 16 hex digits, satisfying
    the MSC spec's >=12-hex-digit unique-serial requirement -- which is
    also why hosts remember drive-letter assignments per physical unit.
*******************************************************************************/

#ifndef USB_DESCRIPTORS_H
#define USB_DESCRIPTORS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// !!! PLACEHOLDER IDs -- pid.codes test-allocation VID/PID, explicitly
// forbidden for released products. Replace with a real assignment before
// this device ships anywhere.
#define USB_VID     0x1209u
#define USB_PID     0x0001u

#define USB_BCD_DEVICE  0x0100u     // device release 1.00

// Endpoint addresses/sizes (bulk IN 0x81 / bulk OUT 0x01 on hardware
// endpoint 1; EP0 is always 64B)
#define USB_EP0_MAX_PACKET      64u
#define USB_BULK_EP_NUM         1u
#define USB_BULK_MAX_PACKET_HS  512u
#define USB_BULK_MAX_PACKET_FS  64u

// Descriptor type codes (USB 2.0 table 9-5)
#define USB_DESC_TYPE_DEVICE            1u
#define USB_DESC_TYPE_CONFIGURATION     2u
#define USB_DESC_TYPE_STRING            3u
#define USB_DESC_TYPE_INTERFACE         4u
#define USB_DESC_TYPE_ENDPOINT          5u
#define USB_DESC_TYPE_DEVICE_QUALIFIER  6u
#define USB_DESC_TYPE_OTHER_SPEED_CFG   7u

// Fixed-size descriptor blobs (usb_descriptors.c)
extern const uint8_t usb_device_descriptor[18];
extern const uint8_t usb_device_qualifier[10];
#define USB_CONFIG_DESC_TOTAL_LENGTH 32u
extern const uint8_t usb_config_descriptor_hs[USB_CONFIG_DESC_TOTAL_LENGTH];
extern const uint8_t usb_config_descriptor_fs[USB_CONFIG_DESC_TOTAL_LENGTH];

// Returns the string descriptor for `index` (0 = LANGID table,
// 1 = manufacturer, 2 = product, 3 = serial from DEVSN0/1), or NULL for
// an unknown index. *length receives the descriptor's bLength.
const uint8_t *USB_GetStringDescriptor(uint8_t index, uint16_t *length);

#ifdef __cplusplus
}
#endif

#endif /* USB_DESCRIPTORS_H */
