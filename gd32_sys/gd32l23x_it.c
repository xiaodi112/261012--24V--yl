/*!
    \file    gd32l23x_it.c
    \brief   interrupt service routines

    \version 2021-08-04, V1.0.0, firmware for GD32L23x
*/

/*
    Copyright (c) 2021, GigaDevice Semiconductor Inc.

    Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

    1. Redistributions of source code must retain the above copyright notice, this
       list of conditions and the following disclaimer.
    2. Redistributions in binary form must reproduce the above copyright notice,
       this list of conditions and the following disclaimer in the documentation
       and/or other materials provided with the distribution.
    3. Neither the name of the copyright holder nor the names of its contributors
       may be used to endorse or promote products derived from this software without
       specific prior written permission.

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
OF SUCH DAMAGE.
*/

#include <stddef.h>     /* NULL */

#include "gd32l23x_it.h"
#include "main.h"
#include "systick.h"
/* 故障/断言打印直接走串口驱动：不经过 printf、不用堆、不碰 RTOS */
#include "bsp_uart.h"

/* ==================== 致命错误上报（串口裸打印）====================
 * 为什么单独写这几行：断言失败和硬件异常往往发生在"系统已经不正常"的时刻，
 * 这时候不能用 printf（要堆、要互斥量），也不能依赖 RTOS，只能直接往串口写。
 * 目的只有一个：让"静默卡死"变成"串口上能看到原因"。
 */

static void it_puts(const char *s)
{
    uint16_t n = 0U;

    if (NULL == s) {
        return;
    }
    while ('\0' != s[n]) {
        n++;
    }
    bsp_uart_write((const uint8_t *)s, n);
}

static void it_put_hex(uint32_t v)
{
    char     buf[9];
    uint8_t  i;

    for (i = 0U; i < 8U; i++) {
        uint32_t nib = (v >> ((7U - i) * 4U)) & 0x0FU;
        buf[i] = (char)((nib < 10U) ? ('0' + nib) : ('A' + (nib - 10U)));
    }
    buf[8] = '\0';
    it_puts(buf);
}

static void it_put_dec(uint32_t v)
{
    char    buf[11];
    uint8_t i = 0U;

    if (0U == v) {
        it_puts("0");
        return;
    }
    while ((0U != v) && (i < 10U)) {
        buf[i] = (char)('0' + (v % 10U));
        v /= 10U;
        i++;
    }
    while (i > 0U) {
        i--;
        bsp_uart_write((const uint8_t *)&buf[i], 1U);
    }
}

/*!
    \brief      断言失败处理（由 FreeRTOSConfig.h 的 configASSERT 调用）
    \note       打印"哪个文件哪一行断言失败"后停机。内核内部的断言也会走到这里，
                所以调度器起不来这类问题能直接看到原因，而不是无声卡死。
*/
void app_assert_fail(const char *file, int line)
{
    it_puts("\r\n!! ASSERT FAIL: ");
    it_puts(file);
    it_puts(":");
    it_put_dec((uint32_t)line);
    it_puts("\r\n");
    __disable_irq();
    while (1) {
    }
}

/*!
    \brief      this function handles NMI exception
    \param[in]  none
    \param[out] none
    \retval     none
*/
void NMI_Handler(void)
{
}

/*!
    \brief      硬件异常处理：打印故障寄存器与出错现场后停机
    \note       CFSR/HFSR 说明"哪一类故障"，BFAR/PC 说明"哪条指令"，
                拿 PC 去 map 文件里查就能定位到具体代码。
*/
void HardFault_Handler(void)
{
    uint32_t cfsr = *(volatile uint32_t *)0xE000ED28U;   /* 可配置故障状态 */
    uint32_t hfsr = *(volatile uint32_t *)0xE000ED2CU;   /* 硬件故障状态 */
    uint32_t bfar = *(volatile uint32_t *)0xE000ED38U;   /* 总线故障地址 */
    uint32_t msp  = __get_MSP();
    uint32_t psp  = __get_PSP();

    it_puts("\r\n!! HARD FAULT !!\r\n");
    it_puts("CFSR="); it_put_hex(cfsr);
    it_puts(" HFSR="); it_put_hex(hfsr);
    it_puts(" BFAR="); it_put_hex(bfar);
    it_puts("\r\n");
    /* 异常压栈帧：R0,R1,R2,R3,R12,LR,PC,xPSR；任务里出错看在 PSP，中断前出错看在 MSP。
     * 两个都打出来，哪个像 0x08xxxxxx 的地址就是真正的出错 PC。 */
    it_puts("PC(MSP)="); it_put_hex(((volatile uint32_t *)msp)[6]);
    it_puts(" PC(PSP)="); it_put_hex(((volatile uint32_t *)psp)[6]);
    it_puts("\r\n");

    while (1) {
    }
}

/*!
    \brief      this function handles SVC exception
    \param[in]  none
    \param[out] none
    \retval     none
*/
//void SVC_Handler(void)
//{
//}

/*!
    \brief      this function handles PendSV exception
    \param[in]  none
    \param[out] none
    \retval     none
*/
//void PendSV_Handler(void)
//{
//}

/*!
    \brief      this function handles SysTick exception
    \param[in]  none
    \param[out] none
    \retval     none
*/
//void SysTick_Handler(void)
//{
//    led_spark();
//    delay_decrement();
//}
