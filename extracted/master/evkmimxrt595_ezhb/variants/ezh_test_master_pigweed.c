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

static void i3c_master_ibi_callback(I3C_Type *base,
                                    i3c_master_handle_t *handle,
                                    i3c_ibi_type_t ibiType,
                                    i3c_ibi_state_t ibiState);
static void i3c_master_callback(I3C_Type *base, i3c_master_handle_t *handle, status_t status, void *userData);

#define SMARTDMA_FIRMWARE_SIZE 0x2F0
#define SMART_DMA_TRIGGER_CHANNEL 0U
#define SMARTDMA_WAIT_LIMIT 100000000U

extern uint8_t __smartdma_start__[];

volatile bool isDone = false;
volatile bool g_sdmaIrqSeen = false;

static void EZH_Callback(void *param)
{
    isDone = true;
    log_info_line("master: ezh callback\n");
}

static void log_master_registers(const char *prefix)
{
    log_info_line(prefix);
    log_status_info("master: MSTATUS=", EXAMPLE_MASTER->MSTATUS);
    log_status_info("master: MERRWARN=", EXAMPLE_MASTER->MERRWARN);
    log_status_info("master: MDMACTRL=", EXAMPLE_MASTER->MDMACTRL);
    log_status_info("master: MCTRL=", EXAMPLE_MASTER->MCTRL);
    log_status_info("master: SMARTDMA_CTRL=", SMARTDMA->CTRL);
    log_status_info("master: SMARTDMA_BOOTADR=", SMARTDMA->BOOTADR);
    log_status_info("master: SMARTDMA_ARM2EZH=", SMARTDMA->ARM2EZH);
    log_status_info("master: SMARTDMA_EZH2ARM=", SMARTDMA->EZH2ARM);
    log_status_info("master: SMARTDMA_PC=", SMARTDMA->PC);
    log_status_info("master: SMARTDMA_SP=", SMARTDMA->SP);
}

uint32_t ezh_stack[16];

typedef struct _ezh_io_control_param
{
    uint32_t *pStack;
    uint32_t ezhPinMask;
} ezh_io_control_param;

static ezh_io_control_param ezh_io_param;

__attribute__((aligned(4))) uint8_t ezh_data_buffer[33];
__attribute__((aligned(4))) uint8_t ezh_data_buffer_rx[32];

typedef struct _ezh_transfer_param
{
    uint32_t *srcAddr;
    uint32_t dataSize;
    uint32_t *i3cBaseAddress;
} ezh_transfer_param;

static ezh_transfer_param ezh_src_param;

volatile uint32_t testSrcAddress = 0;
volatile uint32_t testSrcNum     = 0;

uint8_t g_master_ibiBuff[10];
i3c_master_handle_t g_i3c_m_handle;
const i3c_master_transfer_callback_t masterCallback = {
    .slave2Master = NULL, .ibiCallback = i3c_master_ibi_callback, .transferComplete = i3c_master_callback};
volatile bool g_masterCompletionFlag = false;
volatile bool g_ibiWonFlag           = false;
volatile status_t g_completionStatus = kStatus_Success;

#define I3C_TIME_OUT_INDEX 100000000U

static void i3c_master_ibi_callback(I3C_Type *base,
                                    i3c_master_handle_t *handle,
                                    i3c_ibi_type_t ibiType,
                                    i3c_ibi_state_t ibiState)
{
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

static void i3c_master_callback(I3C_Type *base, i3c_master_handle_t *handle, status_t status, void *userData)
{
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
    status_t result           = kStatus_Success;
    volatile uint32_t timeout = 0;

    BOARD_InitHardware();
    log_info_line("master: hardware init done\n");

    testSrcAddress = 0x5A;
    testSrcNum     = 0xA5;

    log_info_line("master: start\n");
    PW_LOG_INFO("MCUX SDK version: %s", MCUXSDK_VERSION_FULL_STR);
    PW_LOG_INFO("EZHB Builder");
    log_info_line("master: powering smartdma sram\n");

    POWER_DisablePD(kPDRUNCFG_APD_SMARTDMA_SRAM);
    POWER_DisablePD(kPDRUNCFG_PPD_SMARTDMA_SRAM);
    POWER_ApplyPD();

    for (uint32_t i = 0; i < sizeof(ezh_data_buffer); i++)
    {
        ezh_data_buffer[i] = i + 0x1U;
    }

    memset(ezh_data_buffer_rx, 0, sizeof(ezh_data_buffer_rx));

    keep_smartdma_api_alive();

    RESET_PeripheralReset(kINPUTMUX_RST_SHIFT_RSTn);

    INPUTMUX_Init(INPUTMUX);
    INPUTMUX_AttachSignal(INPUTMUX, SMART_DMA_TRIGGER_CHANNEL, kINPUTMUX_I3c0IrqToSmartDmaInput);
    INPUTMUX_Deinit(INPUTMUX);
    log_info_line("master: inputmux ready\n");

    I3C_MasterGetDefaultConfig(&masterConfig);
    masterConfig.baudRate_Hz.i2cBaud          = EXAMPLE_I2C_BAUDRATE;
    masterConfig.baudRate_Hz.i3cPushPullBaud  = EXAMPLE_I3C_PP_BAUDRATE;
    masterConfig.baudRate_Hz.i3cOpenDrainBaud = EXAMPLE_I3C_OD_BAUDRATE;
    masterConfig.enableOpenDrainStop          = false;
    I3C_MasterInit(EXAMPLE_MASTER, &masterConfig, I3C_MASTER_CLOCK_FREQUENCY);
    log_info_line("master: i3c init done\n");

    I3C_MasterTransferCreateHandle(EXAMPLE_MASTER, &g_i3c_m_handle, &masterCallback, NULL);
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
    result                    = I3C_MasterTransferNonBlocking(EXAMPLE_MASTER, &g_i3c_m_handle, &masterXfer);
    if (kStatus_Success != result)
    {
        log_status_error("master: daa broadcast failed ", (uint32_t)result);
        return result;
    }

    timeout = 0U;
    while ((!g_ibiWonFlag) && (!g_masterCompletionFlag) && (g_completionStatus == kStatus_Success) &&
           (++timeout < I3C_TIME_OUT_INDEX))
    {
        __NOP();
    }

    result = g_completionStatus;
    if ((result != kStatus_Success) || (timeout == I3C_TIME_OUT_INDEX))
    {
        log_info_line("master: daa wait failed\n");
        log_status_error("master: completion status ", (uint32_t)result);
        return -1;
    }
    g_masterCompletionFlag = false;
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
        uint8_t slaveAddr = 0x0U;

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
        (void)slaveAddr;
    }

    log_info_line("master: preparing smartdma transfer\n");

    I3C_MasterClearErrorStatusFlags(EXAMPLE_MASTER,
                                    (uint32_t)kI3C_MasterErrorNackFlag | kI3C_MasterErrorWriteAbortFlag |
                                        kI3C_MasterErrorTermFlag | kI3C_MasterErrorParityFlag |
                                        kI3C_MasterErrorCrcFlag | kI3C_MasterErrorReadFlag |
                                        kI3C_MasterErrorWriteFlag | kI3C_MasterErrorMsgFlag |
                                        kI3C_MasterErrorInvalidReqFlag | kI3C_MasterErrorTimeoutFlag);
    I3C_MasterClearStatusFlags(EXAMPLE_MASTER,
                               (uint32_t)kI3C_MasterSlaveStartFlag | kI3C_MasterControlDoneFlag |
                                   kI3C_MasterCompleteFlag | kI3C_MasterArbitrationWonFlag |
                                   kI3C_MasterSlave2MasterFlag | kI3C_MasterErrorFlag);

    I3C0->MDATACTRL |= I3C_MDATACTRL_FLUSHTB_MASK | I3C_MDATACTRL_FLUSHFB_MASK;

#ifdef EZHB_TX
    I3C0->MDMACTRL = I3C_MDMACTRL_DMAWIDTH(1) | I3C_MDMACTRL_DMATB(2);
#else
    I3C0->MDMACTRL = I3C_MDMACTRL_DMAWIDTH(1) | I3C_MDMACTRL_DMAFB(2);
#endif

    SMARTDMA_Init(SMARTDMA_SRAM_ADDR, __smartdma_start__, SMARTDMA_FIRMWARE_SIZE);
    NVIC_EnableIRQ(SDMA_IRQn);

    NVIC_SetPriority(SDMA_IRQn, 3);
    SMARTDMA_Reset();
    log_info_line("master: smartdma booting\n");

#ifdef EZHB_TX
    ezh_src_param.srcAddr        = (uint32_t *)&ezh_data_buffer[0];
    ezh_src_param.dataSize       = sizeof(ezh_data_buffer) - 1U;
    ezh_src_param.i3cBaseAddress = (uint32_t *)(uintptr_t)I3C0_BASE;
#else
    ezh_src_param.srcAddr        = (uint32_t *)&ezh_data_buffer_rx[0];
    ezh_src_param.dataSize       = sizeof(ezh_data_buffer_rx);
    ezh_src_param.i3cBaseAddress = (uint32_t *)(uintptr_t)I3C0_BASE;
#endif

    SMARTDMA_InstallCallback(EZH_Callback, NULL);
    g_sdmaIrqSeen = false;
    SMARTDMA_Boot(0, &ezh_src_param, 0);
    log_master_registers("master: after smartdma boot\n");

    timeout = 0U;
    while ((!isDone) && (++timeout < SMARTDMA_WAIT_LIMIT))
    {
    }

    if (!isDone)
    {
        log_info_line("master: smartdma timeout\n");
        log_status_error("master: sdma irq seen=", g_sdmaIrqSeen ? 1U : 0U);
        log_master_registers("master: timeout register dump\n");
        return -1;
    }

    log_info_line("master: smartdma transfer complete\n");
    log_status_info("master: sdma irq seen=", g_sdmaIrqSeen ? 1U : 0U);

#ifdef EZHB_TX
    log_info_line("master: tx finish\n");
#else
    log_info_line("master: rx buffer ready\n");
    log_buffer_info("master: rx data\n", ezh_data_buffer_rx, sizeof(ezh_data_buffer_rx));
#endif

    while (1)
    {
        __WFI();
    }
}

void SDMA_IRQHandler(void)
{
    g_sdmaIrqSeen = true;
    log_info_line("master: sdma irq\n");
    SMARTDMA_HandleIRQ();
}