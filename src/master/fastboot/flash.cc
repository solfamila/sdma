#include "flash.h"

#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>

#include "pw_bytes/span.h"
#include "pw_fastboot/commands.h"
#include "pw_flash/flash.h"
#include "pw_flash_mcuxpresso/flash.h"
#include "pw_log/log.h"
#include "pw_status/status.h"
#include "sparse/sparse.h"

using namespace pw::fastboot;

// Partitions supported for flashing
// Start and length specify the bounds of the partition, the given
// name can be directly used to flash the partition using fastboot.
static constexpr bootloader::Partition kPartitions[] = {
    {FASTBOOT_BOOTLOADER_BEGIN, FASTBOOT_BOOTLOADER_SIZE, "fastboot"},
    {FASTBOOT_APP_VECTOR_TABLE, FASTBOOT_APP_SIZE, "system"},
};

static std::optional<bootloader::Partition> FindPartitionByName(
    std::string name) {
  auto parts = std::span{kPartitions};
  auto it = std::find_if(
      parts.begin(), parts.end(), [name](const bootloader::Partition& v) {
        return std::strcmp(v.name, name.c_str()) == 0;
      });
  return it != parts.end() ? std::optional{*it} : std::nullopt;
}

static bool VerifyFlashedData(pw::flash::Flash& flash,
                              pw::flash::Range flashed_range,
                              pw::ConstByteSpan data) {
  static constexpr size_t kValidateBufSize = 4096;
  static std::byte buffer[kValidateBufSize];

  if (flashed_range.size != data.size_bytes()) {
    return false;
  }

  size_t offset = 0;
  size_t current = flashed_range.size;
  while (current) {
    const auto read_size = std::min(current, kValidateBufSize);

    const auto err =
        flash.Read(pw::flash::Range{flashed_range.start + offset, read_size},
                   pw::ByteSpan{buffer});
    if (!err.ok()) {
      PW_LOG_ERROR(
          "Failed to read from flash offset %04x during validate (error=%s)",
          offset,
          err.str());
      return false;
    }

    if (memcmp(buffer, data.data() + offset, read_size) != 0) {
      PW_LOG_ERROR(
          "Validation failed - flashed contents do not match, block with size "
          "%04x at flash offset %04x",
          read_size,
          offset);
      return false;
    }

    offset += read_size;
    current -= read_size;
  }

  return true;
}

static CommandResult FlashRawImage(pw::flash::Flash& flash,
                                   const bootloader::Partition& part,
                                   pw::ConstByteSpan image) {
  auto err = flash.Erase(pw::flash::Range{part.start, image.size_bytes()});
  if (!err.ok() && !err.IsDataLoss()) {
    PW_LOG_ERROR("Erasing flash failed with error code %d", err.code());
    return CommandResult::Failed("erase failed");
  }

  auto range = pw::flash::Range{part.start, image.size_bytes()};
  err = flash.Write(range, image);
  if (!err.ok()) {
    PW_LOG_ERROR("Programming flash failed with error code %d", err.code());
    return CommandResult::Failed("write failed");
  }

  if (!VerifyFlashedData(flash, range, image)) {
    PW_LOG_ERROR("Verify failed");
    return CommandResult::Failed("verify failed");
  }

  PW_LOG_INFO("Flashing raw image completed!");
  return CommandResult::Okay();
}

static void SparsePrintWrapper(const char* fmt, ...) {
  std::va_list va;
  va_start(va, fmt);

  static constexpr size_t kBufSize = 256;
  char buf[kBufSize];
  auto n = std::vsnprintf(buf, kBufSize, fmt, va);
  if (n > 0) {
    // FIXME: Strip trailing newline
    PW_LOG_DEBUG("libsparse: %s", buf);
  }

  va_end(va);
}

static CommandResult FlashSparseImage(pw::flash::Flash& flash,
                                      const bootloader::Partition& part,
                                      pw::ConstByteSpan image) {
  struct FlashContext {
    pw::flash::Flash& flash;
    const bootloader::Partition& part;
    pw::ConstByteSpan image;
    // Offset from the start of `part`
    size_t offset;
  };
  FlashContext callback_context{flash, part, image, 0};

  // Used for printing debug information from libsparse
  sparse_print_verbose = SparsePrintWrapper;

  // const_cast: libsparse only reads from the buffer, but takes a generic char*
  // ptr instead of const.
  auto* image_ptr =
      const_cast<char*>(reinterpret_cast<const char*>(image.data()));
  auto* sparsefile =
      sparse_file_import_buf(image_ptr, image.size(), true, false);
  if (!sparsefile) {
    return CommandResult::Failed("importing sparse image failed");
  }

  const auto err = sparse_file_callback(
      sparsefile,
      false, /* sparse */
      false, /* crc */
      [](void* priv, const void* data, size_t len) -> int {
        if (!priv) {
          return -1;
        }

        auto& ctx = *static_cast<FlashContext*>(priv);
        PW_LOG_DEBUG("Sparse data chunk: data=%p len=%d", data, len);
        if (!data) {
          ctx.offset += len;
          return 0;
        }

        auto chunk =
            pw::ConstByteSpan{reinterpret_cast<const std::byte*>(data), len};
        auto range =
            pw::flash::Range{ctx.part.start + ctx.offset, chunk.size_bytes()};

        if (const auto err2 = ctx.flash.Erase(range); !err2.ok()) {
          PW_LOG_ERROR("Erasing flash failed with error code %d", err2.code());
          return -1;
        }

        if (const auto err2 = ctx.flash.Write(range, chunk); !err2.ok()) {
          PW_LOG_ERROR("Programming flash failed with error code %d",
                       err2.code());
          return -1;
        }

        if (!VerifyFlashedData(ctx.flash, range, chunk)) {
          PW_LOG_ERROR("Verify failed");
          return -1;
        }

        PW_LOG_INFO("Written %d bytes to flash", chunk.size_bytes());
        return 0;
      },
      &callback_context);

  if (sparsefile) {
    sparse_file_destroy(sparsefile);
  }

  if (err < 0) {
    return CommandResult::Failed("sparse flash failed");
  }
  return CommandResult::Okay();
}

static CommandResult FlashData(pw::flash::Flash& flash,
                               const bootloader::Partition& part,
                               pw::ConstByteSpan image) {
  static constexpr uint32_t kSparseHeaderMagic = 0xed26ff3a;
  const bool is_sparse =
      image.size() >= sizeof(uint32_t) &&
      *reinterpret_cast<const uint32_t*>(image.data()) == kSparseHeaderMagic;

  PW_LOG_INFO("Image type: %s", is_sparse ? "sparse" : "raw");
  if (is_sparse) {
    return FlashSparseImage(flash, part, image);
  } else {
    return FlashRawImage(flash, part, image);
  }
}

CommandResult bootloader::DoFlash(Device* device, std::string name) {
  auto maybe_part = FindPartitionByName(name);
  if (!maybe_part.has_value()) {
    return CommandResult::Failed("partition does not exist");
  }
  auto partition = maybe_part.value();

  auto image = pw::ConstByteSpan{
      reinterpret_cast<std::byte*>(device->download_data().data()),
      device->download_data().size()};

  auto flash = pw::flash::McuxpressoFlash();
  if (!flash.Initialize().ok()) {
    return CommandResult::Failed("flash init failed");
  }

  return FlashData(flash, partition, image);
}
