#ifndef TTS_ROOT_BOARD_H_
#define TTS_ROOT_BOARD_H_

#ifndef BOARD_FLASH_RESET_GPIO
#define BOARD_FLASH_RESET_GPIO GPIO
#endif

#ifndef BOARD_FLASH_RESET_GPIO_PORT
#define BOARD_FLASH_RESET_GPIO_PORT 4U
#endif

#ifndef BOARD_FLASH_RESET_GPIO_PIN
#define BOARD_FLASH_RESET_GPIO_PIN 5U
#endif

#ifndef BOARD_FLASH_SIZE
#define BOARD_FLASH_SIZE (0x4000000U)
#endif

#ifndef BOARD_USB_PHY_D_CAL
#define BOARD_USB_PHY_D_CAL (0x0CU)
#endif

#ifndef BOARD_USB_PHY_TXCAL45DP
#define BOARD_USB_PHY_TXCAL45DP (0x06U)
#endif

#ifndef BOARD_USB_PHY_TXCAL45DM
#define BOARD_USB_PHY_TXCAL45DM (0x06U)
#endif

#ifdef __cplusplus
extern "C" {
#endif

void BOARD_InitDebugConsole(void);

#ifdef __cplusplus
}
#endif

#endif  // TTS_ROOT_BOARD_H_