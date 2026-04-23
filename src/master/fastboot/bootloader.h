#pragma once
#include <cstdint>

enum class BootMode : std::uint32_t {
  User = 1,
  Fastboot = 2,
};

struct BootData {
  static constexpr std::uint32_t BOOTLOADER_DATA_MAGIC =
      0x54534146;  // `FAST` string, little-endian
  std::uint32_t magic;
  BootMode boot_mode;

  constexpr bool valid() const { return magic == BOOTLOADER_DATA_MAGIC; }

  constexpr void set(BootMode mode) {
    magic = BOOTLOADER_DATA_MAGIC;
    boot_mode = mode;
  }

  constexpr void clear() { magic = 0x0; }
};

#define BOOTDATA reinterpret_cast<BootData*>(FASTBOOT_BOOT_DATA_BASE)

namespace bootloader {

void StartBootFlow();

}  // namespace bootloader
