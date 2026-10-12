/*!
    \file    bsp_motor.h
    \brief   电机 H 桥驱动（BTN8982 ×2）—— 只负责"把桥臂驱成什么状态"
    \note    本层不含任何业务含义（不知道什么是开阀/关阀/到位），
             只提供：转向 + 占空比 + 停止方式。业务判断放 app 层。
*/

#ifndef BSP_MOTOR_H
#define BSP_MOTOR_H

#include <stdint.h>
#include <stdbool.h>
/* 引脚、占空比量程等硬件参数都在 bsp_config.h；这里引入是为了把量程暴露给上层 */
#include "bsp_config.h"

/* 占空比满量程（千分比）。上层做输入校验时用它，避免各处再写一份 1000 */
#define BSP_MOTOR_DUTY_MAX      MOTOR_DUTY_MAX

/* 电机转向/驱动方式 */
typedef enum {
    MOTOR_DIR_STOP = 0,     /* 两半桥断开（输出高阻），电机自由滑行 */
    MOTOR_DIR_FORWARD,      /* IN1 输出 PWM、IN2 恒低 —— 转向 A */
    MOTOR_DIR_REVERSE,      /* IN2 输出 PWM、IN1 恒低 —— 转向 B */
    MOTOR_DIR_BRAKE,        /* 两半桥低边同时导通，绕组短路制动 */
    MOTOR_DIR_COUNT         /* 哨兵：转向取值个数。新增转向必须插在它前面，别加在它后面 */
} motor_dir_t;

/* 驱动器当前状态 */
typedef enum {
    MOTOR_STATE_UNINIT = 0, /* 未初始化，或引脚未经原理图核实（此时不动任何 IO） */
    MOTOR_STATE_COAST,      /* 输出高阻，自由滑行 */
    MOTOR_STATE_RUN,        /* 正在输出 PWM */
    MOTOR_STATE_BRAKE       /* 制动中 */
} motor_state_t;

/* 初始化：配置 INH/IN1/IN2 引脚与 PWM 定时器，上电即处于"高阻断开"安全态。
 * 若 BSP_MOTOR_PINS_CONFIRMED == 0，则不做任何硬件配置，状态停在 MOTOR_STATE_UNINIT。 */
void bsp_motor_init(void);

/* 是否可用（引脚已核实且初始化成功） */
bool bsp_motor_is_ready(void);

/* 启动：dir 只能是 FORWARD / REVERSE；duty 为千分比 0~MOTOR_DUTY_MAX。
 * duty = 0 等价于停机。返回 false 表示参数非法或驱动器未就绪。
 * 说明：换向时会先断开输出再切方向，避免高速反接的电流冲击。 */
bool bsp_motor_run(motor_dir_t dir, uint16_t duty);

/* 运行中调速：仅在 RUN 状态有效，非运行态调用被忽略 */
void bsp_motor_set_duty(uint16_t duty);

/* 滑行停车：先断 INH 再清 PWM，电机靠惯性自由停止 */
void bsp_motor_stop(void);

/* 制动停车：IN1=IN2=0 且 INH=1，两低边导通把绕组短路 */
void bsp_motor_brake(void);

/* 状态查询 */
motor_dir_t   bsp_motor_get_dir(void);
motor_state_t bsp_motor_get_state(void);
uint16_t      bsp_motor_get_duty(void);

#endif /* BSP_MOTOR_H */
