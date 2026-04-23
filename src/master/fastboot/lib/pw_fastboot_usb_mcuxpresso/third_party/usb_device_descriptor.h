/*
 * Copyright (c) 2015 - 2016, Freescale Semiconductor, Inc.
 * Copyright 2016 NXP
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef _USB_DEVICE_DESCRIPTOR_H_
#define _USB_DEVICE_DESCRIPTOR_H_ 1

#include "fastboot.h"

/*******************************************************************************
 * Definitions
 ******************************************************************************/
#define USB_DEVICE_SPECIFIC_BCD_VERSION (0x0200U)
#define USB_DEVICE_DEMO_BCD_VERSION     (0x0101U)

#define USB_DEVICE_VID (FASTBOOT_VID)
#define USB_DEVICE_PID (FASTBOOT_PID)

/* Vendor-specific class, not defined by USB-IF */
#define USB_CLASS_VENDOR_SPECIFIC (0xFFU)

/* usb descriptor length */
#define USB_DESCRIPTOR_LENGTH_CONFIGURATION_ALL (sizeof(g_UsbDeviceConfigurationDescriptor))

/* Configuration, interface and endpoint. */
#define USB_DEVICE_CONFIGURATION_COUNT (1U)
#define USB_DEVICE_STRING_COUNT        (5U)
#define USB_DEVICE_LANGUAGE_COUNT      (1U)

#define FASTBOOT_USB_BULK_IN_ENDPOINT               (1U)
#define FASTBOOT_USB_BULK_OUT_ENDPOINT              (2U)
#define FASTBOOT_USB_CONFIGURE_INDEX                (1U)
#define FASTBOOT_USB_CONFIGURE_COUNT                (1U)
#define FASTBOOT_USB_ENDPOINT_COUNT                 (2U)
#define FASTBOOT_USB_INTERFACE_INDEX                (0U)
#define FASTBOOT_USB_INTERFACE_COUNT                (1U)
#define FASTBOOT_USB_INTERFACE_ALTERNATE_COUNT      (0U)
/* Packet size, for high speed and full speed */
#define HS_FASTBOOT_BULK_IN_PACKET_SIZE  (512U)
#define FS_FASTBOOT_BULK_IN_PACKET_SIZE  (64U)
#define HS_FASTBOOT_BULK_OUT_PACKET_SIZE (512U)
#define FS_FASTBOOT_BULK_OUT_PACKET_SIZE (64U)

/* Present the device class per interface so host matching is driven by the
 * fastboot interface descriptor (ff/42/03) rather than the whole device. */
#define USB_DEVICE_CLASS    (0x00U)
#define USB_DEVICE_SUBCLASS (0x00U)
#define USB_DEVICE_PROTOCOL (0x00U)

#define USB_DEVICE_MAX_POWER (0x32U)

/*******************************************************************************
 * API
 ******************************************************************************/
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
extern usb_status_t USB_DeviceSetSpeed(usb_device_handle handle, uint8_t speed);
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
extern usb_status_t USB_DeviceGetDeviceDescriptor(usb_device_handle handle,
                                                  usb_device_get_device_descriptor_struct_t *deviceDescriptor);
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
                                           usb_device_get_string_descriptor_struct_t *stringDescriptor);
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
extern usb_status_t USB_DeviceGetConfigurationDescriptor(
    usb_device_handle handle, usb_device_get_configuration_descriptor_struct_t *configurationDescriptor);
#endif /* _USB_DEVICE_DESCRIPTOR_H_ */
