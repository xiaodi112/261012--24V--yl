/*
 * FreeRTOS V202112.00
 * Copyright (C) 2020 Amazon.com, Inc. or its affiliates.  All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * http://www.FreeRTOS.org
 * http://aws.amazon.com/freertos
 *
 * 1 tab == 4 spaces!
 */

/******************************************************************************
	本文件各项 configXXX 宏的含义与取值说明，见 FreeRTOS 官方文档：
	http://www.freertos.org/a00110.html
******************************************************************************/

#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

/*-----------------------------------------------------------
 * 与本应用相关的配置项。
 *
 * 这些取值要按你自己的硬件和应用需求来调整。
 *
 * 每个参数的含义，在 FreeRTOS 官方文档的
 * "Configuration（配置）"章节里有逐项说明：
 * http://www.freertos.org/a00110.html
 *----------------------------------------------------------*/
#include "gd32l23x.h"

extern uint32_t SystemCoreClock;

/* Cortex-M23 移植层相关配置 */
#define configENABLE_MPU								0
#define configENABLE_FPU								0
#define configENABLE_TRUSTZONE							0

/* 调度器行为相关配置 */
#define configUSE_PORT_OPTIMISED_TASK_SELECTION			0
#define configUSE_PREEMPTION							1
#define configUSE_TIME_SLICING							1
#define configMAX_PRIORITIES							( 5 )
#define configIDLE_SHOULD_YIELD							1
#define configUSE_16_BIT_TICKS							0 /* 仅用于 8 位和 16 位硬件 */

/* 硬件与内存占用相关配置 */
#define configCPU_CLOCK_HZ								SystemCoreClock
#define configMINIMAL_STACK_SIZE						( ( uint16_t ) 128 )
#define configMINIMAL_SECURE_STACK_SIZE					( 1024 )
#define configMAX_TASK_NAME_LEN							( 12 )
#define configTOTAL_HEAP_SIZE							( ( size_t ) ( 10 * 1024 ) )

/* 功能裁剪开关（1 = 启用该功能，0 = 关闭） */
#define configUSE_MUTEXES								1
#define configUSE_TICKLESS_IDLE							1
#define configUSE_APPLICATION_TASK_TAG					0
#define configUSE_NEWLIB_REENTRANT						0
#define configUSE_CO_ROUTINES							0
#define configUSE_COUNTING_SEMAPHORES					1
#define configUSE_RECURSIVE_MUTEXES						1
#define configUSE_QUEUE_SETS							0
#define configUSE_TASK_NOTIFICATIONS					1
#define configUSE_TRACE_FACILITY						1

/* 钩子（回调）函数开关 */
#define configUSE_IDLE_HOOK								0
#define configUSE_TICK_HOOK								0
#define configUSE_MALLOC_FAILED_HOOK					0

/* 调试与优化辅助相关配置 */
#define configCHECK_FOR_STACK_OVERFLOW					2
/* 断言失败处理：实现在 gd32_sys/gd32l23x_it.c，用调试串口把
 * "断言失败的文件名:行号"直接打出来再停机（不经过 printf / 堆 / RTOS）。
 * 这样内核里的断言（比如调度器启动阶段的配置错误）不会再表现为无声卡死。 */
void app_assert_fail( const char *file, int line );
#define configASSERT( x )								if( ( x ) == 0 ) { app_assert_fail( __FILE__, __LINE__ ); }
#define configQUEUE_REGISTRY_SIZE						0

/* 软件定时器配置 */
#define configUSE_TIMERS								1
#define configTIMER_TASK_PRIORITY						( 3 )
#define configTIMER_QUEUE_LENGTH						5
#define configTIMER_TASK_STACK_DEPTH					( configMINIMAL_STACK_SIZE  )

/* 下面这些 INCLUDE_ 宏设为 1 才会把对应的 API 函数编译进来，设为 0 则排除掉。
 * 注意：只有当链接器不会自动删除"未被引用的函数"时，才必须把它们设为 0。 */
#define INCLUDE_vTaskPrioritySet						1
#define INCLUDE_uxTaskPriorityGet						1
#define INCLUDE_vTaskDelete								1
#define INCLUDE_vTaskCleanUpResources					0
#define INCLUDE_vTaskSuspend							1
#define INCLUDE_vTaskDelayUntil							1
#define INCLUDE_vTaskDelay								1
#define INCLUDE_uxTaskGetStackHighWaterMark				0
#define INCLUDE_xTaskGetIdleTaskHandle					0
#define INCLUDE_eTaskGetState							1
#define INCLUDE_xTaskResumeFromISR						0
#define INCLUDE_xTaskGetCurrentTaskHandle				1
#define INCLUDE_xTaskGetSchedulerState					0
#define INCLUDE_xSemaphoreGetMutexHolder				0
#define INCLUDE_xTimerPendFunctionCall					1

/* 本工程会用到若干"统计信息格式化"函数，它们把 uxTaskGetSystemState()
 * 给出的原始数据格式化成便于阅读的 ASCII 文本。
 * 用法和限制（例如输出缓冲需要多大）见 FreeRTOS/tasks.c 里 vTaskList() 的实现。 */
#define configUSE_STATS_FORMATTING_FUNCTIONS			1

/* FreeRTOS+CLI 命令解释器可用输出缓冲区的长度。
 * 本工程没有使用 FreeRTOS+CLI，保持原值即可。
 * 详见 http://www.FreeRTOS.org/FreeRTOS-Plus/FreeRTOS_Plus_CLI/ */
#define configCOMMAND_INT_MAX_OUTPUT_SIZE				2048

/* 下面是中断优先级相关配置 ......................................... */

/* 如果系统头文件里已经定义了优先级位数，就用它的 */
#ifdef __NVIC_PRIO_BITS
	#define configPRIO_BITS								__NVIC_PRIO_BITS
#else
	#define configPRIO_BITS								3	 /* 3 位 = 8 级中断优先级 */
#endif

/* 调用"设置优先级"函数时能使用的最低中断优先级
 * （中断优先级是数值越大越不紧急，所以 7 是最低优先级）。 */
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY			0x07

/* 允许调用 FreeRTOS"中断安全版"API（带 FromISR 后缀的那些）的最高中断优先级。
 *
 * !! 绝对不要在任何"优先级高于此值"的中断里调用 FromISR 版 API !!
 *    注意方向别搞反：中断优先级是数值越小越紧急，"高于此值"指的是数值比它小。
 *
 * 违反这条会让内核自己的数据结构被破坏，表现为随机死机，且极难定位。 */
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY	5

/* 内核移植层自己使用的中断优先级（PendSV 等）。
 * 这个写法对所有 Cortex-M 移植通用，不依赖任何具体的库函数。 */
#define configKERNEL_INTERRUPT_PRIORITY					( configLIBRARY_LOWEST_INTERRUPT_PRIORITY << ( 8 - configPRIO_BITS ) )

/* !!!! configMAX_SYSCALL_INTERRUPT_PRIORITY 绝对不能设为 0 !!!!
 * 原因见 http://www.FreeRTOS.org/RTOS-Cortex-M3-M4.html */
#define configMAX_SYSCALL_INTERRUPT_PRIORITY			( configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << ( 8 - configPRIO_BITS ) )

/* 这个 #ifdef 是为了防止本文件被 IAR 的汇编文件包含进来 */
#ifndef __IASMARM__

	/* 运行时统计（各任务占用 CPU 的比例）相关配置 */
	#define configGENERATE_RUN_TIME_STATS				0
	#define portCONFIGURE_TIMER_FOR_RUN_TIME_STATS()
	#define portGET_RUN_TIME_COUNTER_VALUE()			0
	#define configTICK_RATE_HZ							( ( TickType_t ) 1000 )

#endif /* __IASMARM__ */

/* 是否启用静态分配。
 * 0 = 只用动态分配：任务的栈和 TCB 都由 pvPortMalloc 从 configTOTAL_HEAP_SIZE
 * 那块堆里取，所以"任务创建失败"通常就是堆不够。 */
#define configSUPPORT_STATIC_ALLOCATION					0

#endif /* FREERTOS_CONFIG_H */
