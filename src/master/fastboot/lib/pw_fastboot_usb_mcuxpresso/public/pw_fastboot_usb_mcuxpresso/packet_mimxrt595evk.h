#pragma once

#include "pw_fastboot_usb/packet.h"

namespace pw::fastboot {
class Mimxrt595UsbPacketInterface : public pw::fastboot::UsbPacketInterface {
 public:
  Mimxrt595UsbPacketInterface() = default;
  ~Mimxrt595UsbPacketInterface() = default;

  void Init(InitStatusUpdateCb) override;
  void Deinit() override;
  std::ptrdiff_t QueuePacket(pw::ConstByteSpan) override;

 private:
};
}  // namespace pw::fastboot