#pragma once
#include "pw_bytes/span.h"
#include "pw_fastboot/commands.h"
#include "pw_fastboot/fastboot_device.h"

namespace bootloader {

struct Partition {
  uint32_t start;
  uint32_t length;
  char name[16];
};

pw::fastboot::CommandResult DoFlash(pw::fastboot::Device*, std::string);

}  // namespace bootloader
