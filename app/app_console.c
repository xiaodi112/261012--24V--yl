/*!
    \file    app_console.c
    \brief   调试串口控制台实现（命令解析 + printf_debug 打印）
    \note    设计要点：
             1) 命令解析放在任务里做，串口中断只搬字节，互不阻塞；
             2) 打印统一走 printf_debug()：先整块格式化、再一次性发出，并用互斥量保护，
                多任务打印既不会交叉错乱，也不会像重定向 fputc 那样逐字符打断；
             3) 命令只做"最小可用"集：开关停、清故障、调速、查状态、帮助。
*/

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

#include "app_console.h"
#include "app_motor.h"
#include "bsp_uart.h"

/* ============================ 内部状态 ============================ */
#define CONSOLE_LINE_MAX    (48U)       /* 一行命令最长字符数（含结尾 '\0'） */

static char              s_line[CONSOLE_LINE_MAX];
static uint8_t           s_line_len;
static SemaphoreHandle_t s_tx_mutex;    /* 串口发送互斥量 */

/* ============================ 调试打印 ============================ */
/* 为什么不重定向 printf/fputc：
 *   fputc 是"一个字符一次调用"，格式化中间结果也逐字符往外送，多任务时还会互相插花；
 *   这里改成"先在缓冲里格式化好，再整块发送"——一次调用只锁一次、只发一次，
 *   输出天然成行，也顺便把串口发送对其它任务的打扰降到最低。
 * 注意：只能在任务上下文调用（内部用了互斥量），不要放在中断里。 */
int printf_debug(const char *fmt, ...)
{
    static char buf[256];           /* static：不占用调用者栈；由互斥量保证串行使用 */
    va_list     args;
    int         n;
    bool        locked = false;

    if (NULL == fmt) {
        return -1;
    }

    /* 调度器启动前互斥量还没建，此时不可能有并发，直接发 */
    if (NULL != s_tx_mutex) {
        if (pdTRUE != xSemaphoreTake(s_tx_mutex, portMAX_DELAY)) {
            return -1;
        }
        locked = true;
    }

    va_start(args, fmt);
    n = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    if (n > 0) {
        /* vsnprintf 返回"本该写入的长度"，可能超过缓冲；截断后只发真正存在的内容，
         * 所以不需要 memset 清缓冲 */
        if (n > (int)sizeof(buf) - 1) {
            n = (int)sizeof(buf) - 1;
        }
        bsp_uart_write((const uint8_t *)buf, (uint16_t)n);
    }

    if (locked) {
        (void)xSemaphoreGive(s_tx_mutex);
    }
    return n;
}

/* ============================ 基础工具 ============================ */

/* 不区分大小写比较（MicroLIB 没有 strcasecmp） */
static bool str_eq_ci(const char *a, const char *b)
{
    char ca;
    char cb;

    while (('\0' != *a) && ('\0' != *b)) {
        ca = *a;
        cb = *b;
        if ((ca >= 'A') && (ca <= 'Z')) {
            ca = (char)(ca - 'A' + 'a');
        }
        if ((cb >= 'A') && (cb <= 'Z')) {
            cb = (char)(cb - 'A' + 'a');
        }
        if (ca != cb) {
            return false;
        }
        a++;
        b++;
    }
    return (*a == *b);
}

/* 把一行按第一个空白拆成"命令 + 参数"，返回参数指针（没有参数时返回 NULL）。
 * 会原地把分隔的空白改成 '\0'。 */
static char *split_arg(char *line)
{
    char *p = line;

    while (('\0' != *p) && (' ' != *p) && ('\t' != *p)) {
        p++;
    }
    if ('\0' == *p) {
        return NULL;
    }
    *p = '\0';
    p++;
    while ((' ' == *p) || ('\t' == *p)) {
        p++;
    }
    return ('\0' == *p) ? NULL : p;
}

/* 十进制解析，遇到非数字即失败；限制上限避免溢出 */
static bool parse_u32(const char *s, uint32_t *out)
{
    uint32_t v = 0U;

    if ((NULL == s) || ('\0' == *s)) {
        return false;
    }
    while ('\0' != *s) {
        if ((*s < '0') || (*s > '9')) {
            return false;
        }
        v = (v * 10U) + (uint32_t)(*s - '0');
        if (v > 100000U) {
            return false;
        }
        s++;
    }
    *out = v;
    return true;
}

/* ============================ 命令实现 ============================ */

static void console_show_help(void)
{
    printf_debug("格式: <命令> [参数]   大小写不限，回车结束，可用退格\r\n");
    printf_debug("commands:\r\n");
    printf_debug("  open        开阀（正转，到位自动停）\r\n");
    printf_debug("  close       关阀（反转，到位自动停）\r\n");
    printf_debug("  stop        立即停止\r\n");
    printf_debug("  clear       清除故障\r\n");
    printf_debug("  duty <n>    设置默认占空比，n = 0~1000（千分比）\r\n");
    printf_debug("  status      查看状态/故障/到位信号（别名 st）\r\n");
    printf_debug("  uart        查看串口接收诊断（别名 diag）\r\n");
    printf_debug("  help        显示本帮助（别名 ?）\r\n");
    printf_debug("说明: 命令回 \"... accepted\" 只表示已受理，实际结果看 status\r\n");
}

static void console_show_status(void)
{
    /* volatile 的实时快照先读到本地，保证这一屏数据是同一时刻的 */
    app_motor_state_t st  = g_motor_diag.state;
    app_motor_fault_t flt = g_motor_diag.fault;
    uint8_t           dir = g_motor_diag.dir;
    uint16_t          duty = g_motor_diag.duty;
    uint32_t          run_ms = g_motor_diag.running_ms;

    printf_debug("state=%s  fault=%s\r\n",
           app_motor_state_name(st),
           app_motor_fault_name(flt));
    printf_debug("dir=%s  duty=%u/1000  running=%ums\r\n",
           app_motor_dir_name(dir),
           (unsigned)duty, (unsigned)run_ms);
    printf_debug("limit: open=%u close=%u  raw=%u/%u\r\n",
           (unsigned)g_motor_diag.limit_open,
           (unsigned)g_motor_diag.limit_close,
           (unsigned)g_motor_diag.raw_open,
           (unsigned)g_motor_diag.raw_close);
    printf_debug("ready: motor=%u din=%u  loop=%u  default_duty=%u\r\n",
           (unsigned)g_motor_diag.motor_ready,
           (unsigned)g_motor_diag.din_ready,
           (unsigned)g_motor_diag.poll_cnt,
           (unsigned)app_motor_get_duty_default());
}

/* 串口接收诊断：用来区分"引脚上没数据"和"中断没进来" */
static void console_show_uart(void)
{
    bsp_uart_diag_t d;

    bsp_uart_get_diag(&d);
    printf_debug("uart: ready=%u  fifo=%u  last=0x%02X\r\n",
                 (unsigned)bsp_uart_is_ready(),
                 (unsigned)bsp_uart_rx_count(),
                 (unsigned)d.last);
    printf_debug("  rx_isr=%u  rx_poll=%u  err=%u\r\n",
                 (unsigned)d.rx_isr, (unsigned)d.rx_poll, (unsigned)d.err);
    printf_debug("  flags: rbne=%u orerr=%u ferr=%u\r\n",
                 (unsigned)d.flag_rbne, (unsigned)d.flag_orerr, (unsigned)d.flag_ferr);
    printf_debug("  判读: 两者都为 0 -> 引脚上没收到数据(查接线/引脚占用)\r\n");
    printf_debug("        rx_poll 涨而 rx_isr 不涨 -> 中断路径没进来\r\n");
}

static void console_do_motor_cmd(app_motor_cmd_t cmd, const char *ok_text)
{
    if (app_motor_request(cmd)) {
        printf_debug("%s\r\n", ok_text);
    } else {
        printf_debug("FAILED: command queue not ready\r\n");
    }
}

static void console_exec(char *line)
{
    char *arg = split_arg(line);

    if (str_eq_ci(line, "help") || (0 == strcmp(line, "?"))) {
        console_show_help();
        return;
    }
    if (str_eq_ci(line, "status") || str_eq_ci(line, "st")) {
        console_show_status();
        return;
    }
    if (str_eq_ci(line, "uart") || str_eq_ci(line, "diag")) {
        console_show_uart();
        return;
    }
    if (str_eq_ci(line, "open")) {
        console_do_motor_cmd(APP_MOTOR_CMD_OPEN, "OPEN accepted");
        return;
    }
    if (str_eq_ci(line, "close")) {
        console_do_motor_cmd(APP_MOTOR_CMD_CLOSE, "CLOSE accepted");
        return;
    }
    if (str_eq_ci(line, "stop")) {
        console_do_motor_cmd(APP_MOTOR_CMD_STOP, "STOP accepted");
        return;
    }
    if (str_eq_ci(line, "clear")) {
        console_do_motor_cmd(APP_MOTOR_CMD_CLEAR_FAULT, "CLEAR accepted");
        return;
    }
    if (str_eq_ci(line, "duty")) {
        uint32_t v;

        if ((NULL == arg) || (!parse_u32(arg, &v))) {
            printf_debug("usage: duty <0..1000>\r\n");
            return;
        }
        /* 越界由驱动侧统一夹紧，这里回读夹紧后的值，避免两处各写一份上限 */
        app_motor_set_duty_default((uint16_t)v);
        printf_debug("default duty = %u/1000\r\n", (unsigned)app_motor_get_duty_default());
        return;
    }

    printf_debug("unknown command: %s  (type help)\r\n", line);
}

/* ============================ 行输入处理 ============================ */

static void console_prompt(void)
{
    printf_debug("> ");
}

/* 处理一个收到的字节：回车执行、退格删除、其它回显 */
static void console_handle_byte(uint8_t ch)
{
    if (('\r' == ch) || ('\n' == ch)) {
        printf_debug("\r\n");
        if (0U != s_line_len) {
            s_line[s_line_len] = '\0';
            console_exec(s_line);
            s_line_len = 0U;
        }
        console_prompt();
        return;
    }

    if ((0x08U == ch) || (0x7FU == ch)) {           /* 退格 */
        if (0U != s_line_len) {
            s_line_len--;
            printf_debug("\b \b");                        /* 终端上擦掉一个字符 */
        }
        return;
    }

    if (ch < 0x20U) {                               /* 其它控制字符忽略 */
        return;
    }

    if (s_line_len < (CONSOLE_LINE_MAX - 1U)) {
        s_line[s_line_len] = (char)ch;
        s_line_len++;
        printf_debug("%c", ch);                           /* 回显 */
    }
    /* 行满了以后的字符直接丢弃：命令行很短，不做更复杂的编辑功能 */
}

/* ============================ 对外接口 ============================ */

void app_console_init(void)
{
    s_line_len = 0U;
    s_tx_mutex = NULL;                              /* 先置空：横幅走直接发送 */

    bsp_uart_init();

    /* 开机横幅：既是使用提示，也是"串口通了"的直观自检 */
    printf_debug("\r\n=== GD32L233RCT6 motor console ===\r\n");
    printf_debug("USART0 115200 8N1, type 'help' for commands\r\n");
    console_prompt();

    /* 互斥量在横幅之后创建：这样创建前的 printf 不会去碰还没建好的互斥量 */
    s_tx_mutex = xSemaphoreCreateMutex();
}

void app_console_task(void *pv_para)
{
    TickType_t last_wake;
    uint8_t    ch;

    (void)pv_para;
    last_wake = xTaskGetTickCount();

    for (;;) {
        /* 轮询兜底：正常时中断已把字节取进 FIFO，这一步取不到东西；
         * 万一中断路径没进来，靠它仍能收到命令（诊断里 rx_poll 会体现） */
        bsp_uart_poll_rx();

        /* 一次把 FIFO 里的字节全部消化掉，避免输入快时积压 */
        while (bsp_uart_read_byte(&ch)) {
            console_handle_byte(ch);
        }

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(APP_CONSOLE_POLL_MS));
    }
}
