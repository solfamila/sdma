/*
 * Copyright (c) 2013 - 2015, Freescale Semiconductor, Inc.
 * Copyright 2016-2017, 2024 NXP
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "fsl_device_registers.h"
#include "fsl_debug_console.h"
#include "board.h"
#include "app.h"
#include "fsl_smartdma.h"
#include "fsl_smartdma_fw.h"
#include "fsl_power.h"
#include "fsl_inputmux.h"
#include "fsl_i3c.h"

void keep_smartdma_api_alive(void);

static void semihost_write0(const char *message)
{
    register uint32_t operation asm("r0") = 0x04U;
    register const char *parameter asm("r1") = message;

    __asm volatile(
        "bkpt 0xAB"
        : "+r"(operation)
        : "r"(parameter)
        : "memory");
}

static void semihost_write_hex32(uint32_t value)
{
    static const char hex_digits[] = "0123456789ABCDEF";
    char message[] = "0x00000000\n";

    for (uint32_t index = 0; index < 8U; index++)
    {
        uint32_t shift = (7U - index) * 4U;
        message[2U + index] = hex_digits[(value >> shift) & 0xFU];
    }

    semihost_write0(message);
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
    semihost_write0("master: ezh callback\n");
}

static void semihost_write_label_hex32(const char *label, uint32_t value)
{
    semihost_write0(label);
    semihost_write_hex32(value);
}

static void semihost_dump_buffer(const char *label, const uint8_t *buffer, uint32_t length)
{
    static const char hex_digits[] = "0123456789ABCDEF";
    char line[] = "0x00 ";

    semihost_write0(label);
    for (uint32_t index = 0; index < length; index++)
    {
        line[2] = hex_digits[(buffer[index] >> 4) & 0xFU];
        line[3] = hex_digits[buffer[index] & 0xFU];
        semihost_write0(line);
        if ((index % 8U) == 7U)
        {
            semihost_write0("\n");
        }
    }
    if ((length % 8U) != 0U)
    {
        semihost_write0("\n");
    }
}

static void semihost_dump_master_registers(const char *prefix)
{
    semihost_write0(prefix);
    semihost_write_label_hex32("master: MSTATUS=", EXAMPLE_MASTER->MSTATUS);
    semihost_write_label_hex32("master: MERRWARN=", EXAMPLE_MASTER->MERRWARN);
    semihost_write_label_hex32("master: MDMACTRL=", EXAMPLE_MASTER->MDMACTRL);
    semihost_write_label_hex32("master: MCTRL=", EXAMPLE_MASTER->MCTRL);
    semihost_write_label_hex32("master: SMARTDMA_CTRL=", SMARTDMA->CTRL);
    semihost_write_label_hex32("master: SMARTDMA_BOOTADR=", SMARTDMA->BOOTADR);
    semihost_write_label_hex32("master: SMARTDMA_ARM2EZH=", SMARTDMA->ARM2EZH);
    semihost_write_label_hex32("master: SMARTDMA_EZH2ARM=", SMARTDMA->EZH2ARM);
    semihost_write_label_hex32("master: SMARTDMA_PC=", SMARTDMA->PC);
    semihost_write_label_hex32("master: SMARTDMA_SP=", SMARTDMA->SP);
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
        semihost_write0("master callback: success\n");
    }

    if (status == kStatus_I3C_IBIWon)
    {
        g_ibiWonFlag = true;
        semihost_write0("master callback: ibi won\n");
    }

    if ((status != kStatus_Success) && (status != kStatus_I3C_IBIWon))
    {
        semihost_write0("master callback: status ");
        semihost_write_hex32((uint32_t)status);
    }

    g_completionStatus = status;
}

int main(void)
{
    i3c_master_config_t masterConfig;
    i3c_master_transfer_t masterXfer;
    status_t result          = kStatus_Success;
    volatile uint32_t timeout = 0;

    BOARD_InitHardware();
    semihost_write0("master: hardware init done\n");

    testSrcAddress = 0x5A;
    testSrcNum     = 0xA5;

    semihost_write0("master: start\n");
    PRINTF("MCUX SDK version: %s\r\n", MCUXSDK_VERSION_FULL_STR);
    PRINTF("EZHB Builder.\r\n");
    semihost_write0("master: powering smartdma sram\n");

    POWER_DisablePD(kPDRUNCFG_APD_SMARTDMA_SRAM);
    POWER_DisablePD(kPDRUNCFG_PPD_SMARTDMA_SRAM);
    POWER_ApplyPD();

    for (uint32_t i = 0; i < sizeof(ezh_data_buffer); i++)
    {
        ezh_data_buffer[i] = i + 0x1;
    }

    memset(ezh_data_buffer_rx, 0, sizeof(ezh_data_buffer_rx));

    keep_smartdma_api_alive();

    RESET_PeripheralReset(kINPUTMUX_RST_SHIFT_RSTn);

    INPUTMUX_Init(INPUTMUX);
    INPUTMUX_AttachSignal(INPUTMUX, SMART_DMA_TRIGGER_CHANNEL, kINPUTMUX_I3c0IrqToSmartDmaInput);
    INPUTMUX_Deinit(INPUTMUX);
    semihost_write0("master: inputmux ready\n");

    I3C_MasterGetDefaultConfig(&masterConfig);
    masterConfig.baudRate_Hz.i2cBaud          = EXAMPLE_I2C_BAUDRATE;
    masterConfig.baudRate_Hz.i3cPushPullBaud  = EXAMPLE_I3C_PP_BAUDRATE;
    masterConfig.baudRate_Hz.i3cOpenDrainBaud = EXAMPLE_I3C_OD_BAUDRATE;
    masterConfig.enableOpenDrainStop          = false;
    I3C_MasterInit(EXAMPLE_MASTER, &masterConfig, I3C_MASTER_CLOCK_FREQUENCY);
    semihost_write0("master: i3c init done\n");

    I3C_MasterTransferCreateHandle(EXAMPLE_MASTER, &g_i3c_m_handle, &masterCallback, NULL);
    memset(&masterXfer, 0, sizeof(masterXfer));
    semihost_write0("master: handle created\n");

    PRINTF("\r\nI3C master do dynamic address assignment to the I3C slaves on bus.");
    semihost_write0("master: starting daa broadcast\n");

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
        semihost_write0("master: daa broadcast failed ");
        semihost_write_hex32((uint32_t)result);
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
        semihost_write0("master: daa wait failed\n");
        semihost_write0("master: completion status ");
        semihost_write_hex32((uint32_t)result);
        return -1;
    }
    g_masterCompletionFlag = false;
    semihost_write0("master: daa broadcast complete\n");

    {
        uint8_t addressList[8] = {0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37};
        result                 = I3C_MasterProcessDAA(EXAMPLE_MASTER, addressList, 8);
    }
    if (result != kStatus_Success)
    {
        semihost_write0("master: process daa failed ");
        semihost_write_hex32((uint32_t)result);
        return -1;
    }

    PRINTF("\r\nI3C master dynamic address assignment done.\r\n");
    semihost_write0("master: daa finished\n");

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
        semihost_write_label_hex32("master: discovered slave count=", devCount);
        semihost_write_label_hex32("master: discovered slave addr=", slaveAddr);
        (void)slaveAddr;
    }

    PRINTF("\r\nStart to do I3C master transfer in I3C SDR mode.");
    semihost_write0("master: preparing smartdma transfer\n");

    {
        uint32_t status   = EXAMPLE_MASTER->MSTATUS;
        uint32_t merrwarn = EXAMPLE_MASTER->MERRWARN;
        PRINTF("\r\n status = %x\r\n", status);
        PRINTF("\r\n merrwarn = %x\r\n", merrwarn);
    }

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
    semihost_write0("master: smartdma booting\n");

#ifdef EZHB_TX
    ezh_src_param.srcAddr        = (uint32_t *)&ezh_data_buffer[0];
    ezh_src_param.dataSize       = sizeof(ezh_data_buffer) - 1;
    ezh_src_param.i3cBaseAddress = (uint32_t *)(uintptr_t)I3C0_BASE;
#else
    ezh_src_param.srcAddr        = (uint32_t *)&ezh_data_buffer_rx[0];
    ezh_src_param.dataSize       = sizeof(ezh_data_buffer_rx);
    ezh_src_param.i3cBaseAddress = (uint32_t *)(uintptr_t)I3C0_BASE;
#endif

    SMARTDMA_InstallCallback(EZH_Callback, NULL);
    g_sdmaIrqSeen = false;
    SMARTDMA_Boot(0, &ezh_src_param, 0);
    semihost_dump_master_registers("master: after smartdma boot\n");

    timeout = 0U;
    while ((!isDone) && (++timeout < SMARTDMA_WAIT_LIMIT))
    {
    }

    if (!isDone)
    {
        semihost_write0("master: smartdma timeout\n");
        semihost_write_label_hex32("master: sdma irq seen=", g_sdmaIrqSeen ? 1U : 0U);
        semihost_dump_master_registers("master: timeout register dump\n");
        return -1;
    }

    semihost_write0("master: smartdma transfer complete\n");
    semihost_write_label_hex32("master: sdma irq seen=", g_sdmaIrqSeen ? 1U : 0U);

    {
        uint32_t status   = EXAMPLE_MASTER->MSTATUS;
        uint32_t merrwarn = EXAMPLE_MASTER->MERRWARN;
        PRINTF("\r\nstatus = %x\r\n", status);
        PRINTF("\r\nmerrwarn = %x\r\n", merrwarn);
    }

#ifdef EZHB_TX
    PRINTF("Finish\r\n");
    semihost_write0("master: tx finish\n");
#else
    PRINTF("\r\nThe master reveice data from slave:\r\n");
    semihost_write0("master: rx buffer ready\n");
    semihost_dump_buffer("master: rx data\n", ezh_data_buffer_rx, sizeof(ezh_data_buffer_rx));

    for (uint32_t i = 0; i < sizeof(ezh_data_buffer_rx); i++)
    {
        if (i % 8 == 0)
        {
            PRINTF("\r\n");
        }
        PRINTF("0x%2x  ", ezh_data_buffer_rx[i]);
    }
#endif

    while (1)
    {
        __WFI();
    }
}

void SDMA_IRQHandler(void)
{
    g_sdmaIrqSeen = true;
    semihost_write0("master: sdma irq\n");
    SMARTDMA_HandleIRQ();
}