/*!
    \file    bsp_uart.c
    \brief   调试串口驱动实现（USART0：PC4=TX / PC5=RX，AF7）
    \note    实例与引脚怎么定的：
             同产品既有工程 Vertical_pipe_valve 的 BSP/usart.c 里
             "trea_uart_init()" 用的就是 USART0 + GPIOC PIN_4/PIN_5 + GPIO_AF_7，
             它的 gd32l23x_it.c 里 USART0_IRQHandler 注释为 "debug serial RX"，
             与《GD32L233xx 数据手册》端口C复用功能表（AF7 = USART0_TX/RX）一致。
             注意别和 4G 模块搞混：4G 走的是 LPUART（PD8/PD9，即 LP_TXD/LP_RXD）。
    \note    为什么中断只做"搬字节"：
             串口接收是"随时可能来"的事件，必须用中断保证不丢字节；
             但解析、执行、打印这些事放中断里会拉长中断时间、影响系统实时性，
             所以 ISR 只把字节推进 FIFO，任务侧慢慢消费。
*/

#include <stddef.h>

#include "bsp_uart.h"
#include "bsp_config.h"

/* 发送等待上限：正常几十个周期就发完了；这里给一个大数，
 * 万一外设时钟没开或引脚被占，也只是丢字节，不会把任务卡死在这里。 */
#define BSP_UART_TX_GUARD   (0x000FFFFFU)

/* ---------------- 接收 FIFO（单生产者 ISR / 单消费者任务）---------------- */
/* 一写一读的两个索引，天然无锁：ISR 只动 head，任务只动 tail */
static volatile uint16_t s_rx_head = 0U;
static volatile uint16_t s_rx_tail = 0U;
static uint8_t           s_rx_fifo[BSP_UART_RX_FIFO_SIZE];

static bool s_uart_ready = false;

/* 接收诊断计数：把"中断收的"和"轮询兜底收的"分开记。
 * 一旦 rx_poll 涨而 rx_isr 不涨，就说明中断路径没进来。 */
static volatile uint32_t s_rx_isr_cnt  = 0U;
static volatile uint32_t s_rx_poll_cnt = 0U;
static volatile uint32_t s_rx_err_cnt  = 0U;
static volatile uint8_t  s_rx_last     = 0U;

/* ---------------- 内部函数 ---------------- */

static void uart_gpio_config(void)
{
    /* TX/RX 都配成复用推挽；RX 加内部上拉，空闲时保持高电平，抗干扰 */
    gpio_mode_set(BSP_UART_GPIO_PORT, GPIO_MODE_AF, GPIO_PUPD_PULLUP,
                  BSP_UART_TX_PIN | BSP_UART_RX_PIN);
    gpio_output_options_set(BSP_UART_GPIO_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_10MHZ,
                            BSP_UART_TX_PIN | BSP_UART_RX_PIN);
    gpio_af_set(BSP_UART_GPIO_PORT, BSP_UART_AF, BSP_UART_TX_PIN | BSP_UART_RX_PIN);
}

static void uart_periph_config(void)
{
    usart_deinit(BSP_UART);
    usart_baudrate_set(BSP_UART, BSP_UART_BAUDRATE);
    usart_word_length_set(BSP_UART, USART_WL_8BIT);
    usart_stop_bit_set(BSP_UART, USART_STB_1BIT);
    usart_parity_config(BSP_UART, USART_PM_NONE);
    usart_transmit_config(BSP_UART, USART_TRANSMIT_ENABLE);
    usart_receive_config(BSP_UART, USART_RECEIVE_ENABLE);
    usart_enable(BSP_UART);
}

/* ---------------- 对外接口 ---------------- */

void bsp_uart_init(void)
{
    s_rx_head = 0U;
    s_rx_tail = 0U;

    rcu_periph_clock_enable(BSP_UART_GPIO_RCU);
    rcu_periph_clock_enable(BSP_UART_RCU);

    uart_gpio_config();
    uart_periph_config();

    /* 中断优先级数值 ≥ 5（configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY），
     * 这样将来若要在 ISR 里调用 FromISR 的 FreeRTOS API 也仍然合法 */
    nvic_irq_enable(BSP_UART_IRQ, BSP_UART_IRQ_PRIO);
    usart_interrupt_enable(BSP_UART, USART_INT_RBNE);

    s_uart_ready = true;
}

bool bsp_uart_is_ready(void)
{
    return s_uart_ready;
}

void bsp_uart_putc(uint8_t ch)
{
    uint32_t guard = BSP_UART_TX_GUARD;

    if (!s_uart_ready) {
        return;
    }
    /* 先等发送寄存器空，再写数据：保证上一个字节已经进了移位寄存器 */
    while ((RESET == usart_flag_get(BSP_UART, USART_FLAG_TBE)) && (0U != guard)) {
        guard--;
    }
    usart_data_transmit(BSP_UART, (uint32_t)ch);
}

void bsp_uart_write(const uint8_t *data, uint16_t len)
{
    uint16_t i;

    if ((NULL == data) || (!s_uart_ready)) {
        return;
    }
    for (i = 0U; i < len; i++) {
        bsp_uart_putc(data[i]);
    }
}

bool bsp_uart_read_byte(uint8_t *ch)
{
    if ((NULL == ch) || (!s_uart_ready)) {
        return false;
    }
    if (s_rx_tail == s_rx_head) {
        return false;                       /* 空 */
    }
    *ch = s_rx_fifo[s_rx_tail];
    s_rx_tail = (uint16_t)((s_rx_tail + 1U) % BSP_UART_RX_FIFO_SIZE);
    return true;
}

uint16_t bsp_uart_rx_count(void)
{
    return (uint16_t)((s_rx_head + BSP_UART_RX_FIFO_SIZE - s_rx_tail) % BSP_UART_RX_FIFO_SIZE);
}

bool bsp_uart_tx_idle(void)
{
    return (RESET != usart_flag_get(BSP_UART, USART_FLAG_TC));
}

/* ---------------- 接收内部处理 ---------------- */

/* 把一个字节放进 FIFO（中断路径和轮询路径共用） */
static void uart_accept_byte(uint8_t data)
{
    uint16_t next = (uint16_t)((s_rx_head + 1U) % BSP_UART_RX_FIFO_SIZE);

    s_rx_last = data;
    if (next != s_rx_tail) {
        s_rx_fifo[s_rx_head] = data;
        s_rx_head = next;
    }
    /* FIFO 满了就丢掉这个字节：宁可丢数据，也不能在中断里阻塞 */
}

/* 统计一次溢出：读一次数据寄存器即可清 ORERR（有效字节已被取走，不会误吃） */
static void uart_count_overrun(void)
{
    if (RESET != usart_flag_get(BSP_UART, USART_FLAG_ORERR)) {
        (void)usart_data_receive(BSP_UART);
        s_rx_err_cnt++;
    }
}

void bsp_uart_poll_rx(void)
{
    if (!s_uart_ready) {
        return;
    }

    /* 先屏蔽 RBNE 中断：避免"刚查完标志、中断就把字节取走"的竞态。
     * 屏蔽期间到达的字节只会让 RBNE 保持置位，等下面读走即可，不会丢。 */
    usart_interrupt_disable(BSP_UART, USART_INT_RBNE);

    if (RESET != usart_flag_get(BSP_UART, USART_FLAG_RBNE)) {
        uart_accept_byte((uint8_t)usart_data_receive(BSP_UART));
        s_rx_poll_cnt++;
    }
    uart_count_overrun();

    usart_interrupt_enable(BSP_UART, USART_INT_RBNE);
}

void bsp_uart_get_diag(bsp_uart_diag_t *out)
{
    if (NULL == out) {
        return;
    }
    out->rx_isr     = s_rx_isr_cnt;
    out->rx_poll    = s_rx_poll_cnt;
    out->err        = s_rx_err_cnt;
    out->last       = s_rx_last;
    out->flag_rbne  = (RESET != usart_flag_get(BSP_UART, USART_FLAG_RBNE))  ? 1U : 0U;
    out->flag_orerr = (RESET != usart_flag_get(BSP_UART, USART_FLAG_ORERR)) ? 1U : 0U;
    out->flag_ferr  = (RESET != usart_flag_get(BSP_UART, USART_FLAG_FERR))  ? 1U : 0U;
}

/* ---------------- 中断服务函数（由本模块负责，不外放）----------------
 * 只做三件事：读数据、推进 head、满了就丢。
 * 不在这里做任何解析、不调用任何 RTOS API（所以任何优先级下都安全）。
 */
void BSP_UART_IRQ_HANDLER(void)
{
    if (RESET != usart_flag_get(BSP_UART, USART_FLAG_RBNE)) {
        uart_accept_byte((uint8_t)usart_data_receive(BSP_UART));    /* 读走即清 RBNE */
        s_rx_isr_cnt++;
    }
    uart_count_overrun();
}
