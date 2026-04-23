#ifndef _FASTBOOT_H_
#define _FASTBOOT_H_ 1

#include <stddef.h>
#include <stdint.h>

/* USB VID/PIDs for fastboot devices */
#define FASTBOOT_VID (0x18D1u)
#define FASTBOOT_PID (0xD00Du)

/* To be detected as a fastboot device, the interface descriptor
 * of the USB device must present the following class values. */
#define FASTBOOT_USB_IFC_CLASS (0xFFU)
#define FASTBOOT_USB_IFC_SUBCLASS (0x42U)
#define FASTBOOT_USB_IFC_PROTOCOL (0x03U)

#endif