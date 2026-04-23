/*
 * Copyright (c) 2015 - 2016, Freescale Semiconductor, Inc.
 * Copyright 2016, 2019 NXP
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "fastboot.h"
#include "usb_device_config.h"
#include "usb.h"
#include "usb_device.h"

#include "usb_device_class.h"

#include "usb_device_descriptor.h"

/*******************************************************************************
 * Variables
 ******************************************************************************/

/* USB IN/OUT endpoints used for command transfer */
usb_device_endpoint_struct_t g_UsbDeviceFastbootEndpoints[FASTBOOT_USB_ENDPOINT_COUNT] = {
    {
        FASTBOOT_USB_BULK_IN_ENDPOINT | (USB_IN << 7U),
        USB_ENDPOINT_BULK,
        FS_FASTBOOT_BULK_IN_PACKET_SIZE,
        0U,
    },
    {
        FASTBOOT_USB_BULK_OUT_ENDPOINT | (USB_OUT << 7U),
        USB_ENDPOINT_BULK,
        FS_FASTBOOT_BULK_OUT_PACKET_SIZE,
        0U,
    }};

/* USB interface, encapsulates the endpoints */
usb_device_interface_struct_t g_UsbDeviceFastbootInterfaceEndpoints[] = {{
    0,
    {
        ARRAY_SIZE(g_UsbDeviceFastbootEndpoints),
        g_UsbDeviceFastbootEndpoints,
    },
    .classSpecific = NULL,
}};
usb_device_interfaces_struct_t g_UsbDeviceFastbootInterfaces[] = {
    {
        FASTBOOT_USB_IFC_CLASS, FASTBOOT_USB_IFC_SUBCLASS, FASTBOOT_USB_IFC_PROTOCOL,
        FASTBOOT_USB_INTERFACE_INDEX,
        g_UsbDeviceFastbootInterfaceEndpoints,
        ARRAY_SIZE(g_UsbDeviceFastbootInterfaceEndpoints)
     },
};
usb_device_interface_list_t g_UsbDeviceFastbootConfigurationList[USB_DEVICE_CONFIGURATION_COUNT] = {
    {
        ARRAY_SIZE(g_UsbDeviceFastbootInterfaces),
        g_UsbDeviceFastbootInterfaces,
    },
};

/* Configuration for the vendor device class */
usb_device_class_struct_t g_UsbDeviceFastbootConfig = {
    g_UsbDeviceFastbootConfigurationList,
    kUSB_DeviceClassTypeCdc,
    FASTBOOT_USB_CONFIGURE_COUNT,
};

/* Define device descriptor */
USB_DMA_INIT_DATA_ALIGN(USB_DATA_ALIGN_SIZE)
uint8_t g_UsbDeviceDescriptor[] = {
    /* Size of this descriptor in bytes */
    USB_DESCRIPTOR_LENGTH_DEVICE,
    /* DEVICE Descriptor Type */
    USB_DESCRIPTOR_TYPE_DEVICE,
    /* USB Specification Release Number in Binary-Coded Decimal (i.e., 2.10 is 210H). */
    USB_SHORT_GET_LOW(USB_DEVICE_SPECIFIC_BCD_VERSION),
    USB_SHORT_GET_HIGH(USB_DEVICE_SPECIFIC_BCD_VERSION),
    /* Class code (assigned by the USB-IF). */
    USB_DEVICE_CLASS,
    /* Subclass code (assigned by the USB-IF). */
    USB_DEVICE_SUBCLASS,
    /* Protocol code (assigned by the USB-IF). */
    USB_DEVICE_PROTOCOL,
    /* Maximum packet size for endpoint zero (only 8, 16, 32, or 64 are valid) */
    USB_CONTROL_MAX_PACKET_SIZE,
    USB_SHORT_GET_LOW(USB_DEVICE_VID),
    USB_SHORT_GET_HIGH(USB_DEVICE_VID), /* Vendor ID (assigned by the USB-IF) */
    USB_SHORT_GET_LOW(USB_DEVICE_PID),
    USB_SHORT_GET_HIGH(USB_DEVICE_PID), /* Product ID (assigned by the manufacturer) */
    /* Device release number in binary-coded decimal */
    USB_SHORT_GET_LOW(USB_DEVICE_DEMO_BCD_VERSION),
    USB_SHORT_GET_HIGH(USB_DEVICE_DEMO_BCD_VERSION),
    /* Index of string descriptor describing manufacturer */
    0x01,
    /* Index of string descriptor describing product */
    0x02,
    /* Index of string descriptor describing the device's serial number */
    0x03,
    /* Number of possible configurations */
    USB_DEVICE_CONFIGURATION_COUNT,
};

/* Define configuration descriptor */
USB_DMA_INIT_DATA_ALIGN(USB_DATA_ALIGN_SIZE)
uint8_t g_UsbDeviceConfigurationDescriptor[] = {
    /* Size of this descriptor in bytes */
    USB_DESCRIPTOR_LENGTH_CONFIGURE,
    /* CONFIGURATION Descriptor Type */
    USB_DESCRIPTOR_TYPE_CONFIGURE,
    /* Total length of data returned for this configuration. */
    USB_SHORT_GET_LOW(USB_DESCRIPTOR_LENGTH_CONFIGURE +
                      USB_DESCRIPTOR_LENGTH_INTERFACE + USB_DESCRIPTOR_LENGTH_ENDPOINT +
                      USB_DESCRIPTOR_LENGTH_ENDPOINT),
    USB_SHORT_GET_HIGH(USB_DESCRIPTOR_LENGTH_CONFIGURE +
                       USB_DESCRIPTOR_LENGTH_INTERFACE + USB_DESCRIPTOR_LENGTH_ENDPOINT +
                       USB_DESCRIPTOR_LENGTH_ENDPOINT),
    /* Number of interfaces supported by this configuration */
    FASTBOOT_USB_INTERFACE_COUNT,
    /* Value to use as an argument to the SetConfiguration() request to select this configuration */
    FASTBOOT_USB_CONFIGURE_INDEX,
    /* Index of string descriptor describing this configuration */
    0,
    /* Configuration characteristics D7: Reserved (set to one) D6: Self-powered D5: Remote Wakeup D4...0: Reserved
       (reset to zero) */
    (USB_DESCRIPTOR_CONFIGURE_ATTRIBUTE_D7_MASK) |
        (USB_DEVICE_CONFIG_SELF_POWER << USB_DESCRIPTOR_CONFIGURE_ATTRIBUTE_SELF_POWERED_SHIFT) |
        (USB_DEVICE_CONFIG_REMOTE_WAKEUP << USB_DESCRIPTOR_CONFIGURE_ATTRIBUTE_REMOTE_WAKEUP_SHIFT),
    /* Maximum power consumption of the USB * device from the bus in this specific * configuration when the device is
       fully * operational. Expressed in 2 mA units *  (i.e., 50 = 100 mA).  */
    USB_DEVICE_MAX_POWER,

    /* Fastboot Interface Descriptor */
    USB_DESCRIPTOR_LENGTH_INTERFACE, USB_DESCRIPTOR_TYPE_INTERFACE,
    FASTBOOT_USB_INTERFACE_INDEX, FASTBOOT_USB_INTERFACE_ALTERNATE_COUNT, /* Interface index and number of alternates */
    FASTBOOT_USB_ENDPOINT_COUNT,
    FASTBOOT_USB_IFC_CLASS, FASTBOOT_USB_IFC_SUBCLASS, FASTBOOT_USB_IFC_PROTOCOL,
    0x04, /* Interface Description String Index*/

    /* Fastboot data bulk IN endpoint descriptor */
    USB_DESCRIPTOR_LENGTH_ENDPOINT, USB_DESCRIPTOR_TYPE_ENDPOINT, FASTBOOT_USB_BULK_IN_ENDPOINT | (USB_IN << 7U),
    USB_ENDPOINT_BULK, USB_SHORT_GET_LOW(FS_FASTBOOT_BULK_IN_PACKET_SIZE),
    USB_SHORT_GET_HIGH(FS_FASTBOOT_BULK_IN_PACKET_SIZE), 0x00, /* The polling interval value is every 0 Frames */
    /* Fastboot data bulk OUT endpoint descriptor */
    USB_DESCRIPTOR_LENGTH_ENDPOINT, USB_DESCRIPTOR_TYPE_ENDPOINT, FASTBOOT_USB_BULK_OUT_ENDPOINT | (USB_OUT << 7U),
    USB_ENDPOINT_BULK, USB_SHORT_GET_LOW(FS_FASTBOOT_BULK_OUT_PACKET_SIZE),
    USB_SHORT_GET_HIGH(FS_FASTBOOT_BULK_OUT_PACKET_SIZE), 0x00, /* The polling interval value is every 0 Frames */
};

/* Define string descriptor */
USB_DMA_INIT_DATA_ALIGN(USB_DATA_ALIGN_SIZE)
uint8_t g_UsbDeviceString0[] = {2U + 2U, USB_DESCRIPTOR_TYPE_STRING, 0x09, 0x04};

USB_DMA_INIT_DATA_ALIGN(USB_DATA_ALIGN_SIZE)
uint8_t g_UsbDeviceString1[] = {
    2U + 2U * 3U, USB_DESCRIPTOR_TYPE_STRING,
    'T',          0x00U,
    'T',          0x00U,
    'S',          0x00U,
};

USB_DMA_INIT_DATA_ALIGN(USB_DATA_ALIGN_SIZE)
uint8_t g_UsbDeviceString2[] = {
    2U + 2U * 12U, USB_DESCRIPTOR_TYPE_STRING,
    'T',           0x00U,
    'T',           0x00U,
    'S',           0x00U,
    ' ',           0x00U,
    'F',           0x00U,
    'a',           0x00U,
    's',           0x00U,
    't',           0x00U,
    'b',           0x00U,
    'o',           0x00U,
    'o',           0x00U,
    't',           0x00U,
};

USB_DMA_INIT_DATA_ALIGN(USB_DATA_ALIGN_SIZE)
uint8_t g_UsbDeviceString3[] = {
    2U + 2U * 16U, USB_DESCRIPTOR_TYPE_STRING,
    'M',           0x00U,
    'I',           0x00U,
    'M',           0x00U,
    'X',           0x00U,
    'R',           0x00U,
    'T',           0x00U,
    '5',           0x00U,
    '9',           0x00U,
    '5',           0x00U,
    '-',           0x00U,
    'm',           0x00U,
    'a',           0x00U,
    's',           0x00U,
    't',           0x00U,
    'e',           0x00U,
    'r',           0x00U,
};

USB_DMA_INIT_DATA_ALIGN(USB_DATA_ALIGN_SIZE)
uint8_t g_UsbDeviceString4[] = {
    2U + 2U * 8U, USB_DESCRIPTOR_TYPE_STRING,
    'f',           0x00U,
    'a',           0x00U,
    's',           0x00U,
    't',           0x00U,
    'b',           0x00U,
    'o',           0x00U,
    'o',           0x00U,
    't',           0x00U,
};

uint8_t *g_UsbDeviceStringDescriptorArray[USB_DEVICE_STRING_COUNT] = {g_UsbDeviceString0, g_UsbDeviceString1,
                                                                      g_UsbDeviceString2, g_UsbDeviceString3,
                                                                      g_UsbDeviceString4};

/* Define string descriptor size */
uint32_t g_UsbDeviceStringDescriptorLength[USB_DEVICE_STRING_COUNT] = {
    sizeof(g_UsbDeviceString0), sizeof(g_UsbDeviceString1), sizeof(g_UsbDeviceString2), sizeof(g_UsbDeviceString3),
    sizeof(g_UsbDeviceString4)};
usb_language_t g_UsbDeviceLanguage[USB_DEVICE_LANGUAGE_COUNT] = {{
    g_UsbDeviceStringDescriptorArray,
    g_UsbDeviceStringDescriptorLength,
    (uint16_t)0x0409,
}};

usb_language_list_t g_UsbDeviceLanguageList = {
    g_UsbDeviceString0,
    sizeof(g_UsbDeviceString0),
    g_UsbDeviceLanguage,
    USB_DEVICE_LANGUAGE_COUNT,
};

/*******************************************************************************
 * Code
 ******************************************************************************/
/*!
 * @brief USB device get device descriptor function.
 *
 * This function gets the device descriptor of the USB device.
 *
 * @param handle The USB device handle.
 * @param deviceDescriptor The pointer to the device descriptor structure.
 *
 * @return A USB error code or kStatus_USB_Success.
 */
usb_status_t USB_DeviceGetDeviceDescriptor(usb_device_handle handle,
                                           usb_device_get_device_descriptor_struct_t *deviceDescriptor)
{
    (void)handle;
    deviceDescriptor->buffer = g_UsbDeviceDescriptor;
    deviceDescriptor->length = USB_DESCRIPTOR_LENGTH_DEVICE;
    return kStatus_USB_Success;
}

/*!
 * @brief USB device get configuration descriptor function.
 *
 * This function gets the configuration descriptor of the USB device.
 *
 * @param handle The USB device handle.
 * @param configurationDescriptor The pointer to the configuration descriptor structure.
 *
 * @return A USB error code or kStatus_USB_Success.
 */
usb_status_t USB_DeviceGetConfigurationDescriptor(
    usb_device_handle handle, usb_device_get_configuration_descriptor_struct_t *configurationDescriptor)
{
    (void)handle;
    if (FASTBOOT_USB_CONFIGURE_COUNT > configurationDescriptor->configuration)
    {
        configurationDescriptor->buffer = g_UsbDeviceConfigurationDescriptor;
        configurationDescriptor->length = USB_DESCRIPTOR_LENGTH_CONFIGURATION_ALL;
        return kStatus_USB_Success;
    }
    return kStatus_USB_InvalidRequest;
}

/*!
 * @brief USB device get string descriptor function.
 *
 * This function gets the string descriptor of the USB device.
 *
 * @param handle The USB device handle.
 * @param stringDescriptor Pointer to the string descriptor structure.
 *
 * @return A USB error code or kStatus_USB_Success.
 */
usb_status_t USB_DeviceGetStringDescriptor(usb_device_handle handle,
                                           usb_device_get_string_descriptor_struct_t *stringDescriptor)
{
    (void)handle;
    if (stringDescriptor->stringIndex == 0U)
    {
        stringDescriptor->buffer = (uint8_t *)g_UsbDeviceLanguageList.languageString;
        stringDescriptor->length = g_UsbDeviceLanguageList.stringLength;
    }
    else
    {
        uint8_t languageId    = 0U;
        uint8_t languageIndex = USB_DEVICE_STRING_COUNT;

        for (; languageId < USB_DEVICE_LANGUAGE_COUNT; languageId++)
        {
            if (stringDescriptor->languageId == g_UsbDeviceLanguageList.languageList[languageId].languageId)
            {
                if (stringDescriptor->stringIndex < USB_DEVICE_STRING_COUNT)
                {
                    languageIndex = stringDescriptor->stringIndex;
                }
                break;
            }
        }

        if (USB_DEVICE_STRING_COUNT == languageIndex)
        {
            return kStatus_USB_InvalidRequest;
        }
        stringDescriptor->buffer = (uint8_t *)g_UsbDeviceLanguageList.languageList[languageId].string[languageIndex];
        stringDescriptor->length = g_UsbDeviceLanguageList.languageList[languageId].length[languageIndex];
    }
    return kStatus_USB_Success;
}

/*!
 * @brief USB device set speed function.
 *
 * This function sets the speed of the USB device.
 *
 * Due to the difference of HS and FS descriptors, the device descriptors and configurations need to be updated to match
 * current speed.
 * As the default, the device descriptors and configurations are configured by using FS parameters for both EHCI and
 * KHCI.
 * When the EHCI is enabled, the application needs to call this function to update device by using current speed.
 * The updated information includes endpoint max packet size, endpoint interval, etc.
 *
 * @param handle The USB device handle.
 * @param speed Speed type. USB_SPEED_HIGH/USB_SPEED_FULL/USB_SPEED_LOW.
 *
 * @return A USB error code or kStatus_USB_Success.
 */
usb_status_t USB_DeviceSetSpeed(usb_device_handle handle, uint8_t speed)
{
    usb_descriptor_union_t *ptr1;
    usb_descriptor_union_t *ptr2;
    (void)handle;

    ptr1 = (usb_descriptor_union_t *)(&g_UsbDeviceConfigurationDescriptor[0]);
    ptr2 = (usb_descriptor_union_t *)(&g_UsbDeviceConfigurationDescriptor[USB_DESCRIPTOR_LENGTH_CONFIGURATION_ALL - 1]);

    while (ptr1 < ptr2)
    {
        if (ptr1->common.bDescriptorType == USB_DESCRIPTOR_TYPE_ENDPOINT)
        {
            if (USB_SPEED_HIGH == speed)
            {
                /* Update the max packet size on fastboot bulk IN/OUT endpoints */
                if (((ptr1->endpoint.bEndpointAddress & USB_DESCRIPTOR_ENDPOINT_ADDRESS_DIRECTION_MASK) ==
                     USB_DESCRIPTOR_ENDPOINT_ADDRESS_DIRECTION_IN) &&
                    (FASTBOOT_USB_BULK_IN_ENDPOINT == (ptr1->endpoint.bEndpointAddress & USB_ENDPOINT_NUMBER_MASK)))
                {
                    USB_SHORT_TO_LITTLE_ENDIAN_ADDRESS(HS_FASTBOOT_BULK_IN_PACKET_SIZE, ptr1->endpoint.wMaxPacketSize);
                }
                else if (((ptr1->endpoint.bEndpointAddress & USB_DESCRIPTOR_ENDPOINT_ADDRESS_DIRECTION_MASK) ==
                          USB_DESCRIPTOR_ENDPOINT_ADDRESS_DIRECTION_OUT) &&
                         (FASTBOOT_USB_BULK_OUT_ENDPOINT ==
                          (ptr1->endpoint.bEndpointAddress & USB_ENDPOINT_NUMBER_MASK)))
                {
                    USB_SHORT_TO_LITTLE_ENDIAN_ADDRESS(HS_FASTBOOT_BULK_OUT_PACKET_SIZE, ptr1->endpoint.wMaxPacketSize);
                }
                else
                {
                    /* no action */
                }
            }
            else
            {
                if (((ptr1->endpoint.bEndpointAddress & USB_DESCRIPTOR_ENDPOINT_ADDRESS_DIRECTION_MASK) ==
                     USB_DESCRIPTOR_ENDPOINT_ADDRESS_DIRECTION_IN) &&
                    (FASTBOOT_USB_BULK_IN_ENDPOINT == (ptr1->endpoint.bEndpointAddress & USB_ENDPOINT_NUMBER_MASK)))
                {
                    USB_SHORT_TO_LITTLE_ENDIAN_ADDRESS(FS_FASTBOOT_BULK_IN_PACKET_SIZE, ptr1->endpoint.wMaxPacketSize);
                }
                else if (((ptr1->endpoint.bEndpointAddress & USB_DESCRIPTOR_ENDPOINT_ADDRESS_DIRECTION_MASK) ==
                          USB_DESCRIPTOR_ENDPOINT_ADDRESS_DIRECTION_OUT) &&
                         (FASTBOOT_USB_BULK_OUT_ENDPOINT ==
                          (ptr1->endpoint.bEndpointAddress & USB_ENDPOINT_NUMBER_MASK)))
                {
                    USB_SHORT_TO_LITTLE_ENDIAN_ADDRESS(FS_FASTBOOT_BULK_OUT_PACKET_SIZE, ptr1->endpoint.wMaxPacketSize);
                }
                else
                {
                    /* no action */
                }
            }
        }
        ptr1 = (usb_descriptor_union_t *)((uint8_t *)ptr1 + ptr1->common.bLength);
    }

    /* Update internal structures as well */
    for (int i = 0; i < 2; i++)
    {
        if (USB_SPEED_HIGH == speed)
        {
            if (g_UsbDeviceFastbootEndpoints[i].endpointAddress & USB_DESCRIPTOR_ENDPOINT_ADDRESS_DIRECTION_MASK)
            {
                g_UsbDeviceFastbootEndpoints[i].maxPacketSize = HS_FASTBOOT_BULK_IN_PACKET_SIZE;
            }
            else
            {
                g_UsbDeviceFastbootEndpoints[i].maxPacketSize = HS_FASTBOOT_BULK_OUT_PACKET_SIZE;
            }
        }
        else
        {
            if (g_UsbDeviceFastbootEndpoints[i].endpointAddress & USB_DESCRIPTOR_ENDPOINT_ADDRESS_DIRECTION_MASK)
            {
                g_UsbDeviceFastbootEndpoints[i].maxPacketSize = FS_FASTBOOT_BULK_IN_PACKET_SIZE;
            }
            else
            {
                g_UsbDeviceFastbootEndpoints[i].maxPacketSize = FS_FASTBOOT_BULK_OUT_PACKET_SIZE;
            }
        }
    }

    return kStatus_USB_Success;
}
