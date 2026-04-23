#include <cstdio>
#include <memory>
#include <string>

#include "FreeRTOS.h"
#include "bootloader.h"
#include "config/board.h"
#include "flash.h"
#include "fsl_debug_console.h"
#include "pw_fastboot/commands.h"
#include "pw_fastboot/device_hal.h"
#include "pw_fastboot/device_variable.h"
#include "pw_fastboot/fastboot_device.h"
#include "pw_fastboot_usb_mcuxpresso/transport.h"
#include "pw_log/log.h"
#include "pw_status/status.h"
#include "task.h"

// Enable entering of fastboot using SW1
#define FASTBOOT_ENABLE_GPIO (1)
#define USER_APP_VTOR (FASTBOOT_FLASH_BASE + FASTBOOT_APP_VECTOR_TABLE)

#if defined(FASTBOOT_ENABLE_GPIO) && FASTBOOT_ENABLE_GPIO > 0

#include "config/pin_mux.h"

static bool IsFastbootTriggered() {
  const gpio_pin_config_t button_config = {kGPIO_DigitalInput, 0U};

  BOARD_InitBUTTONPins();
  GPIO_PinInit(
      BOARD_SW1_GPIO, BOARD_SW1_GPIO_PORT, BOARD_SW1_GPIO_PIN, &button_config);
  return GPIO_PinRead(BOARD_SW1_GPIO, BOARD_SW1_GPIO_PORT, BOARD_SW1_GPIO_PIN) == 0U;
}

#endif

struct UserVectorTable {
  std::size_t msp;
  std::size_t reset;
  std::size_t nmi;
  std::size_t hardfault;
  std::size_t mpufault;
  std::size_t busfault;
  std::size_t usagefault;
  std::size_t securefault;
};

static void CleanUpAfterBootloader();

static bool UserAppPresent() {
  auto* vector_table = reinterpret_cast<const UserVectorTable*>(USER_APP_VTOR);
  if (vector_table->msp == 0xFFFFFFFFu || vector_table->reset == 0xFFFFFFFFu) {
    return false;
  }

  return vector_table->msp >= 0x20280000u && vector_table->msp < 0x20500000u;
}

[[noreturn]] static void JumpToUserApplication() {
  auto* vector_table = reinterpret_cast<const UserVectorTable*>(USER_APP_VTOR);

  CleanUpAfterBootloader();
  SCB->VTOR = USER_APP_VTOR;
  __DSB();
  __ISB();
  __set_MSP(static_cast<uint32_t>(vector_table->msp));

  auto reset_handler = reinterpret_cast<void (*)(void)>(vector_table->reset);
  reset_handler();

  while (true) {
  }
}

class BootloaderHal : public pw::fastboot::DeviceHAL {
 public:
  constexpr BootloaderHal() = default;

  pw::fastboot::CommandResult Flash(pw::fastboot::Device* device,
                                    std::string name) override {
    return bootloader::DoFlash(device, name);
  }

  pw::fastboot::CommandResult Reboot(pw::fastboot::Device*,
                                     pw::fastboot::RebootType) override {
    return pw::fastboot::CommandResult::Failed("Command unimplemented!");
  }

  pw::fastboot::CommandResult ShutDown(pw::fastboot::Device*) override {
    return pw::fastboot::CommandResult::Failed("Command unimplemented!");
  }

  pw::fastboot::CommandResult OemCommand(pw::fastboot::Device*,
                                         std::string /*command*/) override {
    return pw::fastboot::CommandResult::Failed("Command unimplemented!");
  }

  bool IsDeviceLocked(pw::fastboot::Device*) override { return false; }

 private:
};

static void FastbootProtocolLoop() {
  auto variables = std::make_unique<pw::fastboot::VariableProvider>();
  variables->RegisterVariable("product",
                              [](auto, auto, std::string* message) -> bool {
                                *message = "mimxrt595-master";
                                return true;
                              });
  variables->RegisterVariable("version-bootloader",
                              [](auto, auto, std::string* message) -> bool {
                                *message = "tts-fastboot";
                                return true;
                              });
  pw::fastboot::Device device{pw::fastboot::CreateMimxrt595UsbTransport(),
                              std::move(variables),
                              std::make_unique<BootloaderHal>()};
  PW_LOG_INFO("Ready to accept fastboot commands, connect to USB port J38..");
  device.ExecuteCommands();
}

static void FastbootTask(void*) {
  FastbootProtocolLoop();
  vTaskDelete(nullptr);
}

static void CleanUpAfterBootloader() {
  // Deinitialize the debug console before interrupts are disabled so UART
  // state does not leak into the user application.
  (void)DbgConsole_Deinit();

  // Disable interrupts
  asm volatile("cpsid i");

  // Disable SYSTICK timer
  // FreeRTOS support enables the SYSTICK, and its configuration will carry
  // over to the user application, which will cause faults if it did not expect
  // an interrupt to occur.
  SysTick->CTRL = 0x0;

  // Put all peripherals into reset before jumping to the user application.
  // Enumeration values were taken directly from _RSTCTL_RSTn enum in
  // `fsl_reset.h`.
  // FlexSPI0 is excluded, as it must be initialized (the application is
  // executed in place from the flash).
  constexpr RSTCTL_RSTn_t PERIPHERALS_TO_RESET[] = {
      kDSP_RST_SHIFT_RSTn,           kAXI_SWITCH_RST_SHIFT_RSTn,
      kPOWERQUAD_RST_SHIFT_RSTn,     kCASPER_RST_SHIFT_RSTn,
      kHASHCRYPT_RST_SHIFT_RSTn,     kPUF_RST_SHIFT_RSTn,
      kRNG_RST_SHIFT_RSTn, /*kFLEXSPI0_RST_SHIFT_RSTn,*/
      kFLEXSPI1_RST_SHIFT_RSTn,      kUSBHS_PHY_RST_SHIFT_RSTn,
      kUSBHS_DEVICE_RST_SHIFT_RSTn,  kUSBHS_HOST_RST_SHIFT_RSTn,
      kUSBHS_SRAM_RST_SHIFT_RSTn,    kSCT_RST_SHIFT_RSTn,
      kGPU_RST_SHIFT_RSTn,           kDISP_CTRL_RST_SHIFT_RSTn,
      kMIPI_DSI_CTRL_RST_SHIFT_RSTn, kMIPI_DSI_PHY_RST_SHIFT_RSTn,
      kSMART_DMA_RST_SHIFT_RSTn,     kSDIO0_RST_SHIFT_RSTn,
      kSDIO1_RST_SHIFT_RSTn,         kACMP0_RST_SHIFT_RSTn,
      kADC0_RST_SHIFT_RSTn,          kSHSGPIO0_RST_SHIFT_RSTn,
      kUTICK0_RST_SHIFT_RSTn,        kWWDT0_RST_SHIFT_RSTn,
      kFC0_RST_SHIFT_RSTn,           kFC1_RST_SHIFT_RSTn,
      kFC2_RST_SHIFT_RSTn,           kFC3_RST_SHIFT_RSTn,
      kFC4_RST_SHIFT_RSTn,           kFC5_RST_SHIFT_RSTn,
      kFC6_RST_SHIFT_RSTn,           kFC7_RST_SHIFT_RSTn,
      kFC8_RST_SHIFT_RSTn,           kFC9_RST_SHIFT_RSTn,
      kFC10_RST_SHIFT_RSTn,          kFC11_RST_SHIFT_RSTn,
      kFC12_RST_SHIFT_RSTn,          kFC13_RST_SHIFT_RSTn,
      kFC14_RST_SHIFT_RSTn,          kFC15_RST_SHIFT_RSTn,
      kDMIC_RST_SHIFT_RSTn,          kFC16_RST_SHIFT_RSTn,
      kOSEVENT_TIMER_RST_SHIFT_RSTn, kFLEXIO_RST_SHIFT_RSTn,
      kHSGPIO0_RST_SHIFT_RSTn,       kHSGPIO1_RST_SHIFT_RSTn,
      kHSGPIO2_RST_SHIFT_RSTn,       kHSGPIO3_RST_SHIFT_RSTn,
      kHSGPIO4_RST_SHIFT_RSTn,       kHSGPIO5_RST_SHIFT_RSTn,
      kHSGPIO6_RST_SHIFT_RSTn,       kHSGPIO7_RST_SHIFT_RSTn,
      kCRC_RST_SHIFT_RSTn,           kDMAC0_RST_SHIFT_RSTn,
      kDMAC1_RST_SHIFT_RSTn,         kMU_RST_SHIFT_RSTn,
      kSEMA_RST_SHIFT_RSTn,          kFREQME_RST_SHIFT_RSTn,
      kCT32B0_RST_SHIFT_RSTn,        kCT32B1_RST_SHIFT_RSTn,
      kCT32B2_RST_SHIFT_RSTn,        kCT32B3_RST_SHIFT_RSTn,
      kCT32B4_RST_SHIFT_RSTn,        kMRT0_RST_SHIFT_RSTn,
      kWWDT1_RST_SHIFT_RSTn,         kI3C0_RST_SHIFT_RSTn,
      kI3C1_RST_SHIFT_RSTn,          kPINT_RST_SHIFT_RSTn,
      kINPUTMUX_RST_SHIFT_RSTn,
  };
  for (auto peri : PERIPHERALS_TO_RESET) {
    RESET_SetPeripheralReset(peri);
  }
}

namespace bootloader {

void StartBootFlow() {
#if defined(FASTBOOT_ENABLE_GPIO) && FASTBOOT_ENABLE_GPIO > 0
  if (!BOOTDATA->valid()) {
    const auto fastboot_gpio_triggered = IsFastbootTriggered();
    BOOTDATA->set(fastboot_gpio_triggered ? BootMode::Fastboot
                                          : BootMode::User);
  }
#endif

  auto boot_mode =
      BOOTDATA->valid() ? BOOTDATA->boot_mode : BootMode::User;
  if (boot_mode == BootMode::User && !UserAppPresent()) {
    PW_LOG_WARN("No application found in fastboot system slot, staying in fastboot mode");
    boot_mode = BootMode::Fastboot;
  }

  BOOTDATA->clear();
  PW_LOG_INFO("Fastboot bootloader: %s mode",
              boot_mode == BootMode::User ? "application" : "fastboot");

  switch (boot_mode) {
    default:
    case BootMode::Fastboot: {
      constexpr uint16_t kFastbootTaskStackWords = 4096;
      constexpr UBaseType_t kFastbootTaskPriority = configMAX_PRIORITIES - 1;
      if (xTaskCreate(FastbootTask,
                      "fastboot",
                      kFastbootTaskStackWords,
                      nullptr,
                      kFastbootTaskPriority,
                      nullptr) != pdPASS) {
        PW_LOG_ERROR("Failed to create fastboot task");
        while (true) {
        }
      }
      return;
    }
    case BootMode::User: {
      JumpToUserApplication();
    }
  }
}

}  // namespace bootloader
