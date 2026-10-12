/*!
    \file    app_motor.c
    \brief   电机应用层实现：命令处理 + 到位停止 + 超时/信号矛盾保护
    \note    为什么这样分层：
             - 驱动层(bsp_motor/bsp_din)只认"转向、占空比、电平"这些硬件概念；
             - 本层负责把硬件能力翻译成"开阀/关阀/到位"的业务语义；
             - 其它任务要动电机，只能通过 app_motor_request() 发命令，
               不能直接调 bsp_motor_run()，避免多处争抢同一个执行机构。
*/

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include "app_motor.h"
#include "bsp_motor.h"
#include "bsp_din.h"

/* ============================ 业务参数 ============================ */
/* 默认占空比（千分比）。阀门电机通常不需要满载转速，先给 60% 便于观察，
 * 实际上机后按转速/力矩需求标定。 */
#define APP_MOTOR_DUTY_DEFAULT          (900U)

/* 单次行程超时：超时仍未收到到位信号则判故障停机，防止堵转烧电机/驱动 */
#define APP_MOTOR_TIMEOUT_MS            (50000U)

/* 到位/停止时是否短路制动：
 *   1 = 制动（绕组短路，位置重复性好，但会持续消耗少量保持电流）
 *   0 = 滑行（断输出省电，但电机会因惯性多走一点） */
#define APP_MOTOR_STOP_BY_BRAKE         (1)

/* 到位开关接在哪两个输入通道上（按实际接线确认，必须与 bsp_config.h 的网络对应） */
#define APP_MOTOR_CH_LIMIT_OPEN         BSP_DIN_CH_LIMIT_1      /* 开到位 */
#define APP_MOTOR_CH_LIMIT_CLOSE        BSP_DIN_CH_LIMIT_2      /* 关到位 */

/* ============================ 内部状态 ============================ */
static QueueHandle_t     s_cmd_queue;       /* 命令队列（深度 1，最新命令覆盖旧命令） */
static app_motor_state_t s_state;
static app_motor_fault_t s_fault;
static TickType_t        s_move_start_tick; /* 本次行程起始 tick */
static uint32_t          s_running_ms;      /* 本次行程已运行时间 */
static uint16_t          s_duty_default = APP_MOTOR_DUTY_DEFAULT;   /* 行程占空比，可运行时修改 */

/* 运行快照：唯一的全局变量，电机任务写、控制台与调试器读，见 app_motor.h */
volatile app_motor_diag_t g_motor_diag;

/* ============================ 显示名 ============================ */
/* 名字表与枚举定义紧挨着，并用编译期断言把"条数"绑死：
 * 往 app_motor.h 的枚举里加项却忘了补名字时直接编译不过，
 * 而不是运行时打印出一个错位的名字——那种错误编译器原本不会报，只能靠人盯着。
 * 表长一律用 APP_MOTOR_ARRAY_LEN 取，不再写死数字。 */
#define APP_MOTOR_STATIC_ASSERT(cond, tag) \
    enum { app_motor_static_assert_##tag = 1 / ((cond) ? 1 : 0) }
#define APP_MOTOR_ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0U]))

static const char *const s_state_name[] = { "IDLE", "OPENING", "CLOSING", "FAULT" };
static const char *const s_fault_name[] = { "none", "timeout", "driver", "conflict" };
static const char *const s_dir_name[]   = { "stop", "fwd", "rev", "brake" };

APP_MOTOR_STATIC_ASSERT(APP_MOTOR_ARRAY_LEN(s_state_name) == (uint32_t)APP_MOTOR_STATE_COUNT, state_name);
APP_MOTOR_STATIC_ASSERT(APP_MOTOR_ARRAY_LEN(s_fault_name) == (uint32_t)APP_MOTOR_FAULT_COUNT, fault_name);
APP_MOTOR_STATIC_ASSERT(APP_MOTOR_ARRAY_LEN(s_dir_name) == (uint32_t)MOTOR_DIR_COUNT, dir_name);

/* 越界一律给 "?"：宁可显示不出来，也不能读越界 */
static const char *name_of(const char *const *table, uint32_t count, uint32_t idx)
{
    return (idx < count) ? table[idx] : "?";
}

const char *app_motor_state_name(app_motor_state_t st)
{
    return name_of(s_state_name, (uint32_t)APP_MOTOR_ARRAY_LEN(s_state_name), (uint32_t)st);
}

const char *app_motor_fault_name(app_motor_fault_t fault)
{
    return name_of(s_fault_name, (uint32_t)APP_MOTOR_ARRAY_LEN(s_fault_name), (uint32_t)fault);
}

const char *app_motor_dir_name(uint8_t dir)
{
    return name_of(s_dir_name, (uint32_t)APP_MOTOR_ARRAY_LEN(s_dir_name), (uint32_t)dir);
}

/* ============================ 内部函数 ============================ */

/* 刷新运行快照。放在每个采样周期末尾，Watch 窗口就能看到"当前发生了什么" */
static void diag_update(void)
{
    /* 用 raw 读瞬时电平：先确认接线与有效电平对不对，再去看消抖后的值 */
    g_motor_diag.raw_open    = (uint8_t)bsp_din_level_raw(APP_MOTOR_CH_LIMIT_OPEN);
    g_motor_diag.raw_close   = (uint8_t)bsp_din_level_raw(APP_MOTOR_CH_LIMIT_CLOSE);

    g_motor_diag.state       = s_state;
    g_motor_diag.fault       = s_fault;
    g_motor_diag.motor_ready = bsp_motor_is_ready() ? 1U : 0U;
    g_motor_diag.din_ready   = bsp_din_is_ready() ? 1U : 0U;
    g_motor_diag.limit_open  = bsp_din_active(APP_MOTOR_CH_LIMIT_OPEN) ? 1U : 0U;
    g_motor_diag.limit_close = bsp_din_active(APP_MOTOR_CH_LIMIT_CLOSE) ? 1U : 0U;
    g_motor_diag.dir         = (uint8_t)bsp_motor_get_dir();
    g_motor_diag.duty        = bsp_motor_get_duty();
    g_motor_diag.running_ms  = s_running_ms;
    g_motor_diag.poll_cnt++;            /* 只增不减：周期性变化即说明任务活着 */
}

/* 停机：按配置选择制动或滑行 */
static void motor_halt(void)
{
#if (APP_MOTOR_STOP_BY_BRAKE)
    bsp_motor_brake();
#else
    bsp_motor_stop();
#endif
}

/* 两个到位信号同时有效：说明接线错误或开关损坏，此时任何方向都不可信 */
static bool limits_conflict(void)
{
    return (bsp_din_active(APP_MOTOR_CH_LIMIT_OPEN) &&
            bsp_din_active(APP_MOTOR_CH_LIMIT_CLOSE));
}

/* 进入故障态：先停机再置故障，顺序不能反（先停机才能保证执行机构安全） */
static void enter_fault(app_motor_fault_t fault)
{
    motor_halt();
    s_running_ms = 0U;
    s_state = APP_MOTOR_FAULT;
    s_fault = fault;
}

/* 启动一次行程 */
static void start_move(app_motor_cmd_t cmd)
{
    motor_dir_t dir;
    bool        started;

    if (limits_conflict()) {
        enter_fault(APP_MOTOR_FAULT_CONFLICT);
        return;
    }

    /* 目标方向已经在位：不必通电，直接认为动作完成（避免顶着限位开关堵转） */
    if ((APP_MOTOR_CMD_OPEN == cmd) && bsp_din_active(APP_MOTOR_CH_LIMIT_OPEN)) {
        s_state = APP_MOTOR_IDLE;
        return;
    }
    if ((APP_MOTOR_CMD_CLOSE == cmd) && bsp_din_active(APP_MOTOR_CH_LIMIT_CLOSE)) {
        s_state = APP_MOTOR_IDLE;
        return;
    }

    dir = (APP_MOTOR_CMD_OPEN == cmd) ? MOTOR_DIR_FORWARD : MOTOR_DIR_REVERSE;
    started = bsp_motor_run(dir, s_duty_default);
    if (!started) {
        enter_fault(APP_MOTOR_FAULT_DRIVER);
        return;
    }

    s_move_start_tick = xTaskGetTickCount();
    s_running_ms = 0U;
    s_state = (APP_MOTOR_CMD_OPEN == cmd) ? APP_MOTOR_OPENING : APP_MOTOR_CLOSING;
}

/* 处理一条命令 */
static void handle_cmd(app_motor_cmd_t cmd)
{
    switch (cmd) {
    case APP_MOTOR_CMD_CLEAR_FAULT:
        /* 驱动器本身不可用时不接受清故障：清了也不能动，只会掩盖问题 */
        if (APP_MOTOR_FAULT_DRIVER != s_fault) {
            s_fault = APP_MOTOR_FAULT_NONE;
            s_state = APP_MOTOR_IDLE;
        }
        break;

    case APP_MOTOR_CMD_STOP:
        motor_halt();
        s_running_ms = 0U;
        s_state = APP_MOTOR_IDLE;
        break;

    case APP_MOTOR_CMD_OPEN:
    case APP_MOTOR_CMD_CLOSE:
        if (APP_MOTOR_FAULT_NONE != s_fault) {
            break;                      /* 故障未清除前拒绝任何动作 */
        }
        start_move(cmd);
        break;

    default:
        break;                          /* 非法命令：丢弃，不做任何硬件动作 */
    }
}

/* 周期状态机：到位停止 / 超时保护 / 信号矛盾检测 */
static void run_state_machine(void)
{
    if ((APP_MOTOR_OPENING != s_state) && (APP_MOTOR_CLOSING != s_state)) {
        s_running_ms = 0U;              /* 非运行态不累计时间 */
        return;
    }

    /* 累计运行时间：用 tick 差值，不受任务唤醒抖动影响 */
    s_running_ms = (uint32_t)((xTaskGetTickCount() - s_move_start_tick) * portTICK_PERIOD_MS);

    if (limits_conflict()) {
        enter_fault(APP_MOTOR_FAULT_CONFLICT);
        return;
    }

    /* 到位判断：到位信号有效即认为行程结束，立即停机 */
    if ((APP_MOTOR_OPENING == s_state) && bsp_din_active(APP_MOTOR_CH_LIMIT_OPEN)) {
        motor_halt();
        s_running_ms = 0U;
        s_state = APP_MOTOR_IDLE;
        return;
    }
    if ((APP_MOTOR_CLOSING == s_state) && bsp_din_active(APP_MOTOR_CH_LIMIT_CLOSE)) {
        motor_halt();
        s_running_ms = 0U;
        s_state = APP_MOTOR_IDLE;
        return;
    }

    /* 超时保护：到位信号丢失或机械卡死时，必须主动断电 */
    if (s_running_ms >= APP_MOTOR_TIMEOUT_MS) {
        enter_fault(APP_MOTOR_FAULT_TIMEOUT);
    }
}

/* ============================ 对外接口 ============================ */

void app_motor_init(void)
{
    s_cmd_queue  = NULL;
    s_state      = APP_MOTOR_IDLE;
    s_fault      = APP_MOTOR_FAULT_NONE;
    s_running_ms = 0U;

    bsp_motor_init();
    bsp_din_init();

    /* 队列深度 1 + xQueueOverwrite：电机只认最新命令，
     * 避免"连点几次开阀"导致命令排队、到位后又被旧命令拖着再走一次 */
    s_cmd_queue = xQueueCreate(1U, sizeof(app_motor_cmd_t));
    if (NULL == s_cmd_queue) {
        s_state = APP_MOTOR_FAULT;
        s_fault = APP_MOTOR_FAULT_DRIVER;
        return;
    }

    /* 驱动器不可用（通常是引脚未按原理图核实）时提前置出故障，
     * 让上层能显示/上报，而不是等操作员按了没反应还找不到原因 */
    if (!bsp_motor_is_ready()) {
        s_state = APP_MOTOR_FAULT;
        s_fault = APP_MOTOR_FAULT_DRIVER;
    }
}

void app_motor_task(void *pv_para)
{
    TickType_t      last_wake;
    app_motor_cmd_t cmd;

    (void)pv_para;
    last_wake = xTaskGetTickCount();

    for (;;) {
        /* 1) 采集到位输入：每个周期都要采，消抖算法依赖固定采样间隔 */
        bsp_din_poll();

        /* 2) 取命令：非阻塞。深度只有 1，这里最多处理一条 */
        while ((NULL != s_cmd_queue) &&
               (pdTRUE == xQueueReceive(s_cmd_queue, &cmd, 0U))) {
            handle_cmd(cmd);
        }

        /* 3) 状态机：到位停止、超时保护、信号矛盾检测 */
        run_state_machine();

        /* 4) 刷新运行快照：不接串口也能用 Keil Watch 观察状态与到位信号 */
        diag_update();

        /* 5) 固定周期唤醒：保证消抖采样间隔稳定，也避免任务被其他任务挤占后时序漂移 */
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(APP_MOTOR_POLL_MS));
    }
}

bool app_motor_request(app_motor_cmd_t cmd)
{
    if (NULL == s_cmd_queue) {
        return false;                   /* 初始化失败，无队列可用 */
    }
    if ((uint32_t)cmd >= (uint32_t)APP_MOTOR_CMD_COUNT) {
        return false;                   /* 命令越界，直接拒绝 */
    }
    return (pdTRUE == xQueueOverwrite(s_cmd_queue, &cmd));
}

app_motor_state_t app_motor_get_state(void)
{
    return s_state;
}

app_motor_fault_t app_motor_get_fault(void)
{
    return s_fault;
}

bool app_motor_limit_open(void)
{
    return bsp_din_active(APP_MOTOR_CH_LIMIT_OPEN);
}

bool app_motor_limit_close(void)
{
    return bsp_din_active(APP_MOTOR_CH_LIMIT_CLOSE);
}

bool app_motor_is_ready(void)
{
    return bsp_motor_is_ready();
}

uint32_t app_motor_get_running_ms(void)
{
    return s_running_ms;
}

void app_motor_set_duty_default(uint16_t duty)
{
    /* 越界在这里统一夹紧，控制台只负责把值报回去，不再各写一份上限 */
    if (duty > BSP_MOTOR_DUTY_MAX) {
        duty = BSP_MOTOR_DUTY_MAX;
    }
    s_duty_default = duty;
}

uint16_t app_motor_get_duty_default(void)
{
    return s_duty_default;
}
