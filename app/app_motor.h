/*!
    \file    app_motor.h
    \brief   电机应用层：把"开阀/关阀/到位停止/超时保护"的业务逻辑封装成一个任务
    \note    分层关系：本层只调用 bsp_motor / bsp_din 的接口，不碰任何寄存器；
             其它任务（按键、485、4G）通过 app_motor_request() 下发命令，互不直接访问驱动。
*/

#ifndef APP_MOTOR_H
#define APP_MOTOR_H

#include <stdint.h>
#include <stdbool.h>

/* 上层下发的动作命令。
 * 末尾的 _COUNT 是哨兵，用于"取值个数"和越界判断——不要在它后面再加取值，
 * 否则它的值就不再等于取值个数，凡是拿它当上限的地方都会静默失效。 */
typedef enum {
    APP_MOTOR_CMD_OPEN = 0,     /* 开阀（电机正转） */
    APP_MOTOR_CMD_CLOSE,        /* 关阀（电机反转） */
    APP_MOTOR_CMD_STOP,         /* 立即停止 */
    APP_MOTOR_CMD_CLEAR_FAULT,  /* 清除故障，恢复到可用状态 */
    APP_MOTOR_CMD_COUNT
} app_motor_cmd_t;

/* 应用层状态（末尾哨兵规则同上） */
typedef enum {
    APP_MOTOR_IDLE = 0,         /* 空闲，电机已停 */
    APP_MOTOR_OPENING,          /* 正在开阀 */
    APP_MOTOR_CLOSING,          /* 正在关阀 */
    APP_MOTOR_FAULT,            /* 故障，电机已强制停止 */
    APP_MOTOR_STATE_COUNT
} app_motor_state_t;

/* 故障码（末尾哨兵规则同上） */
typedef enum {
    APP_MOTOR_FAULT_NONE = 0,       /* 无故障 */
    APP_MOTOR_FAULT_TIMEOUT,        /* 运行超时仍未收到到位信号 */
    APP_MOTOR_FAULT_DRIVER,         /* 驱动器不可用（引脚未核实 / 初始化失败 / 队列创建失败） */
    APP_MOTOR_FAULT_CONFLICT,       /* 到位信号矛盾：两个到位同时有效，接线或开关异常 */
    APP_MOTOR_FAULT_COUNT
} app_motor_fault_t;

/* 任务参数：改这里即可调整任务优先级/栈/采样周期（栈单位是"字"，4 字节） */
#define APP_MOTOR_TASK_PRIO         (3U)
#define APP_MOTOR_TASK_STACK        (256U)
#define APP_MOTOR_POLL_MS           (2U)        /* 采样周期，同时是输入消抖的时间步长 */

/* 初始化：初始化驱动、创建命令队列。必须在创建任务前调用。 */
void app_motor_init(void);

/* FreeRTOS 任务入口：周期采集到位输入、处理命令、跑状态机 */
void app_motor_task(void *pv_para);

/* 下发命令（任务上下文调用，非阻塞，只保留最新一条命令）。
 * 返回 false 表示队列不可用或命令非法。注意：不可在中断中调用。 */
bool app_motor_request(app_motor_cmd_t cmd);

/* 状态 / 故障 / 到位信号查询（供显示、上报、诊断使用） */
app_motor_state_t app_motor_get_state(void);
app_motor_fault_t app_motor_get_fault(void);
bool              app_motor_limit_open(void);       /* 开到位信号是否有效 */
bool              app_motor_limit_close(void);      /* 关到位信号是否有效 */
bool              app_motor_is_ready(void);         /* 驱动是否可用（引脚已核实） */
uint32_t          app_motor_get_running_ms(void);   /* 本次行程已运行时间(ms)，空闲为 0 */

/* 运行占空比（千分比）：开机取 APP_MOTOR_DUTY_DEFAULT，可用控制台 duty 命令修改。
 * 只影响"下一次启动行程"，正在跑的行程不受影响。 */
void     app_motor_set_duty_default(uint16_t duty);
uint16_t app_motor_get_duty_default(void);

/* 状态 / 故障 / 转向的显示名，供控制台、显示、上报统一取用。
 * 名字表与枚举定义在同一个 .c 里，并用编译期断言绑定条数——
 * 调用方不需要知道枚举有几项，枚举增项时也不会打印出错位的名字。 */
const char *app_motor_state_name(app_motor_state_t st);
const char *app_motor_fault_name(app_motor_fault_t fault);
const char *app_motor_dir_name(uint8_t dir);        /* 入参取自 app_motor_diag_t.dir */

/* ------------------------------------------------------------------
 * 运行快照（调试/显示/上报用）
 *
 * 为什么要有它：
 *   1) Keil 的 Watch 窗口只能看变量、不能调函数，把关键信息集中在一个结构体里，
 *      勾上 View → Periodic Window Update 就能在全速运行时观察；
 *   2) 控制台一次性读走整块快照再打印，比逐个调 getter 更不容易读到"半新半旧"的组合。
 * 加 volatile 的原因：它由电机任务写、被控制台任务读，volatile 让读的一方每次都从内存
 * 重读，而不是拿寄存器里缓存的旧值（它并不是"防优化删掉"用的——外部链接的全局本来就不会被删）。
 * ------------------------------------------------------------------ */
typedef struct {
    app_motor_state_t state;        /* 应用状态（IDLE/OPENING/...，Watch 里显示枚举名） */
    app_motor_fault_t fault;        /* 故障码 */
    uint8_t           motor_ready;  /* 1 = 电机驱动可用（引脚已核实且初始化成功） */
    uint8_t           din_ready;    /* 1 = 到位输入可用 */
    uint8_t           limit_open;   /* 开到位信号（消抖后） */
    uint8_t           limit_close;  /* 关到位信号（消抖后） */
    uint8_t           raw_open;     /* 开到位输入瞬时电平：不消抖，用来查接线和有效电平 */
    uint8_t           raw_close;    /* 关到位输入瞬时电平 */
    uint8_t           dir;          /* 驱动器当前转向（motor_dir_t） */
    uint16_t          duty;         /* 当前占空比（千分比） */
    uint32_t          running_ms;   /* 本次行程已运行时间(ms) */
    uint32_t          poll_cnt;     /* 任务循环计数：持续增长说明任务在正常运行 */
} app_motor_diag_t;

extern volatile app_motor_diag_t g_motor_diag;

#endif /* APP_MOTOR_H */
