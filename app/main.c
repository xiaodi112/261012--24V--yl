/*
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
#include <stdio.h>
#include <string.h>
#include "main.h"
/* 栈溢出钩子里要直接裸打印串口（不能走 printf_debug，原因见文件末尾） */
#include "bsp_uart.h"
/* FreeRTOS includes. */
#include "FreeRTOS.h"
#include "task.h"
/* 应用层 */
#include "app_motor.h"
#include "app_console.h"



/*!
    \brief      main function
    \param[in]  none
    \param[out] none
    \retval     none
*/
int main(void)
{
    /* 先把 SysTick 关干净：本工程 SysTick 中断归 FreeRTOS，
     * 在 vTaskStartScheduler() 之前绝不能让它触发——内核的 tick 处理会去操作
     * 那时还没初始化的延迟任务链表（NULL），现象就是"横幅能打印、随后整机静默卡死"。
     * 详见 gd32_sys/systick.c 里 systick_config() 的说明。 */
    systick_config();

    /* 应用层初始化：电机驱动、到位输入、命令队列（必须在创建任务前完成） */
    app_motor_init();
    /* 调试串口 + 控制台（串口先就绪，再建任务） */
    app_console_init();

    /* 创建电机任务：栈大小与优先级在 app_motor.h 中配置 */
    if (pdPASS != xTaskCreate(app_motor_task,
                              "motor",
                              (uint16_t)APP_MOTOR_TASK_STACK,
                              (void *)NULL,
                              (UBaseType_t)APP_MOTOR_TASK_PRIO,
                              (TaskHandle_t *)NULL)) {
        /* 创建失败（堆空间不足）：串口此刻已就绪，直接把原因报出来，
         * 不要静默死循环——否则现象和"串口坏了"完全一样，白白浪费排查时间 */
        printf_debug("ERROR: motor task create failed (heap?)\r\n");
        while (1) {
        }
    }

    /* 创建控制台任务：栈大小与优先级在 app_console.h 中配置。
     * 控制台不是关键任务，建不起来不影响电机动作，但要让调试时能发现（串口已经就绪）。 */
    if (pdPASS != xTaskCreate(app_console_task,
                              "console",
                              (uint16_t)APP_CONSOLE_TASK_STACK,
                              (void *)NULL,
                              (UBaseType_t)APP_CONSOLE_TASK_PRIO,
                              (TaskHandle_t *)NULL)) {
        printf_debug("ERROR: console task create failed\r\n");
    }

    vTaskStartScheduler();          //开启任务调度

    /* 正常永远执行不到这里：vTaskStartScheduler() 只在启动失败时返回
     * （例如堆不足，连空闲任务/定时器任务都建不出来）。到了这里说明调度器没起来。 */
    printf_debug("[ERROR] scheduler returned! (heap?)\r\n");
    while (1) {
    }
}



/* 调试打印统一用 app_console.h 里的 printf_debug()（先整块格式化再发送，带发送互斥）。
 * 本工程不做 fputc 重定向：代码里没有裸 printf 调用，也就没有逐字符输出的开销。 */



/* Stack overflow hook. */
void vApplicationStackOverflowHook( TaskHandle_t xTask, char *pcTaskName )
{
	(void)xTask;
	/* 这个钩子可能在任务切换（异常）上下文里被调用，所以直接裸写串口，
	 * 不能用 printf_debug（它内部要拿互斥量，在异常上下文里不安全）。 */
	bsp_uart_write((const uint8_t *)"\r\n!! STACK OVERFLOW: ", 22U);
	if (NULL != pcTaskName) {
		bsp_uart_write((const uint8_t *)pcTaskName, (uint16_t)strlen(pcTaskName));
	}
	bsp_uart_write((const uint8_t *)"\r\n", 2U);
	__disable_irq();
	for (;;) {
	}
}
/*-----------------------------------------------------------*/

