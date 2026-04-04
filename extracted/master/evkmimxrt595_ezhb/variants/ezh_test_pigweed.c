/*
 * RT595 Pigweed logging demo adapted for the manual GCC build.
 */

#include <stdint.h>

#include "board.h"
#include "app.h"
#include "fsl_common.h"
#include "fsl_device_registers.h"
#include "mcuxsdk_version.h"

#define PW_LOG_MODULE_NAME "rt595"
#include "pw_log/log.h"

#define HELLO_DELAY_US 1000000U

int main(void)
{
    uint32_t counter = 0U;

    BOARD_InitHardware();

    PW_LOG_INFO("Pigweed logging initialized on MIMXRT595-EVK");
    PW_LOG_INFO("MCUX SDK version: %s", MCUXSDK_VERSION_FULL_STR);
    PW_LOG_INFO("Using semihost-backed pw_log backend instead of FLEXCOMM12");

    while (1)
    {
        PW_LOG_INFO("hello %lu", (unsigned long)counter);
        counter++;
        SDK_DelayAtLeastUs(HELLO_DELAY_US, SystemCoreClock);
    }
}