/*!
    \file    app_console.h
    \brief   调试串口控制台：解析上位机命令，转成电机动作
    \note    分层：本层负责"人话 → 动作"的翻译（命令解析、回显、打印），
             往下只调用 app_motor_request() 和 bsp_uart_*()，
             不直接碰电机驱动，也不碰寄存器。
*/

#ifndef APP_CONSOLE_H
#define APP_CONSOLE_H

#include <stdint.h>
#include <stdbool.h>

/* 控制台任务参数 */
#define APP_CONSOLE_TASK_PRIO   (2U)
#define APP_CONSOLE_TASK_STACK  (256U)
#define APP_CONSOLE_POLL_MS     (5U)        /* 收包轮询周期，越小越跟手 */

/* 初始化：打开调试串口、建发送互斥、打印开机提示。必须在创建任务前调用。
 * 注意：本函数里会打印横幅，此时互斥量尚未创建，走的是直接发送路径。 */
void app_console_init(void);

/* 控制台任务：收字节 → 组行 → 执行命令 */
void app_console_task(void *pv_para);

/* 调试打印：先格式化到内部缓冲，再整块发出；
 * 比逐字符重定向 printf 快得多（一次锁、一次发送，不分字符打断）。
 * 内部带互斥量，可被多个任务调用；不要在中断里调用。
 * 返回实际写入的字节数（<0 表示格式化失败）。 */
int printf_debug(const char *fmt, ...);

#endif /* APP_CONSOLE_H */
