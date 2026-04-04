/*
 * Copyright 2019, 2022 NXP
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/*  Standard C Included Files */
#include <string.h>
#include <stdint.h>
/*  SDK Included Files */
#include "pin_mux.h"
#include "clock_config.h"
#include "board.h"
#include "fsl_debug_console.h"
#include "fsl_i3c.h"

#ifndef APP_ENABLE_SEMIHOST
#define APP_ENABLE_SEMIHOST 1
#endif

static void semihost_write0(const char *message);

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

static void semihost_write_label_hex32(const char *label, uint32_t value)
{
    semihost_write0(label);
    semihost_write_hex32(value);
}

static void semihost_write0(const char *message)
{
#if APP_ENABLE_SEMIHOST
    register uint32_t operation asm("r0") = 0x04U;
    register const char *parameter asm("r1") = message;

    __asm volatile(
        "bkpt 0xAB"
        : "+r"(operation)
        : "r"(parameter)
        : "memory");
#else
    (void)message;
#endif
}

/*******************************************************************************
 * Definitions
 ******************************************************************************/
#define EXAMPLE_SLAVE              I3C0
#define I3C_SLAVE_CLOCK_FREQUENCY  CLOCK_GetLpOscFreq()
#define I3C_TIME_OUT_INDEX         0xFFFFFFFFU
#define I3C_MASTER_SLAVE_ADDR_7BIT 0x1EU


#define ENABLE_PRINTF

/*For MASTER DMA TX TEST*/
//#define I3C_MASTER_DMA_TX_TEST
#define I3C_SLAVE_RX_DATA_LENGTH            33U



///*For MASTER DMA RX TEST*/
#define I3C_MASTER_DMA_RX_TEST
#define I3C_SLAVE_TX_DATA_LENGTH            32U


/*******************************************************************************
 * Prototypes
 ******************************************************************************/

/*******************************************************************************
 * Variables
 ******************************************************************************/

uint8_t g_slave_txBuff[I3C_SLAVE_TX_DATA_LENGTH] = {0};
uint8_t g_slave_rxBuff[I3C_SLAVE_RX_DATA_LENGTH] = {0};


volatile bool g_slaveCompletionFlag         = false;
i3c_slave_handle_t g_i3c_s_handle;
uint8_t *g_txBuff;
uint32_t g_txSize                = I3C_SLAVE_TX_DATA_LENGTH;
volatile uint8_t g_deviceAddress = 0U;
uint8_t *g_deviceBuff            = NULL;
uint8_t g_deviceBuffSize         = I3C_SLAVE_RX_DATA_LENGTH;
/*******************************************************************************
 * Code
 ******************************************************************************/
static void i3c_slave_buildTxBuff(uint8_t *regAddr, uint8_t **txBuff, uint32_t *txBuffSize)
{
    /* If this is a combined frame and have device address information, send out the device buffer content and size.
     The received device address information is loaded in g_slave_rxBuff according to callback implementation of
     kI3C_SlaveReceiveEvent*/
    if ((regAddr != NULL) && (g_slave_rxBuff[0] == g_deviceAddress))
    {
        *txBuff     = g_deviceBuff;
        *txBuffSize = g_deviceBuffSize;
    }
    else
    {
        /* No valid register address information received, send default tx buffer. */
        *txBuff     = g_txBuff;
        *txBuffSize = g_txSize;
    }
}

static void i3c_slave_callback(I3C_Type *base, i3c_slave_transfer_t *xfer, void *userData)
{
    switch ((uint32_t)xfer->event)
    {
        case kI3C_SlaveAddressMatchEvent:
            semihost_write0("slave: address match\n");
            break;

        /*  Transmit request */
        case kI3C_SlaveTransmitEvent:
            semihost_write0("slave: transmit event\n");
            /*  Update information for transmit process */
            i3c_slave_buildTxBuff(xfer->rxData, &xfer->txData, (uint32_t *)&xfer->txDataSize);
            break;

        /*  Receive request */
        case kI3C_SlaveReceiveEvent:
            semihost_write0("slave: receive event\n");
            /*  Update information for received process */
            xfer->rxData     = g_slave_rxBuff;
            xfer->rxDataSize = I3C_SLAVE_RX_DATA_LENGTH;
            break;

        case kI3C_SlaveStartEvent:
            semihost_write0("slave: start event\n");
            break;

        /*  Transmit request */
        case (kI3C_SlaveTransmitEvent | kI3C_SlaveHDRCommandMatchEvent):
            semihost_write0("slave: hdr transmit event\n");
            /*  Update information for transmit process */
            xfer->txData     = g_slave_txBuff;
            xfer->txDataSize = I3C_SLAVE_TX_DATA_LENGTH;
            break;

        /*  Receive request */
        case (kI3C_SlaveReceiveEvent | kI3C_SlaveHDRCommandMatchEvent):
            semihost_write0("slave: hdr receive event\n");
            /*  Update information for received process */
            xfer->rxData     = g_slave_rxBuff;
            xfer->rxDataSize = I3C_SLAVE_RX_DATA_LENGTH;
            break;

        /*  Transfer done */
        case kI3C_SlaveCompletionEvent:
            if (xfer->completionStatus == kStatus_Success)
            {
                g_slaveCompletionFlag = true;
                semihost_write0("slave: completion event\n");
            }
            else
            {
                semihost_write0("slave: completion status ");
                semihost_write_hex32((uint32_t)xfer->completionStatus);
            }
            break;

#if defined(I3C_ASYNC_WAKE_UP_INTR_CLEAR)
        /*  Handle async wake up interrupt on specific platform. */
        case kI3C_SlaveAddressMatchEvent:
            I3C_ASYNC_WAKE_UP_INTR_CLEAR
            break;
#endif

        default:
            break;
    }
}

/*!
 * @brief Main function
 */
int main(void)
{
    i3c_slave_config_t slaveConfig;
    uint32_t eventMask = kI3C_SlaveCompletionEvent;
    bool dynamicAddrReported = false;
#if defined(I3C_ASYNC_WAKE_UP_INTR_CLEAR)
    eventMask |= kI3C_SlaveAddressMatchEvent;
#endif

//    /* Attach main clock to I3C, 396MHz / 4 = 99MHz. */
//    CLOCK_AttachClk(kMAIN_CLK_to_I3C_CLK);
//    CLOCK_SetClkDiv(kCLOCK_DivI3cClk, 4);


    BOARD_InitBootPins();
    BOARD_BootClockRUN();
    semihost_write0("slave: boot init done\n");
    //BOARD_InitSimulationClocks();
    
#ifdef ENABLE_PRINTF    
    BOARD_InitDebugConsole();
#endif
    semihost_write0("slave: debug console init done\n");
    
    CLOCK_AttachClk(kMAIN_CLK_to_I3C_CLK);
    CLOCK_SetClkDiv(kCLOCK_DivI3cClk, 8);
    /* Attach lposc_1m clock to I3C time control, clear halt for slow clock. */
    CLOCK_AttachClk(kLPOSC_to_I3C_TC_CLK);
    CLOCK_SetClkDiv(kCLOCK_DivI3cTcClk, 1);
    CLOCK_SetClkDiv(kCLOCK_DivI3cSlowClk, 1);
    
//    IOPCTL->PIO[2][29] = 0x141;
//    IOPCTL->PIO[2][30] = 0x141;
//    IOPCTL->PIO[2][31] = 0x001;
//    SYSCTL0->AUTOCLKGATEOVERRIDE0 = 0x0000003F;
//    SYSCTL0->AUTOCLKGATEOVERRIDE1 = 0x3FFFFFFF;

#ifdef ENABLE_PRINTF    
    PRINTF("\r\nI3C board2board interrupt example -- Slave transfer.\r\n");
    PRINTF("\r\n CPU Freq = %d\r\n",CLOCK_GetFreq(kCLOCK_CoreSysClk));
    PRINTF("\r\n I3C Freq = %d\r\n",CLOCK_GetFreq(kCLOCK_I3cClk));
#endif
    semihost_write0("slave: i3c clocks configured\n");


    I3C_SlaveGetDefaultConfig(&slaveConfig);

    slaveConfig.staticAddr = I3C_MASTER_SLAVE_ADDR_7BIT;
    slaveConfig.vendorID   = 0x123U;
    slaveConfig.offline    = false;

    I3C_SlaveInit(EXAMPLE_SLAVE, &slaveConfig, I3C_SLAVE_CLOCK_FREQUENCY);
    semihost_write0("slave: i3c slave init done\n");

    eventMask = kI3C_SlaveAllEvents;

    /* Create slave handle. */
    I3C_SlaveTransferCreateHandle(EXAMPLE_SLAVE, &g_i3c_s_handle, i3c_slave_callback, NULL);

    g_txBuff = g_slave_txBuff;
    /* Start slave non-blocking transfer. */
    I3C_SlaveTransferNonBlocking(EXAMPLE_SLAVE, &g_i3c_s_handle, eventMask);
    semihost_write0("slave: waiting for master traffic\n");


    uint32_t timeout_i = 0U;

#ifdef ENABLE_PRINTF    
    PRINTF("\r\nCheck I3C master I3C SDR transfer.\r\n");
#endif
    
#ifdef I3C_MASTER_DMA_TX_TEST
      
    /* For I3C SDR transfer check, master board will not send subaddress(device address). The first transfer is a
    I3C SDR write transfer, master will send one byte transmit size + several bytes transmit buffer content. */
    /* Wait for master transmit completed. */
    memset(g_slave_rxBuff, 0, I3C_SLAVE_RX_DATA_LENGTH);
    timeout_i = 0U;

    while ((g_slaveCompletionFlag != true) && (++timeout_i < I3C_TIME_OUT_INDEX))
     {
     }
    
    g_slaveCompletionFlag = false;

#ifdef ENABLE_PRINTF
    PRINTF("Slave will receive:");
    if (timeout_i == I3C_TIME_OUT_INDEX)
    {
        PRINTF("\r\nTransfer timeout.\r\n");
        for(uint32_t i = 0; i < I3C_SLAVE_RX_DATA_LENGTH;i++)
        {
            if (i % 8 == 0)
            {
                PRINTF("\r\n");
            }
            PRINTF("0x%2x  ", g_slave_rxBuff[i]);
        }
        return -1;
    }  
    
    for(uint32_t i = 0; i < I3C_SLAVE_RX_DATA_LENGTH;i++)
    {
        if (i % 8 == 0)
        {
            PRINTF("\r\n");
        }
        PRINTF("0x%2x  ", g_slave_rxBuff[i]);
    }
    
    PRINTF("\r\nI3C slave I3C SDR transfer finished.\r\n");
#endif    
    
#endif
    
    
#ifdef I3C_MASTER_DMA_RX_TEST
    /* The second transfer is a I3C SDR read transfer, master will read back the transmit buffer content just sent. */
    /* Wait for slave transmit completed. */


    for (uint32_t i = 0U; i < I3C_SLAVE_TX_DATA_LENGTH; i++)
    {
       g_txBuff[i] = i;
    }
    g_txSize = I3C_SLAVE_TX_DATA_LENGTH;

#ifdef ENABLE_PRINTF    
    PRINTF("Slave will send:");
    for(uint32_t i = 0; i < I3C_SLAVE_TX_DATA_LENGTH;i++)
    {
        if (i % 8 == 0)
        {
            PRINTF("\r\n");
        }
        PRINTF("0x%2x  ", g_txBuff[i]);
    }
#endif
    
    timeout_i = 0U;
    
    while (g_slaveCompletionFlag != true)
    {
        if ((!dynamicAddrReported) && ((EXAMPLE_SLAVE->SDYNADDR & I3C_SDYNADDR_DAVALID_MASK) != 0U))
        {
            dynamicAddrReported = true;
            semihost_write_label_hex32("slave: dynamic addr reg=", EXAMPLE_SLAVE->SDYNADDR);
        }
        timeout_i++;
    }
    
    g_slaveCompletionFlag = false;
#ifdef ENABLE_PRINTF
    PRINTF("\r\nI3C master I3C SDR transfer finished.\r\n");
#endif
    semihost_write0("slave: transfer finished\n");
    semihost_write_label_hex32("slave: SSTATUS=", EXAMPLE_SLAVE->SSTATUS);
    semihost_write_label_hex32("slave: SERRWARN=", EXAMPLE_SLAVE->SERRWARN);
    semihost_write_label_hex32("slave: SDYNADDR=", EXAMPLE_SLAVE->SDYNADDR);

#endif
    
    while (1)
    {
    }
}
