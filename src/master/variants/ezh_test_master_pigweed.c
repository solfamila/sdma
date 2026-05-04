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
#include "fsl_clock.h"
#include "fsl_inputmux.h"
#include "fsl_i3c.h"

#define PW_LOG_MODULE_NAME "rt595-master"
#include "pw_log/log.h"

#ifndef APP_LINKSERVER_TIMING_MEASURE
#define APP_LINKSERVER_TIMING_MEASURE 0
#endif

#define APP_MEASURE_NO_MISMATCH_INDEX UINT32_MAX
#define MASTER_MEASURE_RETAINED_MAGIC 0x4D545453U
#define MASTER_MEASURE_RETAINED_VERSION 1U

typedef struct master_measure_retained
{
    uint32_t magic;
    uint32_t version;
    uint32_t cycles;
    uint32_t usec;
    uint32_t status;
    uint32_t mismatchIndex;
    uint32_t sdmaIrqSeen;
    uint32_t tx0;
    uint32_t tx1;
    uint32_t tx2;
    uint32_t tx3;
    uint32_t rx0;
    uint32_t rx1;
    uint32_t rx2;
    uint32_t rx3;
    uint32_t tx254;
    uint32_t rx254;
} master_measure_retained_t;

void keep_smartdma_api_alive(void);

static void log_info_line(const char *message)
{
#if APP_LINKSERVER_TIMING_MEASURE
    (void)message;
    return;
#endif

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
#if APP_LINKSERVER_TIMING_MEASURE
    (void)label;
    (void)value;
    return;
#endif

    PW_LOG_INFO("%s0x%08" PRIX32, label, value);
}

static void log_status_error(const char *label, uint32_t value)
{
#if APP_LINKSERVER_TIMING_MEASURE
    (void)label;
    (void)value;
    return;
#endif

    PW_LOG_ERROR("%s0x%08" PRIX32, label, value);
}

static void log_buffer_info(const char *label, const uint8_t *buffer, uint32_t length)
{
#if APP_LINKSERVER_TIMING_MEASURE
    (void)label;
    (void)buffer;
    (void)length;
    return;
#endif

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

static void log_irq_metrics(const char *phase,
                            uint32_t sdmaIrqSeen,
                            uint32_t cpuIrqCount,
                            uint32_t cpuIrqControlCount,
                            uint32_t cpuIrqDataCount,
                            uint32_t cpuIrqSuppressedCount,
                            uint32_t cpuIrqSuppressedPreDisableNvicCount,
                            uint32_t cpuIrqSuppressedPreDisablePendingMask,
                            uint32_t cpuIrqSuppressedPreDisableStatusMask)
{
#if APP_LINKSERVER_TIMING_MEASURE
    (void)phase;
    (void)sdmaIrqSeen;
    (void)cpuIrqCount;
    (void)cpuIrqControlCount;
    (void)cpuIrqDataCount;
    (void)cpuIrqSuppressedCount;
    (void)cpuIrqSuppressedPreDisableNvicCount;
    (void)cpuIrqSuppressedPreDisablePendingMask;
    (void)cpuIrqSuppressedPreDisableStatusMask;
    return;
#endif

    char label[96];

    (void)snprintf(label, sizeof(label), "master: %s sdma irq seen=", phase);
    log_status_info(label, sdmaIrqSeen);
    (void)snprintf(label, sizeof(label), "master: %s cm33 i3c irq count=", phase);
    log_status_info(label, cpuIrqCount);
    (void)snprintf(label, sizeof(label), "master: %s cm33 i3c ctrl irq count=", phase);
    log_status_info(label, cpuIrqControlCount);
    (void)snprintf(label, sizeof(label), "master: %s cm33 i3c data irq count=", phase);
    log_status_info(label, cpuIrqDataCount);
    (void)snprintf(label, sizeof(label), "master: %s cm33 i3c suppressed irq count=", phase);
    log_status_info(label, cpuIrqSuppressedCount);
    (void)snprintf(label, sizeof(label), "master: %s cm33 i3c pre-disable nvic pending count=", phase);
    log_status_info(label, cpuIrqSuppressedPreDisableNvicCount);
    (void)snprintf(label, sizeof(label), "master: %s cm33 i3c pre-disable pending mask=", phase);
    log_status_info(label, cpuIrqSuppressedPreDisablePendingMask);
    (void)snprintf(label, sizeof(label), "master: %s cm33 i3c pre-disable status mask=", phase);
    log_status_info(label, cpuIrqSuppressedPreDisableStatusMask);
}

static void log_post_return_snapshot(const char *phase,
                                     uint32_t postReturnPendingMask,
                                     uint32_t postReturnStatusMask,
                                     uint32_t postReturnNvicPending)
{
#if APP_LINKSERVER_TIMING_MEASURE
    (void)phase;
    (void)postReturnPendingMask;
    (void)postReturnStatusMask;
    (void)postReturnNvicPending;
    return;
#endif

    char label[80];

    (void)snprintf(label, sizeof(label), "master: %s post-return pending mask=", phase);
    log_status_info(label, postReturnPendingMask);
    (void)snprintf(label, sizeof(label), "master: %s post-return status mask=", phase);
    log_status_info(label, postReturnStatusMask);
    (void)snprintf(label, sizeof(label), "master: %s post-return nvic pending=", phase);
    log_status_info(label, postReturnNvicPending);
}

static void log_smartdma_window_metrics(const char *phase, const i3c_master_smartdma_handle_t *handle)
{
#if APP_LINKSERVER_TIMING_MEASURE
    (void)phase;
    (void)handle;
    return;
#endif

    char label[96];

    (void)snprintf(label, sizeof(label), "master: %s smartdma active-window irq count=", phase);
    log_status_info(label, handle->smartdmaWindowIrqCount);
    (void)snprintf(label, sizeof(label), "master: %s smartdma fifo bounce count=", phase);
    log_status_info(label, handle->smartdmaFifoReadyBounceCount);
    (void)snprintf(label, sizeof(label), "master: %s smartdma protocol bounce count=", phase);
    log_status_info(label, handle->smartdmaProtocolBounceCount);
    (void)snprintf(label, sizeof(label), "master: %s smartdma mailbox protocol count=", phase);
    log_status_info(label, handle->smartdmaMailboxProtocolCount);
    (void)snprintf(label, sizeof(label), "master: %s smartdma pending mask=", phase);
    log_status_info(label, handle->smartdmaWindowPendingMask);
    (void)snprintf(label, sizeof(label), "master: %s smartdma fifo mask=", phase);
    log_status_info(label, handle->smartdmaWindowFifoMask);
    (void)snprintf(label, sizeof(label), "master: %s smartdma protocol mask=", phase);
    log_status_info(label, handle->smartdmaWindowProtocolMask);
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
__attribute__((section(".usb_ram"), used, aligned(4))) volatile master_measure_retained_t g_measureRetained;

uint8_t g_master_ibiBuff[10];
i3c_master_smartdma_handle_t g_i3c_m_handle;
const i3c_master_smartdma_callback_t masterCallback = {
    .slave2Master = NULL, .ibiCallback = i3c_master_ibi_callback, .transferComplete = i3c_master_callback};
volatile bool g_masterCompletionFlag = false;
volatile bool g_ibiWonFlag           = false;
volatile status_t g_completionStatus = kStatus_Success;

extern uint32_t SystemCoreClock;

static void master_enable_retained_measurement_ram(void)
{
#if APP_LINKSERVER_TIMING_MEASURE
    POWER_DisablePD(kPDRUNCFG_APD_USBHS_SRAM);
    POWER_DisablePD(kPDRUNCFG_PPD_USBHS_SRAM);
    POWER_ApplyPD();

    RESET_PeripheralReset(kUSBHS_SRAM_RST_SHIFT_RSTn);
    CLOCK_EnableClock(kCLOCK_UsbhsSram);
#endif
}

static void master_reset_retained_measurement(void)
{
#if APP_LINKSERVER_TIMING_MEASURE
    volatile uint32_t *words = (volatile uint32_t *)&g_measureRetained;

    for (uint32_t index = 0U; index < (sizeof(g_measureRetained) / sizeof(uint32_t)); index++)
    {
        words[index] = 0U;
    }

    g_measureRetained.magic = MASTER_MEASURE_RETAINED_MAGIC;
    g_measureRetained.version = MASTER_MEASURE_RETAINED_VERSION;
    g_measureRetained.mismatchIndex = APP_MEASURE_NO_MISMATCH_INDEX;
#endif
}

static void init_cycle_counter(void)
{
#if APP_LINKSERVER_TIMING_MEASURE
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0U;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
#endif
}

static uint32_t read_cycle_counter(void)
{
#if APP_LINKSERVER_TIMING_MEASURE
    return DWT->CYCCNT;
#else
    return 0U;
#endif
}

static void log_timing_summary(status_t result, uint32_t cycles, uint32_t mismatchIndex)
{
#if APP_LINKSERVER_TIMING_MEASURE
    uint32_t usec = 0U;

    if (SystemCoreClock != 0U)
    {
        usec = (uint32_t)(((uint64_t)cycles * 1000000ULL) / SystemCoreClock);
    }

    g_measureRetained.cycles = cycles;
    g_measureRetained.usec = usec;
    g_measureRetained.status = (uint32_t)result;
    g_measureRetained.mismatchIndex = mismatchIndex;
    g_measureRetained.sdmaIrqSeen = g_sdmaIrqSeen ? 1U : 0U;
    g_measureRetained.tx0 = ezh_data_buffer[0];
    g_measureRetained.tx1 = ezh_data_buffer[1];
    g_measureRetained.tx2 = ezh_data_buffer[2];
    g_measureRetained.tx3 = ezh_data_buffer[3];
    g_measureRetained.rx0 = ezh_data_buffer_rx[0];
    g_measureRetained.rx1 = ezh_data_buffer_rx[1];
    g_measureRetained.rx2 = ezh_data_buffer_rx[2];
    g_measureRetained.rx3 = ezh_data_buffer_rx[3];
    g_measureRetained.tx254 = ezh_data_buffer[254];
    g_measureRetained.rx254 = ezh_data_buffer_rx[254];

    PW_LOG_INFO("ttsTiming= cycles=%" PRIu32 " usec=%" PRIu32 " coreClockHz=%" PRIu32
                " status=%" PRIu32 " mismatchIndex=%" PRIu32 " sdmaIrqSeen=%u",
                cycles,
                usec,
                SystemCoreClock,
                (uint32_t)result,
                mismatchIndex,
                g_measureRetained.sdmaIrqSeen);
    PW_LOG_INFO("ttsBuffers= tx0=%u tx1=%u tx2=%u tx3=%u rx0=%u rx1=%u rx2=%u rx3=%u tx254=%u rx254=%u",
                ezh_data_buffer[0],
                ezh_data_buffer[1],
                ezh_data_buffer[2],
                ezh_data_buffer[3],
                ezh_data_buffer_rx[0],
                ezh_data_buffer_rx[1],
                ezh_data_buffer_rx[2],
                ezh_data_buffer_rx[3],
                ezh_data_buffer[254],
                ezh_data_buffer_rx[254]);
#else
    (void)result;
    (void)cycles;
    (void)mismatchIndex;
#endif
}

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
    uint32_t writeSdmaIrqSeen = 0U;
    uint32_t writeCpuIrqCount = 0U;
    uint32_t writeCpuIrqControlCount = 0U;
    uint32_t writeCpuIrqDataCount = 0U;
    uint32_t writeCpuIrqSuppressedCount = 0U;
    uint32_t writeCpuIrqSuppressedPreDisableNvicCount = 0U;
    uint32_t writeCpuIrqSuppressedPreDisablePendingMask = 0U;
    uint32_t writeCpuIrqSuppressedPreDisableStatusMask = 0U;
    uint32_t writePostReturnPendingMask = 0U;
    uint32_t writePostReturnStatusMask = 0U;
    uint32_t writePostReturnNvicPending = 0U;
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
    uint32_t transferCycles = 0U;
    uint32_t mismatchIndex = APP_MEASURE_NO_MISMATCH_INDEX;
    uint8_t slaveAddr = 0U;
    status_t result           = kStatus_Success;

    BOARD_InitHardware();
    master_enable_retained_measurement_ram();
    master_reset_retained_measurement();
    log_info_line("master: hardware init done\n");

    log_info_line("master: start\n");
#if !APP_LINKSERVER_TIMING_MEASURE
    PW_LOG_INFO("MCUX SDK version: %s", MCUXSDK_VERSION_FULL_STR);
    PW_LOG_INFO("EZHB Builder");
#endif
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

#if !APP_LINKSERVER_TIMING_MEASURE
    PW_LOG_INFO("master: starting daa broadcast");
#endif

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
    init_cycle_counter();

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
    writeSdmaIrqSeen = g_sdmaIrqSeen ? 1U : 0U;
    writeCpuIrqCount = I3C_MasterGetIrqEntryCount(EXAMPLE_MASTER);
    writeCpuIrqControlCount = I3C_MasterGetControlIrqEntryCount(EXAMPLE_MASTER);
    writeCpuIrqDataCount = I3C_MasterGetDataIrqEntryCount(EXAMPLE_MASTER);
    writeCpuIrqSuppressedCount = I3C_MasterGetSuppressedIrqCount(EXAMPLE_MASTER);
    writeCpuIrqSuppressedPreDisableNvicCount = I3C_MasterGetSuppressedNvicPendingCount(EXAMPLE_MASTER);
    writeCpuIrqSuppressedPreDisablePendingMask = I3C_MasterGetSuppressedPendingMask(EXAMPLE_MASTER);
    writeCpuIrqSuppressedPreDisableStatusMask = I3C_MasterGetSuppressedStatusMask(EXAMPLE_MASTER);
    writePostReturnPendingMask = I3C_MasterGetPendingInterrupts(EXAMPLE_MASTER);
    writePostReturnStatusMask = EXAMPLE_MASTER->MSTATUS;
    writePostReturnNvicPending = NVIC_GetPendingIRQ(I3C0_IRQn) != 0U ? 1U : 0U;
    if (result != kStatus_Success)
    {
        log_irq_metrics("write",
                        writeSdmaIrqSeen,
                        writeCpuIrqCount,
                        writeCpuIrqControlCount,
                        writeCpuIrqDataCount,
                        writeCpuIrqSuppressedCount,
                        writeCpuIrqSuppressedPreDisableNvicCount,
                        writeCpuIrqSuppressedPreDisablePendingMask,
                        writeCpuIrqSuppressedPreDisableStatusMask);
        log_smartdma_window_metrics("write", &g_i3c_m_handle);
        log_post_return_snapshot(
            "write", writePostReturnPendingMask, writePostReturnStatusMask, writePostReturnNvicPending);
        log_status_error("master: write transfer failed ", (uint32_t)result);
        transferCycles = read_cycle_counter();
        log_timing_summary(result, transferCycles, mismatchIndex);
        return -1;
    }

    log_info_line("master: write transfer complete\n");
    log_irq_metrics("write",
                    writeSdmaIrqSeen,
                    writeCpuIrqCount,
                    writeCpuIrqControlCount,
                    writeCpuIrqDataCount,
                    writeCpuIrqSuppressedCount,
                    writeCpuIrqSuppressedPreDisableNvicCount,
                    writeCpuIrqSuppressedPreDisablePendingMask,
                    writeCpuIrqSuppressedPreDisableStatusMask);
    log_smartdma_window_metrics("write", &g_i3c_m_handle);
    log_post_return_snapshot("write", writePostReturnPendingMask, writePostReturnStatusMask, writePostReturnNvicPending);

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
        log_irq_metrics("read",
                        readSdmaIrqSeen,
                        readCpuIrqCount,
                        readCpuIrqControlCount,
                        readCpuIrqDataCount,
                        readCpuIrqSuppressedCount,
                        readCpuIrqSuppressedPreDisableNvicCount,
                        readCpuIrqSuppressedPreDisablePendingMask,
                        readCpuIrqSuppressedPreDisableStatusMask);
        log_smartdma_window_metrics("read", &g_i3c_m_handle);
        log_post_return_snapshot(
            "read", readPostReturnPendingMask, readPostReturnStatusMask, readPostReturnNvicPending);
        log_status_error("master: read transfer failed ", (uint32_t)result);
        transferCycles = read_cycle_counter();
        log_timing_summary(result, transferCycles, mismatchIndex);
        return -1;
    }

    log_info_line("master: read transfer complete\n");

    for (uint32_t index = 0U; index < sizeof(ezh_data_buffer); index++)
    {
        if (ezh_data_buffer[index] != ezh_data_buffer_rx[index])
        {
#if !APP_LINKSERVER_TIMING_MEASURE
            PW_LOG_ERROR("master: roundtrip mismatch index=%" PRIu32 " expected=0x%02X actual=0x%02X",
                         index,
                         ezh_data_buffer[index],
                         ezh_data_buffer_rx[index]);
#endif
            log_buffer_info("master: expected data\n", ezh_data_buffer, sizeof(ezh_data_buffer));
            log_buffer_info("master: rx data\n", ezh_data_buffer_rx, sizeof(ezh_data_buffer_rx));
            mismatchIndex = index;
            transferCycles = read_cycle_counter();
            log_timing_summary(result, transferCycles, mismatchIndex);
            while (1)
            {
                __WFI();
            }
        }
    }

    log_info_line("master: roundtrip compare success\n");
    transferCycles = read_cycle_counter();
    log_timing_summary(result, transferCycles, mismatchIndex);
    log_irq_metrics("read",
                    readSdmaIrqSeen,
                    readCpuIrqCount,
                    readCpuIrqControlCount,
                    readCpuIrqDataCount,
                    readCpuIrqSuppressedCount,
                    readCpuIrqSuppressedPreDisableNvicCount,
                    readCpuIrqSuppressedPreDisablePendingMask,
                    readCpuIrqSuppressedPreDisableStatusMask);
    log_smartdma_window_metrics("read", &g_i3c_m_handle);
    log_post_return_snapshot("read", readPostReturnPendingMask, readPostReturnStatusMask, readPostReturnNvicPending);

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