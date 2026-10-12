/*!
    \file    systick.c
    \brief   the systick configuration file

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

#include "gd32l23x.h"
#include "systick.h"

/*!
    \brief      关闭 SysTick（把配置权完整交给 FreeRTOS）
    \note       为什么不在这里使能 SysTick，也不保留原来的 1ms 延时：
                本工程的 SysTick 中断已经归 FreeRTOS（port.c 的 SysTick_Handler）。
                FreeRTOS 的 tick 处理 xTaskIncrementTick() 会去操作"延迟任务链表"，
                而这套链表要等**第一个任务创建**时才初始化。如果在调度器启动前就
                让 SysTick 中断跑起来（原厂这版 systick_config() 就是
                SysTick_Config() + 开中断），内核就会对未初始化的链表指针（NULL）
                做读写——现象正是"开机横幅能打印出来，然后整机静默卡死"。
                本工程在启动调度器前不需要任何滴答，所以这里只把 SysTick 关干净；
                SysTick 的 LOAD/时钟源/中断使能由 FreeRTOS 在
                vPortSetupTimerInterrupt() 里统一配置。
    \param[in]  none
    \param[out] none
    \retval     none
*/
void systick_config(void)
{
    SysTick->CTRL = 0U;     /* 关闭计数与 SysTick 中断 */
    SysTick->LOAD = 0U;
    SysTick->VAL  = 0U;
}
