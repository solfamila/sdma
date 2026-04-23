/*
 * Copyright (c) 2013 - 2015, Freescale Semiconductor, Inc.
 * Copyright 2016-2017, 2024 NXP
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "fsl_device_registers.h"
#include "fsl_debug_console.h"
#include "board.h"
#include "app.h"
#include "fsl_i3c_smartdma.h"
#include "fsl_smartdma.h"
#include "fsl_smartdma_fw.h"
#include "fsl_power.h"
#include "fsl_inputmux.h"
#include "fsl_i3c.h"

#define PW_LOG_MODULE_NAME "rt595-master"
#include "pw_log/log.h"

void keep_smartdma_api_alive(void);

static void log_info_line(const char *message)
{
    size_t length = strlen(message);
    while ((length > 0U) && ((message[length - 1U] == '\n') || (message[length - 1U] == '\r')))
    {
        length--;
    }

    if (length == 0U)
    {
        return;
    }

    {
        char buffer[128];
        if (length >= sizeof(buffer))
        {
            length = sizeof(buffer) - 1U;
        }
        memcpy(buffer, message, length);
        buffer[length] = '\0';
        PW_LOG_INFO("%s", buffer);
    }
}

static void log_status_info(const char *label, uint32_t value)
{
    PW_LOG_INFO("%s0x%08" PRIX32, label, value);
}

static void log_status_error(const char *label, uint32_t value)
{
    PW_LOG_ERROR("%s0x%08" PRIX32, label, value);
}

static void log_buffer_info(const char *label, const uint8_t *buffer, uint32_t length)
{
    char line[8U * 5U + 1U];

    log_info_line(label);
    for (uint32_t offset = 0U; offset < length; offset += 8U)
    {
        uint32_t used = 0U;
        uint32_t chunk = length - offset;
        if (chunk > 8U)
        {
            chunk = 8U;
        }

        for (uint32_t index = 0U; index < chunk; index++)
        {
            used += (uint32_t)snprintf(line + used,
                                       sizeof(line) - used,
                                       "0x%02X ",
                                       buffer[offset + index]);
        }

        if (used > 0U)
        {
            line[used - 1U] = '\0';
        }
        else
        {
            line[0] = '\0';
        }

        PW_LOG_INFO("%s", line);
    }
}

static void log_read_irq_metrics(uint32_t readSdmaIrqSeen,
                                 uint32_t readCpuIrqCount,
                                 uint32_t readCpuIrqControlCount,
                                 uint32_t readCpuIrqDataCount,
                                 uint32_t readCpuIrqSuppressedCount,
                                 uint32_t readCpuIrqSuppressedPreDisableNvicCount,
                                 uint32_t readCpuIrqSuppressedPreDisablePendingMask,
                                 uint32_t readCpuIrqSuppressedPreDisableStatusMask)
{
    log_status_info("master: read sdma irq seen=", readSdmaIrqSeen);
    log_status_info("master: read cm33 i3c irq count=", readCpuIrqCount);
    log_status_info("master: read cm33 i3c ctrl irq count=", readCpuIrqControlCount);
    log_status_info("master: read cm33 i3c data irq count=", readCpuIrqDataCount);
    log_status_info("master: read cm33 i3c suppressed irq count=", readCpuIrqSuppressedCount);
    log_status_info("master: read cm33 i3c pre-disable nvic pending count=", readCpuIrqSuppressedPreDisableNvicCount);
    log_status_info("master: read cm33 i3c pre-disable pending mask=", readCpuIrqSuppressedPreDisablePendingMask);
    log_status_info("master: read cm33 i3c pre-disable status mask=", readCpuIrqSuppressedPreDisableStatusMask);
}

static void log_read_post_return_snapshot(uint32_t readPostReturnPendingMask,
                                          uint32_t readPostReturnStatusMask,
                                          uint32_t readPostReturnNvicPending)
{
    log_status_info("master: read post-return pending mask=", readPostReturnPendingMask);
    log_status_info("master: read post-return status mask=", readPostReturnStatusMask);
    log_status_info("master: read post-return nvic pending=", readPostReturnNvicPending);
}

static void i3c_master_ibi_callback(I3C_Type *base,
                                    i3c_master_smartdma_handle_t *handle,
                                    i3c_ibi_type_t ibiType,
                                    i3c_ibi_state_t ibiState);
static void i3c_master_callback(I3C_Type *base,
                                i3c_master_smartdma_handle_t *handle,
                                status_t status,
                                void *userData);

#define SMART_DMA_TRIGGER_CHANNEL 0U
#define EZH_ROUNDTRIP_DATA_LENGTH 255U

extern uint8_t __smartdma_start__[];
extern uint8_t __smartdma_end__[];

volatile bool g_sdmaIrqSeen = false;
__attribute__((aligned(4))) uint8_t ezh_data_buffer[EZH_ROUNDTRIP_DATA_LENGTH];
__attribute__((aligned(4))) uint8_t ezh_data_buffer_rx[EZH_ROUNDTRIP_DATA_LENGTH];

uint8_t g_master_ibiBuff[10];
i3c_master_smartdma_handle_t g_i3c_m_handle;
const i3c_master_smartdma_callback_t masterCallback = {
    .slave2Master = NULL, .ibiCallback = i3c_master_ibi_callback, .transferComplete = i3c_master_callback};
volatile bool g_masterCompletionFlag = false;
volatile bool g_ibiWonFlag           = false;
volatile status_t g_completionStatus = kStatus_Success;

#define I3C_TIME_OUT_INDEX 100000000U

static status_t wait_for_transfer_completion(void)
{
    volatile uint32_t timeout = 0U;

    while ((!g_ibiWonFlag) && (!g_masterCompletionFlag) && (g_completionStatus == kStatus_Success) &&
           (++timeout < I3C_TIME_OUT_INDEX))
    {
        __NOP();
    }

    if (timeout == I3C_TIME_OUT_INDEX)
    {
        return kStatus_Timeout;
    }

    return g_completionStatus;
}

static status_t run_transfer_blocking(i3c_master_transfer_t *transfer)
{
    status_t result;

    g_masterCompletionFlag = false;
    g_ibiWonFlag           = false;
    g_completionStatus     = kStatus_Success;

    result = I3C_MasterTransferSmartDMA(EXAMPLE_MASTER, &g_i3c_m_handle, transfer);
    if (result != kStatus_Success)
    {
        return result;
    }

    result = wait_for_transfer_completion();
    g_masterCompletionFlag = false;
    g_ibiWonFlag           = false;
    return result;
}

static void i3c_master_ibi_callback(I3C_Type *base,
                                    i3c_master_smartdma_handle_t *handle,
                                    i3c_ibi_type_t ibiType,
                                    i3c_ibi_state_t ibiState)
{
    (void)base;

    switch (ibiType)
    {
        case kI3C_IbiNormal:
            if (ibiState == kI3C_IbiDataBuffNeed)
            {
                handle->ibiBuff = g_master_ibiBuff;
            }
            break;

        default:
            assert(false);
            break;
    }
}

static void i3c_master_callback(I3C_Type *base,
                                i3c_master_smartdma_handle_t *handle,
                                status_t status,
                                void *userData)
{
    (void)base;
    (void)handle;
    (void)userData;

    if (status == kStatus_Success)
    {
        g_masterCompletionFlag = true;
        log_info_line("master callback: success\n");
    }

    if (status == kStatus_I3C_IBIWon)
    {
        g_ibiWonFlag = true;
        log_info_line("master callback: ibi won\n");
    }

    if ((status != kStatus_Success) && (status != kStatus_I3C_IBIWon))
    {
        log_status_error("master callback: status ", (uint32_t)status);
    }

    g_completionStatus = status;
}

int main(void)
{
    i3c_master_config_t masterConfig;
    i3c_master_transfer_t masterXfer;
    uint32_t readSdmaIrqSeen = 0U;
    uint32_t readCpuIrqCount = 0U;
    uint32_t readCpuIrqControlCount = 0U;
    uint32_t readCpuIrqDataCount = 0U;
    uint32_t readCpuIrqSuppressedCount = 0U;
    uint32_t readCpuIrqSuppressedPreDisableNvicCount = 0U;
    uint32_t readCpuIrqSuppressedPreDisablePendingMask = 0U;
    uint32_t readCpuIrqSuppressedPreDisableStatusMask = 0U;
    uint32_t readPostReturnPendingMask = 0U;
    uint32_t readPostReturnStatusMask = 0U;
    uint32_t readPostReturnNvicPending = 0U;
    uint8_t slaveAddr = 0U;
    status_t result           = kStatus_Success;

    BOARD_InitHardware();
    log_info_line("master: hardware init done\n");

    log_info_line("master: start\n");
    PW_LOG_INFO("MCUX SDK version: %s", MCUXSDK_VERSION_FULL_STR);
    PW_LOG_INFO("EZHB Builder");
    log_info_line("master: powering smartdma sram\n");

    POWER_DisablePD(kPDRUNCFG_APD_SMARTDMA_SRAM);
    POWER_DisablePD(kPDRUNCFG_PPD_SMARTDMA_SRAM);
    POWER_ApplyPD();

    for (uint32_t index = 0U; index < sizeof(ezh_data_buffer); index++)
    {
        ezh_data_buffer[index] = (uint8_t)(index + 1U);
    }
    memset(ezh_data_buffer_rx, 0, sizeof(ezh_data_buffer_rx));

    keep_smartdma_api_alive();

    RESET_PeripheralReset(kINPUTMUX_RST_SHIFT_RSTn);

    INPUTMUX_Init(INPUTMUX);
    INPUTMUX_AttachSignal(INPUTMUX, SMART_DMA_TRIGGER_CHANNEL, kINPUTMUX_I3c0IrqToSmartDmaInput);
    INPUTMUX_Deinit(INPUTMUX);
    log_info_line("master: inputmux ready\n");

    SMARTDMA_Init(
        SMARTDMA_SRAM_ADDR, __smartdma_start__, (uint32_t)((uintptr_t)__smartdma_end__ - (uintptr_t)__smartdma_start__));
    NVIC_EnableIRQ(SDMA_IRQn);
    NVIC_SetPriority(SDMA_IRQn, 3);
    SMARTDMA_Reset();
    log_info_line("master: smartdma ready\n");

    I3C_MasterGetDefaultConfig(&masterConfig);
    masterConfig.baudRate_Hz.i2cBaud          = EXAMPLE_I2C_BAUDRATE;
    masterConfig.baudRate_Hz.i3cPushPullBaud  = EXAMPLE_I3C_PP_BAUDRATE;
    masterConfig.baudRate_Hz.i3cOpenDrainBaud = EXAMPLE_I3C_OD_BAUDRATE;
    masterConfig.enableOpenDrainStop          = false;
    I3C_MasterInit(EXAMPLE_MASTER, &masterConfig, I3C_MASTER_CLOCK_FREQUENCY);
    log_info_line("master: i3c init done\n");

    I3C_MasterTransferCreateHandleSmartDMA(EXAMPLE_MASTER, &g_i3c_m_handle, &masterCallback, NULL);
    memset(&masterXfer, 0, sizeof(masterXfer));
    log_info_line("master: handle created\n");

    PW_LOG_INFO("master: starting daa broadcast");

    memset(&masterXfer, 0, sizeof(masterXfer));

    masterXfer.slaveAddress   = 0x7EU;
    masterXfer.subaddress     = 0x06U;
    masterXfer.subaddressSize = 1U;
    masterXfer.direction      = kI3C_Write;
    masterXfer.busType        = kI3C_TypeI3CSdr;
    masterXfer.flags          = kI3C_TransferDefaultFlag;
    masterXfer.ibiResponse    = kI3C_IbiRespAckMandatory;
    result                    = run_transfer_blocking(&masterXfer);
    if (kStatus_Success != result)
    {
        log_status_error("master: daa broadcast failed ", (uint32_t)result);
        return result;
    }

    log_info_line("master: daa broadcast complete\n");

    {
        uint8_t addressList[8] = {0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37};
        result                 = I3C_MasterProcessDAA(EXAMPLE_MASTER, addressList, 8);
    }
    if (result != kStatus_Success)
    {
        log_status_error("master: process daa failed ", (uint32_t)result);
        return -1;
    }

    log_info_line("master: daa finished\n");

    {
        uint8_t devCount;
        i3c_device_info_t *devList;

        devList = I3C_MasterGetDeviceListAfterDAA(EXAMPLE_MASTER, &devCount);
        for (uint8_t devIndex = 0; devIndex < devCount; devIndex++)
        {
            if (devList[devIndex].vendorID == 0x123U)
            {
                slaveAddr = devList[devIndex].dynamicAddr;
                break;
            }
        }
        log_status_info("master: discovered slave count=", devCount);
        log_status_info("master: discovered slave addr=", slaveAddr);
    }

    if (slaveAddr == 0U)
    {
        log_info_line("master: target slave not found\n");
        return -1;
    }

    log_info_line("master: preparing smartdma transfer\n");

    memset(&masterXfer, 0, sizeof(masterXfer));
    masterXfer.slaveAddress = slaveAddr;
    masterXfer.direction    = kI3C_Write;
    masterXfer.busType      = kI3C_TypeI3CSdr;
    masterXfer.flags        = kI3C_TransferDefaultFlag;
    masterXfer.ibiResponse  = kI3C_IbiRespAckMandatory;
    masterXfer.data         = ezh_data_buffer;
    masterXfer.dataSize     = sizeof(ezh_data_buffer);

    g_sdmaIrqSeen = false;
    I3C_MasterClearIrqEntryCount(EXAMPLE_MASTER);
    result = run_transfer_blocking(&masterXfer);
    if (result != kStatus_Success)
    {
        log_status_error("master: write transfer failed ", (uint32_t)result);
        return -1;
    }

    log_info_line("master: write transfer complete\n");

    for (volatile uint32_t delay = 0U; delay < WAIT_TIME; delay++)
    {
        __NOP();
    }

    memset(ezh_data_buffer_rx, 0, sizeof(ezh_data_buffer_rx));
    memset(&masterXfer, 0, sizeof(masterXfer));
    masterXfer.slaveAddress = slaveAddr;
    masterXfer.direction    = kI3C_Read;
    masterXfer.busType      = kI3C_TypeI3CSdr;
    masterXfer.flags        = kI3C_TransferDefaultFlag;
    masterXfer.ibiResponse  = kI3C_IbiRespAckMandatory;
    masterXfer.data         = ezh_data_buffer_rx;
    masterXfer.dataSize     = sizeof(ezh_data_buffer_rx);

    g_sdmaIrqSeen = false;
    I3C_MasterClearIrqEntryCount(EXAMPLE_MASTER);
    result = run_transfer_blocking(&masterXfer);
    readSdmaIrqSeen = g_sdmaIrqSeen ? 1U : 0U;
    readCpuIrqCount = I3C_MasterGetIrqEntryCount(EXAMPLE_MASTER);
    readCpuIrqControlCount = I3C_MasterGetControlIrqEntryCount(EXAMPLE_MASTER);
    readCpuIrqDataCount = I3C_MasterGetDataIrqEntryCount(EXAMPLE_MASTER);
    readCpuIrqSuppressedCount = I3C_MasterGetSuppressedIrqCount(EXAMPLE_MASTER);
    readCpuIrqSuppressedPreDisableNvicCount = I3C_MasterGetSuppressedNvicPendingCount(EXAMPLE_MASTER);
    readCpuIrqSuppressedPreDisablePendingMask = I3C_MasterGetSuppressedPendingMask(EXAMPLE_MASTER);
    readCpuIrqSuppressedPreDisableStatusMask = I3C_MasterGetSuppressedStatusMask(EXAMPLE_MASTER);
    readPostReturnPendingMask = I3C_MasterGetPendingInterrupts(EXAMPLE_MASTER);
    readPostReturnStatusMask = EXAMPLE_MASTER->MSTATUS;
    readPostReturnNvicPending = NVIC_GetPendingIRQ(I3C0_IRQn) != 0U ? 1U : 0U;
    if (result != kStatus_Success)
    {
        log_read_irq_metrics(readSdmaIrqSeen,
                             readCpuIrqCount,
                             readCpuIrqControlCount,
                             readCpuIrqDataCount,
                             readCpuIrqSuppressedCount,
                             readCpuIrqSuppressedPreDisableNvicCount,
                             readCpuIrqSuppressedPreDisablePendingMask,
                             readCpuIrqSuppressedPreDisableStatusMask);
        log_read_post_return_snapshot(readPostReturnPendingMask,
                                      readPostReturnStatusMask,
                                      readPostReturnNvicPending);
        log_status_error("master: read transfer failed ", (uint32_t)result);
        return -1;
    }

    log_info_line("master: read transfer complete\n");

    for (uint32_t index = 0U; index < sizeof(ezh_data_buffer); index++)
    {
        if (ezh_data_buffer[index] != ezh_data_buffer_rx[index])
        {
            PW_LOG_ERROR("master: roundtrip mismatch index=%" PRIu32 " expected=0x%02X actual=0x%02X",
                         index,
                         ezh_data_buffer[index],
                         ezh_data_buffer_rx[index]);
            log_buffer_info("master: expected data\n", ezh_data_buffer, sizeof(ezh_data_buffer));
            log_buffer_info("master: rx data\n", ezh_data_buffer_rx, sizeof(ezh_data_buffer_rx));
            while (1)
            {
                __WFI();
            }
        }
    }

    log_info_line("master: roundtrip compare success\n");
    log_read_irq_metrics(readSdmaIrqSeen,
                         readCpuIrqCount,
                         readCpuIrqControlCount,
                         readCpuIrqDataCount,
                         readCpuIrqSuppressedCount,
                         readCpuIrqSuppressedPreDisableNvicCount,
                         readCpuIrqSuppressedPreDisablePendingMask,
                         readCpuIrqSuppressedPreDisableStatusMask);
    log_read_post_return_snapshot(readPostReturnPendingMask,
                                  readPostReturnStatusMask,
                                  readPostReturnNvicPending);

    while (1)
    {
        __WFI();
    }
}

void SDMA_IRQHandler(void)
{
    g_sdmaIrqSeen = true;
    SMARTDMA_HandleIRQ();
}