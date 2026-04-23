#pragma once

#include <memory>

#include "pw_fastboot_usb/transport.h"
#include "pw_fastboot_usb_mcuxpresso/packet_mimxrt595evk.h"

namespace pw::fastboot {
inline std::unique_ptr<UsbTransport> CreateMimxrt595UsbTransport() {
  return std::make_unique<UsbTransport>(
      std::make_unique<Mimxrt595UsbPacketInterface>());
}

}  // namespace pw::fastboot