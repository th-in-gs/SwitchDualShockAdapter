#include "descriptors.h"
#include <assert.h>
#include <usbdrv/usbdrv.h>

PROGMEM const char usbDescriptorHidReport[] = {
    0x05, 0x01,                   // Usage Page (Generic Desktop Ctrls)
    0x15, 0x00,                   // Logical Minimum (0)
    0x09, 0x04,                   // Usage (Joystick)
    0xA1, 0x01,                   // Collection (Application)
    0x85, 0x30,                   //   Report ID (0x30)
    0x05, 0x01,                   //   Usage Page (Generic Desktop Ctrls)
    0x05, 0x09,                   //   Usage Page (Button)
    0x19, 0x01,                   //   Usage Minimum (0x01)
    0x29, 0x0A,                   //   Usage Maximum (0x0A)
    0x15, 0x00,                   //   Logical Minimum (0)
    0x25, 0x01,                   //   Logical Maximum (1)
    0x75, 0x01,                   //   Report Size (1)
    0x95, 0x0A,                   //   Report Count (10)
    0x55, 0x00,                   //   Unit Exponent (0)
    0x65, 0x00,                   //   Unit (None)
    0x81, 0x02,                   //   Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x05, 0x09,                   //   Usage Page (Button)
    0x19, 0x0B,                   //   Usage Minimum (0x0B)
    0x29, 0x0E,                   //   Usage Maximum (0x0E)
    0x15, 0x00,                   //   Logical Minimum (0)
    0x25, 0x01,                   //   Logical Maximum (1)
    0x75, 0x01,                   //   Report Size (1)
    0x95, 0x04,                   //   Report Count (4)
    0x81, 0x02,                   //   Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x75, 0x01,                   //   Report Size (1)
    0x95, 0x02,                   //   Report Count (2)
    0x81, 0x03,                   //   Input (Const,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x0B, 0x01, 0x00, 0x01, 0x00, //   Usage (0x010001)
    0xA1, 0x00,                   //   Collection (Physical)
    0x0B, 0x30, 0x00, 0x01, 0x00, //     Usage (0x010030)
    0x0B, 0x31, 0x00, 0x01, 0x00, //     Usage (0x010031)
    0x0B, 0x32, 0x00, 0x01, 0x00, //     Usage (0x010032)
    0x0B, 0x35, 0x00, 0x01, 0x00, //     Usage (0x010035)
    0x15, 0x00,                   //     Logical Minimum (0)
    0x27, 0xFF, 0xFF, 0x00, 0x00, //     Logical Maximum (65534)
    0x75, 0x10,                   //     Report Size (16)
    0x95, 0x04,                   //     Report Count (4)
    0x81, 0x02,                   //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0xC0,                         //   End Collection
    0x0B, 0x39, 0x00, 0x01, 0x00, //   Usage (0x010039)
    0x15, 0x00,                   //   Logical Minimum (0)
    0x25, 0x07,                   //   Logical Maximum (7)
    0x35, 0x00,                   //   Physical Minimum (0)
    0x46, 0x3B, 0x01,             //   Physical Maximum (315)
    0x65, 0x14,                   //   Unit (System: English Rotation, Length: Centimeter)
    0x75, 0x04,                   //   Report Size (4)
    0x95, 0x01,                   //   Report Count (1)
    0x81, 0x02,                   //   Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x05, 0x09,                   //   Usage Page (Button)
    0x19, 0x0F,                   //   Usage Minimum (0x0F)
    0x29, 0x12,                   //   Usage Maximum (0x12)
    0x15, 0x00,                   //   Logical Minimum (0)
    0x25, 0x01,                   //   Logical Maximum (1)
    0x75, 0x01,                   //   Report Size (1)
    0x95, 0x04,                   //   Report Count (4)
    0x81, 0x02,                   //   Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x75, 0x08,                   //   Report Size (8)
    0x95, 0x34,                   //   Report Count (52)
    0x81, 0x03,                   //   Input (Const,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x06, 0x00, 0xFF,             //   Usage Page (Vendor Defined 0xFF00)
    0x85, 0x21,                   //   Report ID (0x21)
    0x09, 0x01,                   //   Usage (0x01)
    0x75, 0x08,                   //   Report Size (8)
    0x95, 0x3F,                   //   Report Count (63)
    0x81, 0x03,                   //   Input (Const,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x85, 0x81,                   //   Report ID (0x81)
    0x09, 0x02,                   //   Usage (0x02)
    0x75, 0x08,                   //   Report Size (8)
    0x95, 0x3F,                   //   Report Count (63)
    0x81, 0x03,                   //   Input (Const,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x85, 0x01,                   //   Report ID (0x01)
    0x09, 0x03,                   //   Usage (0x03)
    0x75, 0x08,                   //   Report Size (8)
    0x95, 0x3F,                   //   Report Count (63)
    0x91, 0x83,                   //   Output (Const,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Volatile)
    0x85, 0x10,                   //   Report ID (0x10)
    0x09, 0x04,                   //   Usage (0x04)
    0x75, 0x08,                   //   Report Size (8)
    0x95, 0x3F,                   //   Report Count (63)
    0x91, 0x83,                   //   Output (Const,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Volatile)
    0x85, 0x80,                   //   Report ID (0x80)
    0x09, 0x05,                   //   Usage (0x05)
    0x75, 0x08,                   //   Report Size (8)
    0x95, 0x3F,                   //   Report Count (63)
    0x91, 0x83,                   //   Output (Const,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Volatile)
    0x85, 0x82,                   //   Report ID (0x82)
    0x09, 0x06,                   //   Usage (0x06)
    0x75, 0x08,                   //   Report Size (8)
    0x95, 0x3F,                   //   Report Count (63)
    0x91, 0x83,                   //   Output (Const,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Volatile)
    0xC0,                         // End Collection

    // 203 bytes
};

// Just to make sure these are in sync.
static_assert(sizeof(usbDescriptorHidReport) == USB_CFG_HID_REPORT_DESCRIPTOR_LENGTH, "usbHidReportDescriptor contains a different number of entries than the USB_CFG_HID_REPORT_DESCRIPTOR_LENGTH macro specifies");

PROGMEM const char usbDescriptorConfiguration[] = {
    9,                          //  8: sizeof(usbDescriptorConfiguration):
                                //     length of descriptor in bytes
    USBDESCR_CONFIG,            //  8: descriptor type
    USB_PROP_LENGTH(USB_CFG_DESCR_PROPS_CONFIGURATION), 0,
                                // 16: total length of data returned
                                //     (including inlined descriptors)
    2,                          //  8: number of interfaces in this configuration
    1,                          //  8: configuration value
                                //     (index of this configuration)
    0,                          //  8: configuration name string index (no name)
    1 << 7 |
    USBATTR_REMOTEWAKE,         //  8: attributes (standard requires bit 7
                                //     to be set)
    USB_CFG_MAX_BUS_POWER/2,    //  8: max USB current in 2mA units
    // Interface descriptors follow inline:
        9,                          //  8: sizeof(usbDescrInterface):
                                    //     length of descriptor in bytes
        USBDESCR_INTERFACE,         //  8: descriptor type
        0,                          //  8: index of this interface
        0,                          //  8: alternate setting for this interface
        2,                          //  8: number of endpoint descriptors to
                                    //     follow (_excluding_ endpoint 0)
        USB_CFG_INTERFACE_CLASS,    //  8: interface class code
        USB_CFG_INTERFACE_SUBCLASS, //  8: interface subclass code
        USB_CFG_INTERFACE_PROTOCOL, //  8: interface protocol code
        0,                          //  8: string index for interface

            9,                      //  8: sizeof(usbDescrHID):
                                    //     length of descriptor in bytes
            USBDESCR_HID,           //  8: descriptor type: HID
            0x01, 0x01,             //  8: BCD representation of HID version
            0x00,                   //  8: target country code
            0x01,                   //  8: number of HID Report (or other HID
                                    //     class) Descriptor infos to follow
            0x22,                   //  8: descriptor type: report
            (USB_CFG_HID_REPORT_DESCRIPTOR_LENGTH & 0xFF), ((USB_CFG_HID_REPORT_DESCRIPTOR_LENGTH >> 8) & 0xFF),
                                    // 16: descriptor length

            7,                          //  8: sizeof(usbDescrEndpoint)
            USBDESCR_ENDPOINT,          //  8: descriptor type = endpoint
            0x81,                       //  8: IN endpoint number 1
            0x03,                       //  8: attrib: Interrupt endpoint
            8, 0,                       // 16: maximum packet size
            2,                          //  8: poll interval in ms

            7,                          //  8: sizeof(usbDescrEndpoint)
            USBDESCR_ENDPOINT,          //  8: descriptor type = endpoint
            0x01,                       //  8: OUT endpoint number 1
            0x03,                       //  8: attrib: Interrupt endpoint
            8, 0,                       // 16: maximum packet size
            3,                          //  8: poll interval in ms
};

static_assert(sizeof(usbDescriptorConfiguration) == USB_PROP_LENGTH(USB_CFG_DESCR_PROPS_CONFIGURATION), "usbHidReportDescriptor contains a different number of entries than the USB_CFG_HID_REPORT_DESCRIPTOR_LENGTH macro specifies");