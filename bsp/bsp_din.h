/*!
    \file    bsp_din.h
    \brief   带消抖的数字输入驱动（到位/限位开关等开关量）
    \note    本层只负责"读到一个干净的电平"，不判断这个电平代表什么业务含义。
             消抖在驱动层完成，上层直接读稳定值，避免上层各写一套消抖代码。
*/

#ifndef BSP_DIN_H
#define BSP_DIN_H

#include <stdint.h>
#include <stdbool.h>

/* 输入通道（按原理图网络命名，业务上的"开到位/关到位"由 app 层映射） */
typedef enum {
    BSP_DIN_CH_LIMIT_1 = 0,     /* 原理图 Btn1 */
    BSP_DIN_CH_LIMIT_2,         /* 原理图 Btn2 */
    BSP_DIN_CH_NUM
} bsp_din_channel_t;

/* 电平 */
typedef enum {
    BSP_DIN_LEVEL_LOW = 0,
    BSP_DIN_LEVEL_HIGH = 1
} bsp_din_level_t;

/* 初始化：把各通道配成输入（内部上拉，外部还有 10K 上拉 + RC + TVS）。
 * 若 BSP_DIN_PINS_CONFIRMED == 0，则不配置任何 IO，读值恒为"无效"。 */
void bsp_din_init(void);

/* 是否可用（引脚已核实且初始化成功） */
bool bsp_din_is_ready(void);

/* 采样并消抖。必须以固定周期调用（建议 2ms），
 * 消抖时间 = BSP_DIN_DEBOUNCE_CNT × 调用周期，且同一周期内所有通道一起采样。 */
void bsp_din_poll(void);

/* 消抖后的电平 */
bsp_din_level_t bsp_din_level(bsp_din_channel_t ch);

/* 消抖后是否处于"有效电平"（有效电平按各通道硬件接线在 bsp_config.h 中配置） */
bool bsp_din_active(bsp_din_channel_t ch);

/* 读取瞬时电平（未消抖），用于调试/边界场景 */
bsp_din_level_t bsp_din_level_raw(bsp_din_channel_t ch);

#endif /* BSP_DIN_H */
