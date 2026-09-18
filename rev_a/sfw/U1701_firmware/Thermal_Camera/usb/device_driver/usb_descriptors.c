/*******************************************************************************
  USB Descriptor Tables

  File Name:
    usb_descriptors.c

  Summary:
    Descriptor tables for the USB mass storage device. See
    usb_descriptors.h for the identity/speed design notes.
*******************************************************************************/

#include <xc.h>

#include "usb/device_driver/usb_descriptors.h"

#define USB_WORD(w)  (uint8_t)((w) & 0xFFu), (uint8_t)(((w) >> 8) & 0xFFu)

const uint8_t usb_device_descriptor[18] = {
    18,                             // bLength
    USB_DESC_TYPE_DEVICE,           // bDescriptorType
    USB_WORD(0x0200),               // bcdUSB 2.00
    0x00,                           // bDeviceClass (per interface)
    0x00,                           // bDeviceSubClass
    0x00,                           // bDeviceProtocol
    USB_EP0_MAX_PACKET,             // bMaxPacketSize0
    USB_WORD(USB_VID),              // idVendor
    USB_WORD(USB_PID),              // idProduct
    USB_WORD(USB_BCD_DEVICE),       // bcdDevice
    1,                              // iManufacturer
    2,                              // iProduct
    3,                              // iSerialNumber
    1                               // bNumConfigurations
};

// Required for any HS-capable device: what the device would look like at
// the other speed (fields mirror the device descriptor)
const uint8_t usb_device_qualifier[10] = {
    10,                             // bLength
    USB_DESC_TYPE_DEVICE_QUALIFIER, // bDescriptorType
    USB_WORD(0x0200),               // bcdUSB
    0x00, 0x00, 0x00,               // class/subclass/protocol
    USB_EP0_MAX_PACKET,             // bMaxPacketSize0
    1,                              // bNumConfigurations
    0                               // bReserved
};

// Configuration + interface + 2 bulk endpoints = 9 + 9 + 7 + 7 = 32 bytes.
// Generated per speed because only wMaxPacketSize differs. bDescriptorType
// is patched to OTHER_SPEED_CFG at serve time by usb.c when the host asks
// for the other-speed flavor, per USB 2.0 9.6.4.
#define USB_CONFIG_DESC_BODY(bulkPacketSize)                                   \
    /* Configuration */                                                        \
    9,                              /* bLength */                              \
    USB_DESC_TYPE_CONFIGURATION,    /* bDescriptorType */                      \
    USB_WORD(USB_CONFIG_DESC_TOTAL_LENGTH), /* wTotalLength */                 \
    1,                              /* bNumInterfaces */                       \
    1,                              /* bConfigurationValue */                  \
    0,                              /* iConfiguration */                       \
    0xC0,                           /* bmAttributes: self-powered */           \
    50,                             /* bMaxPower: 100mA (hub port feeds us) */ \
    /* Interface: MSC / SCSI transparent / Bulk-Only Transport */              \
    9,                              /* bLength */                              \
    USB_DESC_TYPE_INTERFACE,        /* bDescriptorType */                      \
    0,                              /* bInterfaceNumber */                     \
    0,                              /* bAlternateSetting */                    \
    2,                              /* bNumEndpoints */                        \
    0x08,                           /* bInterfaceClass: mass storage */        \
    0x06,                           /* bInterfaceSubClass: SCSI transparent */ \
    0x50,                           /* bInterfaceProtocol: BOT */              \
    0,                              /* iInterface */                           \
    /* Endpoint: bulk IN 0x81 */                                               \
    7,                              /* bLength */                              \
    USB_DESC_TYPE_ENDPOINT,         /* bDescriptorType */                      \
    0x80u | USB_BULK_EP_NUM,        /* bEndpointAddress */                     \
    0x02,                           /* bmAttributes: bulk */                   \
    USB_WORD(bulkPacketSize),       /* wMaxPacketSize */                       \
    0,                              /* bInterval */                            \
    /* Endpoint: bulk OUT 0x01 */                                              \
    7,                              /* bLength */                              \
    USB_DESC_TYPE_ENDPOINT,         /* bDescriptorType */                      \
    USB_BULK_EP_NUM,                /* bEndpointAddress */                     \
    0x02,                           /* bmAttributes: bulk */                   \
    USB_WORD(bulkPacketSize),       /* wMaxPacketSize */                       \
    0                               /* bInterval */

const uint8_t usb_config_descriptor_hs[USB_CONFIG_DESC_TOTAL_LENGTH] = {
    USB_CONFIG_DESC_BODY(USB_BULK_MAX_PACKET_HS)
};

const uint8_t usb_config_descriptor_fs[USB_CONFIG_DESC_TOTAL_LENGTH] = {
    USB_CONFIG_DESC_BODY(USB_BULK_MAX_PACKET_FS)
};

// ---- String descriptors (UTF-16LE, built as byte arrays) ----

#define USB_STRING_HEADER(charCount)  (uint8_t)(2u + 2u * (charCount)), USB_DESC_TYPE_STRING
#define USB_UTF16(c)  (c), 0x00

static const uint8_t usb_string0_langid[] = {
    USB_STRING_HEADER(1),
    USB_WORD(0x0409)    // English (United States)
};

static const uint8_t usb_string1_manufacturer[] = {
    USB_STRING_HEADER(12),
    USB_UTF16('D'), USB_UTF16('r'), USB_UTF16('e'), USB_UTF16('w'),
    USB_UTF16(' '), USB_UTF16('M'), USB_UTF16('a'), USB_UTF16('a'),
    USB_UTF16('t'), USB_UTF16('m'), USB_UTF16('a'), USB_UTF16('n')
};

static const uint8_t usb_string2_product[] = {
    USB_STRING_HEADER(22),
    USB_UTF16('T'), USB_UTF16('h'), USB_UTF16('e'), USB_UTF16('r'),
    USB_UTF16('m'), USB_UTF16('a'), USB_UTF16('l'), USB_UTF16(' '),
    USB_UTF16('C'), USB_UTF16('a'), USB_UTF16('m'), USB_UTF16('e'),
    USB_UTF16('r'), USB_UTF16('a'), USB_UTF16(' '), USB_UTF16('S'),
    USB_UTF16('t'), USB_UTF16('o'), USB_UTF16('r'), USB_UTF16('a'),
    USB_UTF16('g'), USB_UTF16('e')
};

// Serial: 16 hex digits from the factory device serial number SFRs,
// rendered into UTF-16LE on first use (the SFRs are constant, so once is
// enough). MSC spec requires >=12 hex digits, unique per unit.
static uint8_t usb_string3_serial[2 + 16 * 2];

static void usbBuildSerialString(void)
{
    static const char hex[] = "0123456789ABCDEF";
    uint64_t serial = ((uint64_t)DEVSN1 << 32) | DEVSN0;

    usb_string3_serial[0] = sizeof(usb_string3_serial);
    usb_string3_serial[1] = USB_DESC_TYPE_STRING;

    for (uint8_t i = 0; i < 16; i++)
    {
        uint8_t nibble = (uint8_t)((serial >> (60 - 4 * i)) & 0xFu);
        usb_string3_serial[2 + 2 * i] = (uint8_t)hex[nibble];
        usb_string3_serial[3 + 2 * i] = 0x00;
    }
}

const uint8_t *USB_GetStringDescriptor(uint8_t index, uint16_t *length)
{
    switch (index)
    {
        case 0:
            *length = sizeof(usb_string0_langid);
            return usb_string0_langid;

        case 1:
            *length = sizeof(usb_string1_manufacturer);
            return usb_string1_manufacturer;

        case 2:
            *length = sizeof(usb_string2_product);
            return usb_string2_product;

        case 3:
            if (usb_string3_serial[0] == 0)
            {
                usbBuildSerialString();
            }
            *length = sizeof(usb_string3_serial);
            return usb_string3_serial;

        default:
            *length = 0;
            return NULL;
    }
}
