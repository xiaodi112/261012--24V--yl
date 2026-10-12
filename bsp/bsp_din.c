/*!
    \file    bsp_din.c
    \brief   带消抖的数字输入驱动实现
    \note    消抖算法：连续 N 次采样与当前稳定值不一致才认可变化（N = BSP_DIN_DEBOUNCE_CNT）。
             这样对单个干扰脉冲完全抑制，对真实开关动作只引入 N × 采样周期 的延迟。
             不使用中断，纯轮询——到位开关对响应时间要求是毫秒级，轮询更简单可靠；
             将来若需要"停机时靠开关唤醒"，再单独给这两个脚加 EXTI 唤醒。
*/

#include "bsp_din.h"
#include "bsp_config.h"

/* ---------------- 通道设备表 ---------------- */
typedef struct {
    uint32_t         port;          /* GPIO 端口 */
    uint32_t         pin;           /* 引脚 */
    uint32_t         rcu;           /* 对应端口时钟 */
    bsp_din_level_t  active_level;  /* 硬件有效电平 */
    bsp_din_level_t  stable;        /* 消抖后的稳定电平 */
    uint8_t          count;         /* 连续不一致采样计数 */
} bsp_din_dev_t;

static bsp_din_dev_t s_din[BSP_DIN_CH_NUM] = {
    { DIN_LIMIT1_PORT, DIN_LIMIT1_PIN, DIN_LIMIT1_RCU,
      (bsp_din_level_t)DIN_LIMIT1_ACTIVE_LEVEL, BSP_DIN_LEVEL_LOW, 0U },
    { DIN_LIMIT2_PORT, DIN_LIMIT2_PIN, DIN_LIMIT2_RCU,
      (bsp_din_level_t)DIN_LIMIT2_ACTIVE_LEVEL, BSP_DIN_LEVEL_LOW, 0U },
};

/* 设备表长度必须与通道枚举一致：BSP_DIN_CH_NUM 加了通道却忘了补表项时，
 * 数组会多出全 0 的项，而 0 就是 GPIOA 的 pin0 —— 初始化会去配一个不存在的引脚。
 * 这种错误编译器不会报（C 允许初始化项少于数组长度），只能在这里拦住。 */
#define BSP_DIN_STATIC_ASSERT(cond, tag) \
    enum { bsp_din_static_assert_##tag = 1 / ((cond) ? 1 : 0) }

BSP_DIN_STATIC_ASSERT((sizeof(s_din) / sizeof(s_din[0])) == (uint32_t)BSP_DIN_CH_NUM, table_size);

static bool s_din_ready = false;

/* ---------------- 内部函数 ---------------- */

/* 读瞬时电平（直接读引脚，不做任何状态判断） */
static bsp_din_level_t din_read_pin(const bsp_din_dev_t *dev)
{
    return (RESET != gpio_input_bit_get(dev->port, dev->pin))
           ? BSP_DIN_LEVEL_HIGH : BSP_DIN_LEVEL_LOW;
}

/* 通道号合法性检查：越界一律按"最低优先级"返回，避免野指针 */
static bool din_ch_valid(bsp_din_channel_t ch)
{
    return ((uint32_t)ch < (uint32_t)BSP_DIN_CH_NUM);
}

/* ---------------- 对外接口 ---------------- */

void bsp_din_init(void)
{
#if (0U == BSP_DIN_PINS_CONFIRMED)
    /* 引脚未经原理图核实：不配置任何 IO，读值恒为无效 */
    s_din_ready = false;
#else
    uint32_t i;

    for (i = 0U; i < (uint32_t)BSP_DIN_CH_NUM; i++) {
        rcu_periph_clock_enable(s_din[i].rcu);
        /* 开漏/推挽无关，输入模式；内部上拉提高抗干扰余量（外部另有 10K 上拉） */
        gpio_mode_set(s_din[i].port, GPIO_MODE_INPUT, GPIO_PUPD_PULLUP, s_din[i].pin);
        /* 以初始化时的实际电平作为初值，上层无需等消抖就能拿到真实状态 */
        s_din[i].stable = din_read_pin(&s_din[i]);
        s_din[i].count  = 0U;
    }
    s_din_ready = true;
#endif
}

bool bsp_din_is_ready(void)
{
    return s_din_ready;
}

void bsp_din_poll(void)
{
    uint32_t i;

    if (!s_din_ready) {
        return;
    }

    for (i = 0U; i < (uint32_t)BSP_DIN_CH_NUM; i++) {
        bsp_din_dev_t  *dev = &s_din[i];
        bsp_din_level_t raw = din_read_pin(dev);

        if (raw == dev->stable) {
            dev->count = 0U;                /* 电平一致，计数器清零 */
            continue;
        }
        if (dev->count < BSP_DIN_DEBOUNCE_CNT) {
            dev->count++;
        }
        if (dev->count >= BSP_DIN_DEBOUNCE_CNT) {
            dev->stable = raw;              /* 连续 N 次一致，认可新电平 */
            dev->count  = 0U;
        }
    }
}

bsp_din_level_t bsp_din_level(bsp_din_channel_t ch)
{
    if ((!s_din_ready) || (!din_ch_valid(ch))) {
        return BSP_DIN_LEVEL_LOW;
    }
    return s_din[ch].stable;
}

bool bsp_din_active(bsp_din_channel_t ch)
{
    if ((!s_din_ready) || (!din_ch_valid(ch))) {
        return false;
    }
    return (s_din[ch].stable == s_din[ch].active_level);
}

bsp_din_level_t bsp_din_level_raw(bsp_din_channel_t ch)
{
    if ((!s_din_ready) || (!din_ch_valid(ch))) {
        return BSP_DIN_LEVEL_LOW;
    }
    return din_read_pin(&s_din[ch]);
}
