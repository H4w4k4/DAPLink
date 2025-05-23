/*
 * Copyright (c) 2004-2016 ARM Limited. All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the License); you may
 * not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an AS IS BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*----------------------------------------------------------------------------
*      RL-ARM - USB
*----------------------------------------------------------------------------
*      Name:    usbd_STM32L412.c
*      Purpose: Hardware Layer module for ST STM32L412
*      Rev.:    V1.0 (based on usbd_STM32F103 V4.70)
*---------------------------------------------------------------------------*/

/* Double Buffering is not supported                                         */

#include <rl_usb.h>
#include "stm32l4xx.h"
#include "usbreg.h"
#include "IO_Config.h"
#include "cortex_m.h"
#include "string.h"
#define __NO_USB_LIB_C
#include "usb_config.c"

#define USB_ISTR_W0C_MASK   (ISTR_PMAOVR | ISTR_ERR | ISTR_WKUP | ISTR_SUSP | ISTR_RESET | ISTR_SOF | ISTR_ESOF)
#define VAL_MASK            0xFFFF
#define VAL_SHIFT           16
#define EP_NUM_MASK         0xFFFF
#define EP_NUM_SHIFT        0

#define USB_DBL_BUF_EP      0x0000

#define EP_BUF_ADDR (sizeof(EP_BUFF_DSCR)*(USBD_EP_NUM+1)) /* Endpoint Buf Adr */

volatile static EP_BUFF_DSCR  *pBUF_DSCR = (EP_BUFF_DSCR *) USB_PMA_ADDR; /* Ptr to EP Buf Desc   */

volatile uint16_t FreeBufAddr;                        /* Endpoint Free Buffer Address       */

uint32_t StatQueue[(USBD_EP_NUM + 1) * 2 + 1];
uint32_t StatQueueHead = 0;
uint32_t StatQueueTail = 0;
uint32_t LastIstr = 0;

static void USB_ClearSRAM() {
    char *buffer = (char*) USB_PMA_ADDR;

    for (uint16_t i = 0; i < USB_PMA_SIZE; i++) {
        buffer[i] = 0;
    }
}

inline static void stat_enque(uint32_t stat)
{
    cortex_int_state_t state;
    state = cortex_int_get_and_disable();
    StatQueue[StatQueueTail] = stat;
    StatQueueTail = (StatQueueTail + 1) % (sizeof(StatQueue) / sizeof(StatQueue[0]));
    cortex_int_restore(state);
}

inline static uint32_t stat_deque()
{
    cortex_int_state_t state;
    uint32_t stat;
    state = cortex_int_get_and_disable();
    stat = StatQueue[StatQueueHead];
    StatQueueHead = (StatQueueHead + 1) % (sizeof(StatQueue) / sizeof(StatQueue[0]));
    cortex_int_restore(state);

    return stat;
}

inline static uint32_t stat_is_empty()
{
    cortex_int_state_t state;
    uint32_t empty;
    state = cortex_int_get_and_disable();
    empty = StatQueueHead == StatQueueTail;
    cortex_int_restore(state);
    return empty;
}

/*
 *  Reset Endpoint
 *    Parameters:      EPNum: Endpoint Number
 *                       EPNum.0..3: Address
 *                       EPNum.7:    Dir
 *    Return Value:    None
 */

void EP_Reset(U32 EPNum)
{
    U32 num, val;
    num = EPNum & 0x0F;
    val = EPxREG(num);

    if (EPNum & 0x80) {                   /* IN Endpoint                        */
        EPxREG(num) = val & (EP_MASK | EP_DTOG_TX);
    } else {                              /* OUT Endpoint                       */
        EPxREG(num) = val & (EP_MASK | EP_DTOG_RX);
    }
}


/*
 *  Set Endpoint Status
 *    Parameters:      EPNum: Endpoint Number
 *                       EPNum.0..3: Address
 *                       EPNum.7:    Dir
 *                     stat: New Status
 *    Return Value:    None
 */

void EP_Status(U32 EPNum, U32 stat)
{
    U32 num, val;
    num = EPNum & 0x0F;
    val = EPxREG(num);

    if (EPNum & 0x80) {                   /* IN Endpoint                        */
        EPxREG(num) = EP_VAL_UNCHANGED(val) | ((val ^ stat) & EP_STAT_TX);
    } else {                              /* OUT Endpoint                       */
        EPxREG(num) = EP_VAL_UNCHANGED(val) | ((val ^ stat) & EP_STAT_RX);
    }
}


/*
 *  USB Device Interrupt enable
 *   Called by USBD_Init to enable the USB Interrupt
 *    Return Value:    None
 */

#ifdef __RTX
void __svc(1) USBD_IntrEna(void);
void __SVC_1(void)
{
#else
void          USBD_IntrEna(void)
{
#endif
    NVIC_EnableIRQ(USB_IRQn);
}


/*
 *  USB Device Initialize Function
 *   Called by the User to initialize USB
 *    Return Value:    None
 */

void USBD_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;

    GPIO_InitStructure.Pull = GPIO_NOPULL;
    GPIO_InitStructure.Alternate = GPIO_AF10_USB_FS;
    GPIO_InitStructure.Mode = GPIO_MODE_INPUT;
    GPIO_InitStructure.Speed = GPIO_SPEED_FREQ_HIGH;
    GPIO_InitStructure.Pin = USB_DM_PIN | USB_DP_PIN;
    HAL_GPIO_Init(USB_DM_PIN_PORT, &GPIO_InitStructure);

    SET_BIT(PWR->CR2, PWR_CR2_USV);		  /* turn on power for USB interface 	*/

    CNTR = USB_CNTR_FRES;
    CNTR = 2;
    CNTR = 0;
    ISTR = 0;

    EXTI->IMR1 |= EXTI_IMR1_IM17;

    USBD_IntrEna();                       /* Enable USB Interrupts              */
    /* Control USB connecting via SW                                            */
    USB_CONNECT_OFF();
}


/*
 *  USB Device Connect Function
 *   Called by the User to Connect/Disconnect USB Device
 *    Parameters:      con:   Connect/Disconnect
 *    Return Value:    None
 */

void USBD_Connect(BOOL con)
{
    if (con) {
        CNTR = CNTR_FRES;                   /* Force USB Reset                    */
        CNTR = 0;
        ISTR = 0;                           /* Clear Interrupt Status             */
        CNTR = CNTR_RESETM | CNTR_SUSPM | CNTR_WKUPM; /* USB Interrupt Mask       */
        USB_CONNECT_ON();
    } else {
        CNTR = CNTR_FRES | CNTR_PDWN;       /* Switch Off USB Device              */
        USB_CONNECT_OFF();
    }
}


/*
 *  USB Device Reset Function
 *   Called automatically on USB Device Reset
 *    Return Value:    None
 */

void USBD_Reset(void)
{
    NVIC_DisableIRQ(USB_IRQn);
    /* Double Buffering is not yet supported                                    */
    CNTR = (uint16_t) 0x0;
    ISTR = (uint16_t) 0x0;                             /* Clear Interrupt Status             */
    CNTR = CNTR_CTRM | CNTR_RESETM | CNTR_SUSPM | CNTR_WKUPM |
#ifdef __RTX
           ((USBD_RTX_DevTask   != 0) ? CNTR_ERRM    : 0) |
           ((USBD_RTX_DevTask   != 0) ? CNTR_PMAOVRM : 0) |
           ((USBD_RTX_DevTask   != 0) ? CNTR_SOFM    : 0) |
           ((USBD_RTX_DevTask   != 0) ? CNTR_ESOFM   : 0);
#else
           ((USBD_P_Error_Event != 0) ? CNTR_ERRM    : 0) |
           ((USBD_P_Error_Event != 0) ? CNTR_PMAOVRM : 0) |
           ((USBD_P_SOF_Event   != 0) ? CNTR_SOFM    : 0) |
           ((USBD_P_SOF_Event   != 0) ? CNTR_ESOFM   : 0);
#endif

    USB_ClearSRAM();
    EPxREG(0) = EP_CONTROL | EP_RX_VALID;
    DADDR = DADDR_EF | 0;                 /* Enable USB Default Address         */
    FreeBufAddr = (uint16_t) EP_BUF_ADDR;
    BTABLE = 0x00;                        /* set BTABLE Address                 */

    /* Setup Control Endpoint 0 */
    pBUF_DSCR->ADDR_TX = (uint16_t) FreeBufAddr;
    FreeBufAddr += USBD_MAX_PACKET0;
    pBUF_DSCR->ADDR_RX =(uint16_t) FreeBufAddr;
    FreeBufAddr += USBD_MAX_PACKET0;

    if (USBD_MAX_PACKET0 > 62) {
    	pBUF_DSCR->COUNT_RX = (uint16_t) (((USBD_MAX_PACKET0 << 5) - 1) | 0x8000);
    } else {
    	pBUF_DSCR->COUNT_RX =  (uint16_t) (USBD_MAX_PACKET0 << 9);
    }

    NVIC_EnableIRQ(USB_IRQn);
}


/*
 *  USB Device Suspend Function
 *   Called automatically on USB Device Suspend
 *    Return Value:    None
 */

void USBD_Suspend(void)
{
    CNTR |= CNTR_FSUSP;                   /* Force Suspend                      */
    CNTR |= CNTR_LPMODE;                  /* Low Power Mode                     */
}


/*
 *  USB Device Resume Function
 *   Called automatically on USB Device Resume
 *    Return Value:    None
 */

void USBD_Resume(void)
{
    /* Performed by Hardware                                                    */
}


/*
 *  USB Device Remote Wakeup Function
 *   Called automatically on USB Device Remote Wakeup
 *    Return Value:    None
 */

void USBD_WakeUp(void)
{
    CNTR &= ~CNTR_FSUSP;                  /* Clear Suspend                      */
}


/*
 *  USB Device Remote Wakeup Configuration Function
 *    Parameters:      cfg:   Device Enable/Disable
 *    Return Value:    None
 */

void USBD_WakeUpCfg(BOOL cfg)
{
    /* Not needed                                                               */
}


/*
 *  USB Device Set Address Function
 *    Parameters:      adr:   USB Device Address
 *                     setup: Called in setup stage (!=0), else after status stage
 *    Return Value:    None
 */

void USBD_SetAddress(U32 adr, U32 setup)
{
    if (setup) {
        return;
    }

    DADDR = DADDR_EF | adr;
}


/*
 *  USB Device Configure Function
 *    Parameters:      cfg:   Device Configure/Deconfigure
 *    Return Value:    None
 */

void USBD_Configure(BOOL cfg)
{
    if (cfg == __FALSE) {
        FreeBufAddr  = EP_BUF_ADDR;
        FreeBufAddr += 2 * USBD_MAX_PACKET0; /* reset Buffer address               */
    }
}


/*
 *  Configure USB Device Endpoint according to Descriptor
 *    Parameters:      pEPD:  Pointer to Device Endpoint Descriptor
 *    Return Value:    None
 */

void USBD_ConfigEP(USB_ENDPOINT_DESCRIPTOR *pEPD)
{
    /* Double Buffering is not yet supported                                    */
    U32 num, val;
    num = pEPD->bEndpointAddress & 0x0F;
    val = pEPD->wMaxPacketSize;

    if (pEPD->bEndpointAddress & USB_ENDPOINT_DIRECTION_MASK) {
        (pBUF_DSCR + num)->ADDR_TX = (uint16_t) FreeBufAddr;
        val = (val + 1) & ~1;
    } else {
        (pBUF_DSCR + num)->ADDR_RX = (uint16_t) FreeBufAddr;
        if (val > 62) {
            val = (val + 31) & ~31;
            (pBUF_DSCR + num)->COUNT_RX = (uint16_t) (((val << 5) - 1) | 0x8000);
        } else {
            val = (val + 1)  & ~1;
            (pBUF_DSCR + num)->COUNT_RX = (uint16_t)  (val << 9);
        }
    }

    FreeBufAddr += val;

    switch (pEPD->bmAttributes & USB_ENDPOINT_TYPE_MASK) {
        case USB_ENDPOINT_TYPE_CONTROL:
            val = EP_CONTROL;
            break;

        case USB_ENDPOINT_TYPE_ISOCHRONOUS:
            val = EP_ISOCHRONOUS;
            break;

        case USB_ENDPOINT_TYPE_BULK:
            val = EP_BULK;

            if (USB_DBL_BUF_EP & (1 << num)) {
                val |= EP_KIND;
            }

            break;

        case USB_ENDPOINT_TYPE_INTERRUPT:
            val = EP_INTERRUPT;
            break;
    }

    val |= num;
    EPxREG(num) = val;
}


/*
 *  Set Direction for USB Device Control Endpoint
 *    Parameters:      dir:   Out (dir == 0), In (dir <> 0)
 *    Return Value:    None
 */

void USBD_DirCtrlEP(U32 dir)
{
    /* Not needed                                                               */
}


/*
 *  Enable USB Device Endpoint
 *    Parameters:      EPNum: Device Endpoint Number
 *                       EPNum.0..3: Address
 *                       EPNum.7:    Dir
 *    Return Value:    None
 */

void USBD_EnableEP(U32 EPNum)
{
    EP_Status(EPNum, EP_TX_NAK | EP_RX_VALID);    /* EP is able to receive      */
}


/*
 *  Disable USB Endpoint
 *    Parameters:      EPNum: Endpoint Number
 *                       EPNum.0..3: Address
 *                       EPNum.7:    Dir
 *    Return Value:    None
 */

void USBD_DisableEP(U32 EPNum)
{
    EP_Status(EPNum, EP_TX_DIS | EP_RX_DIS);
}


/*
 *  Reset USB Device Endpoint
 *    Parameters:      EPNum: Device Endpoint Number
 *                       EPNum.0..3: Address
 *                       EPNum.7:    Dir
 *    Return Value:    None
 */

void USBD_ResetEP(U32 EPNum)
{
    EP_Reset(EPNum);
}


/*
 *  Set Stall for USB Device Endpoint
 *    Parameters:      EPNum: Device Endpoint Number
 *                       EPNum.0..3: Address
 *                       EPNum.7:    Dir
 *    Return Value:    None
 */

void USBD_SetStallEP(U32 EPNum)
{
    EP_Status(EPNum, EP_TX_STALL | EP_RX_STALL);
}


/*
 *  Clear Stall for USB Device Endpoint
 *    Parameters:      EPNum: Device Endpoint Number
 *                       EPNum.0..3: Address
 *                       EPNum.7:    Dir
 *    Return Value:    None
 */

void USBD_ClrStallEP(U32 EPNum)
{
    EP_Reset(EPNum);                      /* reset DTog Bits                    */
    EP_Status(EPNum, EP_TX_VALID | EP_RX_VALID);
}


/*
 *  Clear USB Device Endpoint Buffer
 *    Parameters:      EPNum: Device Endpoint Number
 *                       EPNum.0..3: Address
 *                       EPNum.7:    Dir
 *    Return Value:    None
 */

void USBD_ClearEPBuf(U32 EPNum)
{
    ;
}


/*
 *  Read USB Device Endpoint Data
 *    Parameters:      EPNum: Device Endpoint Number
 *                       EPNum.0..3: Address
 *                       EPNum.7:    Dir
 *                     pData: Pointer to Data Buffer
 *    Return Value:    Number of bytes read
 */

U32 USBD_ReadEP(U32 EPNum, U8 *pData, U32 bufsz)
{
    /* Double Buffering is not yet supported                                    */
    U32 num, cnt,n;
    uint16_t *pv, temp;
    num = EPNum & 0x0F;
    pv  = (uint16_t *)(USB_PMA_ADDR + ((uint16_t) (pBUF_DSCR + num)->ADDR_RX));
    cnt = ((uint16_t) (pBUF_DSCR + num)->COUNT_RX) & EP_COUNT_MASK;
    if (cnt > bufsz) {
        cnt = bufsz;
    }

    for (n = 0; n < (cnt + 1) / 2; n++) {
      //  __UNALIGNED_UINT16_WRITE(pData, (uint16_t) *pv++);
        temp = (uint16_t) *pv++;
        *pData++ = (uint8_t) (temp & 0x00FF);
        *pData++ = (uint8_t) (temp >> 8);
    }

    EP_Status(EPNum, EP_RX_VALID);
    return (cnt);
}


/*
 *  Write USB Device Endpoint Data
 *    Parameters:      EPNum: Device Endpoint Number
 *                       EPNum.0..3: Address
 *                       EPNum.7:    Dir
 *                     pData: Pointer to Data Buffer
 *                     cnt:   Number of bytes to write
 *    Return Value:    Number of bytes written
 */

U32 USBD_WriteEP(U32 EPNum, U8 *pData, U32 cnt)
{
    /* Double Buffering is not yet supported                                    */
    U32 num, n;
    uint16_t *pv;
    U16 statusEP;
    num = EPNum & 0x0F;
    pv  = (uint16_t *)(USB_PMA_ADDR + ((uint16_t) (pBUF_DSCR + num)->ADDR_TX));

    for (n = 0; n < (cnt + 1) / 2; n++) {
        *pv++ = (uint16_t) ((((uint16_t) *(pData+1)) << 8) + *pData);
        pData += 2;
    }

    (pBUF_DSCR + num)->COUNT_TX = (uint16_t) cnt;
    statusEP = EPxREG(num);

    if ((statusEP & EP_STAT_TX) != EP_TX_STALL) {
        EP_Status(EPNum, EP_TX_VALID);      /* do not make EP valid if stalled    */
    }

    return (cnt);
}


/*
 *  Get USB Device Last Frame Number
 *    Parameters:      None
 *    Return Value:    Frame Number
 */

U32 USBD_GetFrame(void)
{
    return (FNR & FNR_FN);
}


#ifdef __RTX
U32 LastError;                          /* Last Error                         */

/*
 *  Get USB Last Error Code
 *    Parameters:      None
 *    Return Value:    Error Code
 */

U32 USBD_GetError(void)
{
    return (LastError);
}
#endif


/*
 *  USB Device Interrupt Service Routine
 */

void USB_IRQHandler(void)
{
    uint32_t istr;
    uint32_t num;
    uint32_t val;

    istr = ISTR;
    // Zero out endpoint ID since this is read from the queue
    LastIstr |= istr & ~(ISTR_DIR | ISTR_EP_ID);
    // Clear interrupts that are pending
    ISTR = ~(istr & USB_ISTR_W0C_MASK);
    if (istr & ISTR_CTR) {
        while ((istr = ISTR) & ISTR_CTR) {
            num = istr & ISTR_EP_ID;
            val = EPxREG(num);

            // Process and filter out the zero length status out endpoint to prevent
            // the next SETUP packet from being dropped.
            if ((0 == num) && (val & EP_CTR_RX) && !(val & EP_SETUP)
                    && (0 == (((uint16_t) (pBUF_DSCR + num)->COUNT_RX) & EP_COUNT_MASK))) {
                if (val & EP_CTR_TX) {
                    // Drop the RX event but not TX
                    stat_enque((((val & VAL_MASK) & ~EP_CTR_RX) << VAL_SHIFT) |
                               ((num & EP_NUM_MASK) << EP_NUM_SHIFT));
                } else {
                    // Drop the event
                }
            } else {
                stat_enque(((val & VAL_MASK) << VAL_SHIFT) |
                           ((num & EP_NUM_MASK) << EP_NUM_SHIFT));
            }


            if (val & EP_CTR_RX) {
                EPxREG(num) = EP_VAL_UNCHANGED(val) & ~EP_CTR_RX;
            }

            if (val & EP_CTR_TX) {
                EPxREG(num) = EP_VAL_UNCHANGED(val) & ~EP_CTR_TX;
            }
        }
    }

    USBD_SignalHandler();
}

void USBD_Handler(void)
{
    U32 istr, num, val, num_val;
    cortex_int_state_t state;

    // Get ISTR
    state = cortex_int_get_and_disable();
    istr = LastIstr;
    LastIstr = 0;
    cortex_int_restore(state);

    /* USB Reset Request                                                        */
    if (istr & ISTR_RESET) {
        USBD_Reset();
        usbd_reset_core();
#ifdef __RTX

        if (USBD_RTX_DevTask) {
            isr_evt_set(USBD_EVT_RESET, USBD_RTX_DevTask);
        }

#else

        if (USBD_P_Reset_Event) {
            USBD_P_Reset_Event();
        }

#endif
    }

    /* USB Suspend Request                                                      */
    if (istr & ISTR_SUSP) {
        USBD_Suspend();
#ifdef __RTX

        if (USBD_RTX_DevTask) {
            isr_evt_set(USBD_EVT_SUSPEND, USBD_RTX_DevTask);
        }

#else

        if (USBD_P_Suspend_Event) {
            USBD_P_Suspend_Event();
        }

#endif
    }

    /* USB Wakeup                                                               */
    if (istr & ISTR_WKUP) {
        USBD_WakeUp();
#ifdef __RTX

        if (USBD_RTX_DevTask) {
            isr_evt_set(USBD_EVT_RESUME,  USBD_RTX_DevTask);
        }

#else

        if (USBD_P_Resume_Event) {
            USBD_P_Resume_Event();
        }

#endif
    }

    /* Start of Frame                                                           */
    if (istr & ISTR_SOF) {
#ifdef __RTX

        if (USBD_RTX_DevTask) {
            isr_evt_set(USBD_EVT_SOF, USBD_RTX_DevTask);
        }

#else

        if (USBD_P_SOF_Event) {
            USBD_P_SOF_Event();
        }

#endif
    }

    /* PMA Over/underrun                                                        */
    if (istr & ISTR_PMAOVR) {
#ifdef __RTX
        LastError = 2;

        if (USBD_RTX_DevTask) {
            isr_evt_set(USBD_EVT_ERROR, USBD_RTX_DevTask);
        }

#else

        if (USBD_P_Error_Event) {
            USBD_P_Error_Event(2);
        }

#endif
    }

    /* Error: No Answer, CRC Error, Bit Stuff Error, Frame Format Error         */
    if (istr & ISTR_ERR) {
#ifdef __RTX
        LastError = 1;

        if (USBD_RTX_DevTask) {
            isr_evt_set(USBD_EVT_ERROR, USBD_RTX_DevTask);
        }

#else

        if (USBD_P_Error_Event) {
            USBD_P_Error_Event(1);
        }

#endif
    }

    /* Endpoint Interrupts                                                      */
    while ((istr & ISTR_CTR) && !stat_is_empty()) {
        num_val = stat_deque();
        num = (num_val >> EP_NUM_SHIFT) & EP_NUM_MASK;
        val = (num_val >> VAL_SHIFT) & VAL_MASK;
        if (val & EP_CTR_TX) {
#ifdef __RTX

            if (USBD_RTX_EPTask[num]) {
                isr_evt_set(USBD_EVT_IN,  USBD_RTX_EPTask[num]);
            }

#else

            if (USBD_P_EP[num]) {
                USBD_P_EP[num](USBD_EVT_IN);
            }

#endif
        }

        if (val & EP_CTR_RX) {
#ifdef __RTX

            if (USBD_RTX_EPTask[num]) {
                isr_evt_set((val & EP_SETUP) ? USBD_EVT_SETUP : USBD_EVT_OUT, USBD_RTX_EPTask[num]);
            }

#else

            if (USBD_P_EP[num]) {
                USBD_P_EP[num]((val & EP_SETUP) ? USBD_EVT_SETUP : USBD_EVT_OUT);
            }

#endif
        }
    }
}
