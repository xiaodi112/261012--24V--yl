/*!
    \file    bsp_config.h
    \brief   板级硬件配置：所有"接在哪个引脚、用哪个外设实例"的信息集中在此
    \note    本文件是 bsp 层唯一的硬件收口点。换板子/改板只改这里，
             驱动代码里不允许出现裸引脚号或裸外设实例名。
*/

#ifndef BSP_CONFIG_H
#define BSP_CONFIG_H

#include "gd32l23x.h"

/* ============================== 时钟 ============================== */
/* 由 system_gd32l23x.c 配置：AHB = SYSCLK，APB2 = AHB，APB1 = AHB/2 */
#define BSP_SYSCLK_HZ               (64000000U)     /* 系统时钟 64MHz（IRC16M×4） */
#define BSP_TIMER_CLK_HZ            (64000000U)     /* APB1 = 32MHz，定时器时钟 = 2×APB1 = 64MHz */

/* ==================== 电机 H 桥（BTN8982 ×2，DM1/DM2）====================
 *
 * 引脚分配（硬件确认；复用功能号按《GD32L233xx 数据手册》端口复用功能表核对）：
 *   M_IN1 = PA6   半桥 1 的 IN 脚 → TIMER2_CH0（AF1）
 *   M_IN2 = PA7   半桥 2 的 IN 脚 → TIMER2_CH1（AF1）
 *   M_INH = PB1   半桥使能（INH）：低电平 = 输出高阻
 *   M_IS  = PB0   电流镜输出（模拟量，需 ADC 采样）——本驱动不用，留给过流保护
 *   V24_EN= PA8   半桥 24V 供电开关：高电平才给半桥给电
 *
 * IN 脚逻辑：IN = 1 → 该半桥高边导通；IN = 0 → 低边导通
 *   故 IN1=1/IN2=0 为正向，IN1=0/IN2=1 为反向，两路同为 0 即低边制动
 * 使能与供电是两路独立信号：INH 决定桥臂能不能输出，V24_EN 决定桥臂有没有电，
 *   两者必须同开同关，由 bsp_motor.c 的 motor_enable_output() 统一保证。
 */

/* 电机侧引脚已确认，允许驱动真正操作 GPIO/PWM */
#define BSP_MOTOR_PINS_CONFIRMED    (1U)

/* --- 半桥 1 的 IN 脚（M_IN1）：TIMER2_CH0 --- */
#define MOTOR_IN1_PORT              GPIOA
#define MOTOR_IN1_PIN               GPIO_PIN_6
#define MOTOR_IN1_AF                GPIO_AF_1
#define MOTOR_IN1_RCU               RCU_GPIOA

/* --- 半桥 2 的 IN 脚（M_IN2）：TIMER2_CH1 --- */
#define MOTOR_IN2_PORT              GPIOA
#define MOTOR_IN2_PIN               GPIO_PIN_7
#define MOTOR_IN2_AF                GPIO_AF_1
#define MOTOR_IN2_RCU               RCU_GPIOA

/* --- 半桥使能脚（M_INH）：PB1，低电平输出高阻；原理图带 10K 下拉，复位即安全态 --- */
#define MOTOR_INH_PORT              GPIOB
#define MOTOR_INH_PIN               GPIO_PIN_1
#define MOTOR_INH_RCU               RCU_GPIOB

/* --- 半桥 24V 供电开关（V24_EN）：PA8，高电平给电 --- */
#define MOTOR_PWR_EN_PORT           GPIOA
#define MOTOR_PWR_EN_PIN            GPIO_PIN_8
#define MOTOR_PWR_EN_RCU            RCU_GPIOA

/* 本板使能（INH）与供电（V24_EN）是两路独立信号，必须一起驱动，故置 1。
 * 只有把两者并成一路的板子才置 0。 */
#define BSP_MOTOR_HAS_PWR_GATE      (1U)

/* --- PWM 定时器：PA6/PA7 在芯片上固定对应 TIMER2_CH0/CH1，两路同源同频 --- */
#define MOTOR_PWM_TIMER             TIMER2
#define MOTOR_PWM_TIMER_RCU         RCU_TIMER2
#define MOTOR_PWM_CH_IN1            TIMER_CH_0
#define MOTOR_PWM_CH_IN2            TIMER_CH_1

/* --- PWM 参数 ---
 * 20kHz：高于人耳可听范围，且 BTN8982 建议工作频率 ≤ 25kHz
 * 占空比用"千分比"表示（0~1000），与定时器计数值解耦，方便上层使用 */
#define MOTOR_PWM_FREQ_HZ           (20000U)
#define MOTOR_PWM_PRESCALER         (0U)
#define MOTOR_PWM_PERIOD            ((BSP_TIMER_CLK_HZ / MOTOR_PWM_FREQ_HZ) - 1U)   /* 3199 */
#define MOTOR_DUTY_MAX              (1000U)         /* 占空比满量程（千分比） */

/* ==================== 数字输入（到位 / 限位开关）====================
 *
 * 到位信号就是两个"限位开关"，低电平有效（内部/外部上拉，开关另一端接地）。
 *
 * 引脚取值来自同产品既有工程 Vertical_pipe_valve（BSP/motor.h + motor.c）：
 *     `motor_limit1_reached()` 读 PA11 —— 注释"开阀到位"，判据 `== RESET`（低电平）
 *     `motor_limit2_reached()` 读 PA12 —— 注释"关阀到位"
 *   该工程的电机 IN1/IN2 也在 PA6/PA7（TIMER2_CH0/CH1、AF1），与本板一致，
 *   所以这两个限位脚很可能也相同。
 *
 * !! 仍需你确认 !! 参考工程与本板（YL_conV_V24）的电机使能脚并不相同
 *   （参考工程是 PA5 单脚 M_SLEEP，本板是 PB1(M_INH) + PA8(V24_EN)），
 *   说明两者不是同一版硬件，因此 PA11/PA12 是否为本板的限位脚要按原理图核对。
 *   确认后把 BSP_DIN_PINS_CONFIRMED 置 1 即可启用（默认 0 = 不配置任何 IO）。
 */
#define BSP_DIN_PINS_CONFIRMED      (0U)

#define DIN_LIMIT1_PORT             GPIOA
#define DIN_LIMIT1_PIN              GPIO_PIN_11      /* 开阀到位（参考工程：PA11） */
#define DIN_LIMIT1_RCU              RCU_GPIOA
#define DIN_LIMIT1_ACTIVE_LEVEL     (0U)    /* 0 = 低电平有效（限位开关闭合到地） */

#define DIN_LIMIT2_PORT             GPIOA
#define DIN_LIMIT2_PIN              GPIO_PIN_12      /* 关阀到位（参考工程：PA12） */
#define DIN_LIMIT2_RCU              RCU_GPIOA
#define DIN_LIMIT2_ACTIVE_LEVEL     (0U)

/* 消抖：连续 BSP_DIN_DEBOUNCE_CNT 次采样一致才认可电平变化。
 * 调用方必须以固定周期调用 bsp_din_poll()（建议 2ms），
 * 消抖时间 = BSP_DIN_DEBOUNCE_CNT × 调用周期（默认 5 × 2ms = 10ms）。 */
#define BSP_DIN_DEBOUNCE_CNT        (5U)

/* ==================== 调试串口（USART0）====================
 *
 * 用途：接 USB-TTL 适配器，用串口终端下发电机命令、打印运行信息。
 *
 * 引脚：PC4 = USART0_TX，PC5 = USART0_RX，复用 AF7
 *   依据 1（实测可用的同产品既有工程）：Vertical_pipe_valve 的 BSP/usart.c 中
 *     `#define TREA_UART_DEBUG USART0`，初始化用
 *     `rcu_periph_clock_enable(RCU_USART0)` + `gpio_af_set(GPIOC, GPIO_AF_7, PIN_4/PIN_5)`，
 *     其 gd32l23x_it.c 里 USART0_IRQHandler 的注释就是 "debug serial RX"。
 *   依据 2：《GD32L233xx 数据手册》端口C复用功能表，PC4/PC5 的 AF7 = USART0_TX/RX。
 *
 * 芯片上这两个脚的方向固定：PC4 只能是发送、PC5 只能是接收，无法软件反用。
 * 接线：PC4 → 适配器的 RX，PC5 → 适配器的 TX，且必须共地；不通就对调这两根线。
 *
 * 不要和 4G 模块搞混：4G 走的是 LPUART（PD8/PD9，即原理图的 LP_TXD/LP_RXD 网络），
 *   与调试口不冲突——所以调试口不要再配成 LPUART。
 */
#define BSP_UART                    USART0
#define BSP_UART_RCU                RCU_USART0
#define BSP_UART_IRQ                USART0_IRQn
#define BSP_UART_IRQ_HANDLER        USART0_IRQHandler
#define BSP_UART_IRQ_PRIO           (3U)        /* 0~3：本芯片中断优先级只有 2 位共 4 级。
                                                 * 数值越大越不紧急，调试串口给最低紧急度 3。
                                                 * 注意：与任务优先级方向相反（任务是大者优先）。 */
/* 中断优先级必须落在硬件实际实现的位数之内。
 * 上限从 __NVIC_PRIO_BITS 推导（2 位 -> 0~3），不写死数字：
 * 换芯片只改数据手册对应的那个宏，这里自动跟着变。
 * 越界时触发编译错误，而不是被 NVIC_SetPriority 静默截断成另一个优先级。 */
#define BSP_IRQ_PRIO_MAX            ((1U << __NVIC_PRIO_BITS) - 1U)


#define BSP_CONFIG_STATIC_ASSERT(cond, tag) \
    enum { bsp_config_static_assert_##tag = 1 / ((cond) ? 1 : 0) }

BSP_CONFIG_STATIC_ASSERT(BSP_UART_IRQ_PRIO <= BSP_IRQ_PRIO_MAX, uart_irq_prio_range);

#define BSP_UART_GPIO_PORT          GPIOC
#define BSP_UART_GPIO_RCU           RCU_GPIOC
#define BSP_UART_TX_PIN             GPIO_PIN_4
#define BSP_UART_RX_PIN             GPIO_PIN_5
#define BSP_UART_AF                 GPIO_AF_7

#define BSP_UART_BAUDRATE           (115200U)   /* 8N1 */
#define BSP_UART_RX_FIFO_SIZE       (256U)      /* 2 的幂，取模会被编译成位与 */

#endif /* BSP_CONFIG_H */
