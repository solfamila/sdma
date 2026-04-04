/*
 * Copyright (c) 2013 - 2015, Freescale Semiconductor, Inc.
 * Copyright 2016-2017, 2024 NXP
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdint.h>
#include "fsl_device_registers.h"
#include "board.h"
#include "app.h"
#include "fsl_common.h"
#include "mcuxsdk_version.h"

#define HELLO_DELAY_US 1000000U

static void semihost_write0(const char *message)
{
    register uint32_t operation asm("r0") = 0x04U;
    register const char *parameter asm("r1") = message;

    __asm volatile (
        "bkpt 0xAB"
        : "+r" (operation)
        : "r" (parameter)
        : "memory");
}

static void semihost_write_counter(uint32_t counter)
{
    char message[32];
    char digits[10];
    uint32_t value = counter;
    uint32_t index = 0;
    uint32_t digit_count;

    message[index++] = 'h';
    message[index++] = 'e';
    message[index++] = 'l';
    message[index++] = 'l';
    message[index++] = 'o';
    message[index++] = ' ';

    if (value == 0U)
    {
        digits[0] = '0';
        digit_count = 1U;
    }
    else
    {
        digit_count = 0U;
        while (value > 0U)
        {
            digits[digit_count++] = (char)('0' + (value % 10U));
            value /= 10U;
        }
    }

    while (digit_count > 0U)
    {
        message[index++] = digits[--digit_count];
    }

    message[index++] = '\n';
    message[index] = '\0';
    semihost_write0(message);
}

int main(void)
{
    uint32_t counter = 0;

    BOARD_InitHardware();

    semihost_write0("RT595 hello world via semihosting\n");
    semihost_write0("MCUX SDK version: " MCUXSDK_VERSION_FULL_STR "\n");

    while (1)
    {
        semihost_write_counter(counter);
        counter++;
        SDK_DelayAtLeastUs(HELLO_DELAY_US, SystemCoreClock);
    }
}

