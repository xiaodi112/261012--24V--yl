/*!
    \file    bsp_uart.h
    \brief   调试串口驱动（USART0：PC4=TX / PC5=RX，AF7）
    \note    本层只负责"收发字节"，不认识任何命令内容；
             命令怎么解析、怎么执行属于 app 层的事。
             接收用中断 + 环形 FIFO：ISR 只把字节存进 FIFO，解析全部在任务里做，
             避免在中断里做长耗时处理。
             另提供 bsp_uart_poll_rx() 作为轮询兜底：正常时中断已把字节取走，
             它取不到东西；万一中断路径有问题，靠它仍能收到命令，
             并且从诊断计数能看出到底是谁在收（rx_isr / rx_poll 分开计）。
*/

#ifndef BSP_UART_H
#define BSP_UART_H

#include <stdint.h>
#include <stdbool.h>

/* 初始化：配引脚复用、USART 参数、接收中断。 */
void bsp_uart_init(void);

/* 串口是否可用 */
bool bsp_uart_is_ready(void);

/* 发送一个字节（阻塞到发送寄存器空为止，带超时保护） */
void bsp_uart_putc(uint8_t ch);

/* 发送一串字节 */
void bsp_uart_write(const uint8_t *data, uint16_t len);

/* 从接收 FIFO 取一个字节；返回 false 表示当前没有数据。
 * FIFO 满时新数据被丢弃（宁可丢字节，也不在中断里阻塞）。 */
bool bsp_uart_read_byte(uint8_t *ch);

/* 当前 FIFO 里还有多少字节未取走 */
uint16_t bsp_uart_rx_count(void);

/* 发送是否空闲（用于将来做 DMA/中断发送时判断） */
bool bsp_uart_tx_idle(void);

/* 轮询兜底收一轮（由任务周期调用，与中断路径共存、不冲突） */
void bsp_uart_poll_rx(void);

/* 接收诊断信息：用来区分"引脚上根本没数据"（接线/引脚被占用）
 * 和"字节到了但中断没进来"（中断配置问题） */
typedef struct {
    uint32_t rx_isr;        /* 中断路径收下的字节数 */
    uint32_t rx_poll;       /* 轮询兜底收下的字节数 */
    uint32_t err;           /* 溢出(ORERR)次数 */
    uint8_t  last;          /* 最近收到的字节 */
    uint8_t  flag_rbne;     /* 查询瞬间的状态标志 */
    uint8_t  flag_orerr;
    uint8_t  flag_ferr;
} bsp_uart_diag_t;

void bsp_uart_get_diag(bsp_uart_diag_t *out);

#endif /* BSP_UART_H */
