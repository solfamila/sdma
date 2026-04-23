extern "C" {
// clang-format off
#include "usb.h"
#include "usb_device.h"

#include "usb_device_class.h"
#include "usb_device_cdc_acm.h"
#include "usb_device_descriptor.h"
#include "virtual_com.h"
// clang-format on
}
#include "private/packet_mimxrt595evk.h"

#include <mutex>

#include "pw_bytes/span.h"
#include "pw_fastboot_usb_mcuxpresso/packet_mimxrt595evk.h"
#include "pw_sync/interrupt_spin_lock.h"

// Initialize the USB transport
static void UsbInit() {
  extern usb_cdc_vcom_struct_t s_cdcVcom;
  APPTask(&s_cdcVcom);
}

/* Try enqueueing a fastboot packet
 */
static std::ptrdiff_t UsbTryQueuePacket(pw::ConstByteSpan packet) {
  extern usb_cdc_vcom_struct_t s_cdcVcom;
  // The RT595 USB IP DMA reads IN payloads directly, so stage them in the
  // same aligned DMA-safe storage class used by the NXP USB stack.
  USB_DMA_NONINIT_DATA_ALIGN(USB_DATA_ALIGN_SIZE)
  static uint8_t s_transfer_buffer[pw::fastboot::kFastbootUsbMaxPacketSize];
  static pw::sync::InterruptSpinLock s_state_lock;

  std::lock_guard lg{s_state_lock};
  // If controller is already sending packet, fail as we would clobber
  // s_transfer_buffer.
  if (((usb_device_cdc_acm_struct_t*)s_cdcVcom.cdcAcmHandle)->bulkIn.isBusy) {
    return -1;
  }
  // Copy to an intermediate buffer, lifetime of `packet` is limited to only
  // this function call.
  const auto transfer_size = std::min(packet.size(), sizeof(s_transfer_buffer));
  std::memcpy(s_transfer_buffer, packet.data(), transfer_size);
  // Set the transfer
  const auto err = USB_DeviceCdcAcmSend(s_cdcVcom.cdcAcmHandle,
                                        FASTBOOT_USB_BULK_IN_ENDPOINT,
                                        (uint8_t*)s_transfer_buffer,
                                        transfer_size);
  if (err == kStatus_USB_Success) {
    return transfer_size;
  }
  return -1;
}

static pw::fastboot::Mimxrt595UsbPacketInterface* s_interface = nullptr;

void pw::fastboot::Mimxrt595UsbPacketInterface::Init(InitStatusUpdateCb cb) {
  // Can only register one packet interface object
  if (s_interface) {
    cb(false);
    return;
  }
  s_interface = this;
  UsbInit();
  cb(true);
}

void pw::fastboot::Mimxrt595UsbPacketInterface::Deinit() {
  // Release the handle when we're being destroyed
  if (this == s_interface) {
    s_interface = nullptr;
  }
}

std::ptrdiff_t pw::fastboot::Mimxrt595UsbPacketInterface::QueuePacket(
    pw::ConstByteSpan packet) {
  return UsbTryQueuePacket(packet);
}

/* WARNING: This is within interrupt context!
 */
extern "C" void OnFastbootPacketReceived(uint8_t const* buf, size_t len) {
  if (!s_interface) {
    return;
  }
  s_interface->OnPacketReceived(pw::ConstByteSpan{(std::byte const*)buf, len});
}

/* WARNING: This is within interrupt context!
 */
extern "C" void OnFastbootPacketSent(uint8_t const* buf, size_t len) {
  if (!s_interface) {
    return;
  }
  s_interface->OnPacketSent(pw::ConstByteSpan{(std::byte const*)buf, len});
}
