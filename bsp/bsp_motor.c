/*!
    \file    bsp_motor.c
    \brief   电机 H 桥驱动实现（BTN8982 ×2）
    \note    设计要点：
             1) 只做硬件操作，不含业务判断；
             2) 上电、停机、换向三个时刻都保证"先断输出再改状态"，避免桥臂异常导通；
             3) 引脚未按原理图核实时拒绝初始化，避免误驱动未知引脚。
*/

#include "bsp_motor.h"
#include "bsp_config.h"

/* ---------------- 模块内部状态（全部 static，不对外暴露） ---------------- */
static motor_state_t s_motor_state = MOTOR_STATE_UNINIT;
static motor_dir_t   s_motor_dir   = MOTOR_DIR_STOP;
static uint16_t      s_motor_duty  = 0U;

/* ---------------- 内部函数 ---------------- */

/* 把千分比占空比换算成定时器比较值 */
static uint32_t motor_duty_to_pulse(uint16_t duty)
{
    if (duty > MOTOR_DUTY_MAX) {
        duty = MOTOR_DUTY_MAX;
    }
    /* 用 32 位中间量，避免 duty × 周期 溢出 */
    return ((uint32_t)duty * (MOTOR_PWM_PERIOD + 1U)) / MOTOR_DUTY_MAX;
}

/* 写某个通道的比较值 */
static void motor_set_pulse(uint16_t channel, uint32_t pulse)
{
    timer_channel_output_pulse_value_config(MOTOR_PWM_TIMER, channel, pulse);
}

/* 两个半桥的 IN 脚同时归零（输出全为低边导通，是安全中间态） */
static void motor_in_pulse_clear(void)
{
    motor_set_pulse(MOTOR_PWM_CH_IN1, 0U);
    motor_set_pulse(MOTOR_PWM_CH_IN2, 0U);
}

/* 桥臂"使能 + 供电"同开同关。
 * 本板这两路是独立信号：M_INH(PB1) 决定桥臂能不能输出，V24_EN(PA8) 决定桥臂有没有电。
 * 分开驱动必然出现中间态，所以统一在这里成对操作：
 *   给电时先使能再供电（桥臂先处于可控的高阻态，上电瞬间不会乱通）；
 *   断电时先撤使能再断电（先停止导通，再断 24V）。
 * 只有把两路并成一路的板子才走 #else 分支（BSP_MOTOR_HAS_PWR_GATE = 0）。 */
static void motor_enable_output(bool on)
{
#if (BSP_MOTOR_HAS_PWR_GATE)
    if (on) {
        gpio_bit_set(MOTOR_INH_PORT, MOTOR_INH_PIN);
        gpio_bit_set(MOTOR_PWR_EN_PORT, MOTOR_PWR_EN_PIN);
    } else {
        gpio_bit_reset(MOTOR_INH_PORT, MOTOR_INH_PIN);
        gpio_bit_reset(MOTOR_PWR_EN_PORT, MOTOR_PWR_EN_PIN);
    }
#else
    if (on) {
        gpio_bit_set(MOTOR_INH_PORT, MOTOR_INH_PIN);
    } else {
        gpio_bit_reset(MOTOR_INH_PORT, MOTOR_INH_PIN);
    }
#endif
}

/* 配置两个 IN 脚为定时器复用推挽输出 */
static void motor_in_pins_config(void)
{
    /* IN1 */
    gpio_mode_set(MOTOR_IN1_PORT, GPIO_MODE_AF, GPIO_PUPD_NONE, MOTOR_IN1_PIN);
    gpio_output_options_set(MOTOR_IN1_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_10MHZ, MOTOR_IN1_PIN);
    gpio_af_set(MOTOR_IN1_PORT, MOTOR_IN1_AF, MOTOR_IN1_PIN);
    /* IN2 */
    gpio_mode_set(MOTOR_IN2_PORT, GPIO_MODE_AF, GPIO_PUPD_NONE, MOTOR_IN2_PIN);
    gpio_output_options_set(MOTOR_IN2_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_10MHZ, MOTOR_IN2_PIN);
    gpio_af_set(MOTOR_IN2_PORT, MOTOR_IN2_AF, MOTOR_IN2_PIN);
}

/* 配置 PWM 定时器：两路通道同频，PWM0 模式，初始占空比 0 */
static void motor_pwm_config(void)
{
    timer_parameter_struct timer_initpara;
    timer_oc_parameter_struct timer_ocpara;

    timer_deinit(MOTOR_PWM_TIMER);
    timer_struct_para_init(&timer_initpara);
    timer_initpara.prescaler         = MOTOR_PWM_PRESCALER;
    timer_initpara.alignedmode       = TIMER_COUNTER_EDGE;
    timer_initpara.counterdirection  = TIMER_COUNTER_UP;
    timer_initpara.period            = MOTOR_PWM_PERIOD;
    timer_initpara.clockdivision     = TIMER_CKDIV_DIV1;
    timer_init(MOTOR_PWM_TIMER, &timer_initpara);

    timer_channel_output_struct_para_init(&timer_ocpara);
    timer_ocpara.outputstate = TIMER_CCX_ENABLE;
    timer_ocpara.ocpolarity  = TIMER_OC_POLARITY_HIGH;

    /* 通道 1（对应 IN1） */
    timer_channel_output_config(MOTOR_PWM_TIMER, MOTOR_PWM_CH_IN1, &timer_ocpara);
    timer_channel_output_mode_config(MOTOR_PWM_TIMER, MOTOR_PWM_CH_IN1, TIMER_OC_MODE_PWM0);
    /* 关闭影子寄存器：占空比写入后立即生效，便于运行中调速 */
    timer_channel_output_shadow_config(MOTOR_PWM_TIMER, MOTOR_PWM_CH_IN1, TIMER_OC_SHADOW_DISABLE);
    timer_channel_output_pulse_value_config(MOTOR_PWM_TIMER, MOTOR_PWM_CH_IN1, 0U);

    /* 通道 2（对应 IN2） */
    timer_channel_output_config(MOTOR_PWM_TIMER, MOTOR_PWM_CH_IN2, &timer_ocpara);
    timer_channel_output_mode_config(MOTOR_PWM_TIMER, MOTOR_PWM_CH_IN2, TIMER_OC_MODE_PWM0);
    timer_channel_output_shadow_config(MOTOR_PWM_TIMER, MOTOR_PWM_CH_IN2, TIMER_OC_SHADOW_DISABLE);
    timer_channel_output_pulse_value_config(MOTOR_PWM_TIMER, MOTOR_PWM_CH_IN2, 0U);

    timer_auto_reload_shadow_enable(MOTOR_PWM_TIMER);
    timer_enable(MOTOR_PWM_TIMER);
}

/* ---------------- 对外接口 ---------------- */

void bsp_motor_init(void)
{
#if (0U == BSP_MOTOR_PINS_CONFIRMED)
    /* 引脚未经原理图核实：一个 IO 都不碰，保持安全 */
    s_motor_state = MOTOR_STATE_UNINIT;
    s_motor_dir   = MOTOR_DIR_STOP;
    s_motor_duty  = 0U;
    return;
#else
    /* 先配使能/供电门控脚并确保断开，再配 PWM，最后才允许使能 */
    rcu_periph_clock_enable(MOTOR_INH_RCU);
    gpio_mode_set(MOTOR_INH_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_PULLDOWN, MOTOR_INH_PIN);
    gpio_output_options_set(MOTOR_INH_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_10MHZ, MOTOR_INH_PIN);
#if (BSP_MOTOR_HAS_PWR_GATE)
    rcu_periph_clock_enable(MOTOR_PWR_EN_RCU);
    gpio_mode_set(MOTOR_PWR_EN_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_PULLDOWN, MOTOR_PWR_EN_PIN);
    gpio_output_options_set(MOTOR_PWR_EN_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_10MHZ, MOTOR_PWR_EN_PIN);
#endif
    motor_enable_output(false);                         /* 初始断开：半桥不给电 */

    rcu_periph_clock_enable(MOTOR_IN1_RCU);
    rcu_periph_clock_enable(MOTOR_IN2_RCU);
    rcu_periph_clock_enable(MOTOR_PWM_TIMER_RCU);

    motor_in_pins_config();
    motor_pwm_config();
    motor_in_pulse_clear();

    s_motor_dir   = MOTOR_DIR_STOP;
    s_motor_duty  = 0U;
    s_motor_state = MOTOR_STATE_COAST;
#endif
}

bool bsp_motor_is_ready(void)
{
    return (MOTOR_STATE_UNINIT != s_motor_state);
}

bool bsp_motor_run(motor_dir_t dir, uint16_t duty)
{
    uint32_t pulse;

    if (!bsp_motor_is_ready()) {
        return false;                                   /* 未初始化/引脚未核实 */
    }
    if ((MOTOR_DIR_FORWARD != dir) && (MOTOR_DIR_REVERSE != dir)) {
        return false;                                   /* 只接受两个转向 */
    }
    if (0U == duty) {
        bsp_motor_stop();                               /* 0 占空比按停机处理 */
        return true;
    }

    pulse = motor_duty_to_pulse(duty);

    /* 换向过程：先断开输出 → 目标通道给占空比 → 再使能。
     * 这样两个半桥不会在切换瞬间同时处于高边导通（等效电源短路），
     * 也避免电机高速旋转时突然反接造成的电流冲击。 */
    motor_enable_output(false);     /* 先断半桥供电/使能 */
    motor_in_pulse_clear();
    if (MOTOR_DIR_FORWARD == dir) {
        motor_set_pulse(MOTOR_PWM_CH_IN1, pulse);
    } else {
        motor_set_pulse(MOTOR_PWM_CH_IN2, pulse);
    }
    /* 此时 IN1/IN2 已是目标状态，再给半桥供电：桥臂一上电就按预期方向导通 */
    motor_enable_output(true);

    s_motor_dir   = dir;
    s_motor_duty  = duty;
    s_motor_state = MOTOR_STATE_RUN;
    return true;
}

void bsp_motor_set_duty(uint16_t duty)
{
    if (MOTOR_STATE_RUN != s_motor_state) {
        return;                                         /* 非运行态不调速 */
    }
    if (duty > MOTOR_DUTY_MAX) {
        duty = MOTOR_DUTY_MAX;
    }
    if (MOTOR_DIR_FORWARD == s_motor_dir) {
        motor_set_pulse(MOTOR_PWM_CH_IN1, motor_duty_to_pulse(duty));
    } else if (MOTOR_DIR_REVERSE == s_motor_dir) {
        motor_set_pulse(MOTOR_PWM_CH_IN2, motor_duty_to_pulse(duty));
    } else {
        return;
    }
    s_motor_duty = duty;
}

void bsp_motor_stop(void)
{
    if (!bsp_motor_is_ready()) {
        return;
    }
    /* 先断使能/供电：桥臂失电，之后清占空比不会产生额外动作 */
    motor_enable_output(false);
    motor_in_pulse_clear();

    s_motor_dir   = MOTOR_DIR_STOP;
    s_motor_duty  = 0U;
    s_motor_state = MOTOR_STATE_COAST;
}

void bsp_motor_brake(void)
{
    if (!bsp_motor_is_ready()) {
        return;
    }
    /* IN1 = IN2 = 0 且使能给上：两个半桥的低边同时导通，绕组被短路制动。
     * 制动同样需要半桥带电，所以顺序是"先置 IN 再给电"。 */
    motor_in_pulse_clear();
    motor_enable_output(true);

    s_motor_dir   = MOTOR_DIR_BRAKE;
    s_motor_duty  = 0U;
    s_motor_state = MOTOR_STATE_BRAKE;
}

motor_dir_t bsp_motor_get_dir(void)
{
    return s_motor_dir;
}

motor_state_t bsp_motor_get_state(void)
{
    return s_motor_state;
}

uint16_t bsp_motor_get_duty(void)
{
    return s_motor_duty;
}
