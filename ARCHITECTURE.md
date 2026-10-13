# 工程架构说明（GD32L233RCT6 / YL_conV_V24 板）

> 配套文档：`CONSOLE_COMMANDS.md`（调试串口命令）、
> `RTOS_LEARNING.md`（RTOS 零基础学习路线——把任务/优先级/队列/互斥量/中断边界等概念
> 逐条对到本工程真实代码，含可动手的安全实验）

## 1. 分层与调用方向

```
app/      应用层   业务逻辑、状态机、任务
  ↓
middle/   中间层   协议解析(Modbus/AT)、环形缓冲、通用服务
  ↓
bsp/      驱动层   板级/芯片外设操作：GPIO/UART/I2C/SPI/ADC/DAC/PWM/RTC/WDG
  ↓
Firmware/ 厂商库   CMSIS + GD32L23x 标准外设库（只读，不改）
```

- **只允许自上而下单向调用**。上层通过 `.h` 里声明的接口调用下层。
- **严禁**在 `app/`、`middle/` 里直接读写寄存器或调用 `gd32l23x_xxx.c` 的库函数。
- **严禁**下层出现业务概念（例如 `bsp/usart` 里不允许判断"这是 Modbus 帧"）。
- 调试打印属于**应用层设施**（`printf_debug()`，见 `app/app_console.h`）；驱动层不要反过来去包含 app 头文件，需要打印时由上层转出。
- 跨层数据传递用**接口函数 / 回调 / 消息队列**，不把上层结构体类型当参数往下传。
- 硬件差异（引脚、时钟、外设实例编号）**全部收口到 `bsp/bsp_config.h`**，换板子只改这一处。

## 2. 目录现状

| 目录 | 状态 | 说明 |
|---|---|---|
| `app/` | 已建 | `main.c`（创建任务）、`app_motor`（开/关阀 + 到位停止 + 超时保护） |
| `bsp/` | 已建 | 已有 `bsp_config.h`（引脚/时钟收口）、`bsp_motor`（H 桥 PWM 驱动）、`bsp_din`（消抖数字输入） |
| `middle/` | 已建（空） | 待填：ringbuffer / modbus / at_parser |
| `gd32_sys/` | 已有 | 启动后系统级代码：中断向量实现 `gd32l23x_it.c`、`systick.c`、`gd32l23x_libopt.h` |
| `Firmware/` | 已有 | 厂商库，**不建议改动** |
| `FreeRTOS/` | 已有 | 内核 + CM23 端口 + `config/FreeRTOSConfig.h` |
| `Utilities/` | **已从工程移除** | 旧的评估板 BSP，文件为空（0 字节），可删除该目录 |
| `Objects/` `Listings/` | 编译产物 | 不应纳入版本管理 |

Keil 工程分组已同步：`app` / `bsp` / `middle` / `Firmware` / `gd32_sys` / `CMSIS`（含启动文件）/ `FreeRTOS`，
头文件搜索路径已加入 `.\bsp;.\middle`，`.\Utilities` 已移除。

## 3. 命名与编码约定

- 一个模块一对 `.c/.h`，模块前缀统一：`bsp_uart_init()`、`mb_poll()`、`at_send()`。
- 对外只暴露最小接口，内部函数/变量加 `static`；宏全大写，类型以 `_t` 结尾。
- 平台相关类型用 `stdint.h` 的 `uint8_t` 等，不用 `unsigned char` / 裸 `int`。
- 枚举一律以 `_COUNT` 哨兵结尾（如 `APP_MOTOR_CMD_COUNT`），"取值个数"和越界判断都用它，
  不要用"最后一项"或写死的数字；枚举与配套的名字表/映射表必须放在同一个 `.c` 里，
  并用编译期断言（`enum { x = 1 / ((cond) ? 1 : 0) }`）把条数绑死，防止增项后静默错位。
- 外部输入（串口帧、485 报文、AT 应答）**必须**先做长度/校验/越界检查再使用。
- 错误处理统一返回错误码或 `bool`，不静默吞错；关键故障走看门狗。
- 注释写"为什么"，不写"这行在做什么"。

## 4. 中断与并发约定

- **ISR 只做三件事**：收数据、清标志、发消息（`xQueueSendFromISR` / `vTaskNotifyGiveFromISR`），处理逻辑放任务。
- 调用 `...FromISR` 的 API 要求中断抢占优先级数值 **≥ 5**
  （`configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY = 5`，共 8 级）。
  用 `nvic_irq_enable(irq, pre, sub)` 时务必统一优先级分组，且 `pre >= 5`；不得使用优先级 0~4 调用 RTOS API。
- `SVC/PendSV/SysTick` 由 FreeRTOS 端口接管，**不要**在 `gd32l23x_it.c` 里重新实现。
- ISR 与任务共享的变量用临界区/RTOS 原语保护；`volatile` 只用于硬件寄存器和 ISR 修改的标志。

## 5. 内存约定（芯片 256KB Flash / 32KB SRAM）

- **静态分配优先**：固定大小的对象用数组/对象池；LCD 显存、4G 收发缓冲一律静态分配。
- 动态分配只用 FreeRTOS `heap_4`（当前 `configTOTAL_HEAP_SIZE = 10KB`），规划峰值后留 20%~30% 裕量。
  建议改到 16KB，并打开 `configUSE_MALLOC_FAILED_HOOK`。
- 栈大小按最深调用路径估算；调优时打开 `INCLUDE_uxTaskGetStackHighWaterMark` 用高水位验证。
- MSP（启动文件 `Stack_Size = 0x400`）承载所有中断嵌套，改中断数量时同步复核。

## 6. 待补充的模块清单（对应原理图 YL_conV_V24）

| 层 | 计划模块 | 对应硬件功能 |
|---|---|---|
| bsp | `bsp_config.h` | 引脚宏、时钟树、外设实例分配（电机侧引脚已确认；到位输入待确认） |
| bsp | `bsp_gpio` / `bsp_gpio_exti` | 按键 Btn1/Btn2、继电器、蜂鸣器、LED、各电源使能、LCD 控制脚 |
| bsp | `bsp_usart`(+DMA) | RS485(485_EN 方向)、S1/TDLAS 传感器、UART4 扩展口、Debug 口 |
| bsp | `bsp_lpuart` | EC800 4G 模块（含 PWRKEY/DTR/RI 控制） |
| bsp | `bsp_i2c` | I2C0(EEPROM M24M01) + I2C 开关(U1，SDA0~3/SCL0~3 通道切换) |
| bsp | `bsp_spi` / `bsp_lcd` | SPI 屏（SCK/MOSI/CS/DC/RST/BLK） |
| bsp | `bsp_adc`(+DMA) | AD_BAT、AD_Fb、Vgen-Vbat、M_IS、IN0-10V |
| bsp | `bsp_dac` | 4-20mA 输出（DAC → XTR111） |
| bsp | `bsp_pwm` | 电机 BTN8982 的 M_IN1/M_IN2 驱动 |
| bsp | `bsp_rtc` / `bsp_iwdg` | 32.768kHz RTC、看门狗 |
| middle | `ring_buffer`、`modbus_rtu`、`at_parser`、`filter/calib` | 缓冲、485 协议、4G AT、模拟量标定 |
| app | `sys_mon`、`motor_ctrl`、`sensor_task`、`comm_4g`、`hmi`、`analog_task`、`power_mgr` | 业务任务划分 |

## 7. 关键坑与遗留问题

### 7.1 已在调度器启动前使能 SysTick 中断（已修复，务必不要再犯）

原厂 `gd32_sys/systick.c` 的 `systick_config()` 是 `SysTick_Config()` + 开中断 + 1ms 滴答，
但本工程的 SysTick 中断**已经归 FreeRTOS**（`port.c` 的 `SysTick_Handler`）。
FreeRTOS 的 `xTaskIncrementTick()` 里没有任何"调度器未启动"的保护，它会操作
"延迟任务链表"（`pxDelayedTaskList`）——而该链表要等**第一个任务创建**时才初始化。

于是：如果开机初始化阶段（例如往串口打印横幅要 8ms）就已经开了 SysTick 中断，
中间触发的每一次 tick 都会让内核对 **NULL 链表指针**做读写。
现象非常迷惑人：**开机横幅能完整打印出来，然后整机静默卡死**（因为此时串口是轮询发送，
不依赖中断，而后续的堆/链表已被破坏）。

现在 `systick_config()` 只负责把 SysTick 关干净，配置权完整交给
FreeRTOS 的 `vPortSetupTimerInterrupt()`；`main.c` 里的调用保留但加了解释。
启动前需要延时请用 `vTaskDelay()`，不要指望 SysTick。

### 7.2 致命错误不再静默

- `configASSERT` 改为调用 `app_assert_fail()`（实现在 `gd32_sys/gd32l23x_it.c`）：
  断言失败会把**文件名:行号**从调试串口裸打出来再停机。
- `HardFault_Handler` 会打印 CFSR/HFSR/BFAR 和出错前后 PC（MSP/PSP 两个都打，
  像 `0x08xxxxxx` 的那个就是真正的出错地址），拿它去 map 文件里查即可定位。
- 栈溢出钩子会打印**哪个任务**溢出。
- 任务创建失败、调度器启动失败也会直接在串口报出来。
- 全部是"只在失败时打印"，正常运行不会有多余输出。
  （联调期间为了定位上面 7.1 那个坑，曾临时加过启动进度打印和控制台心跳，
  问题定位后已全部移除；需要时按需再加。）

### 7.3 其余遗留

1. `app/main.c` 已创建电机/控制台两个任务。（原 `main.h` 里 `led_spark()` 只有声明无定义的
   残留已删除；`main.h` 现在是空壳，只被 `main.c` 和 `gd32l23x_it.c` include，可留可删。）
2. 调试打印统一走 `app_console.c` 的 `printf_debug()`（**没有** `fputc` 重定向，
   全工程无裸 `printf`）。它内部带发送互斥，只能在任务里调用。
3. 系统时钟目前由**内部 IRC16M** 倍频到 64MHz，需确认板上是否贴外部 HXTAL。
4. 到位/限位输入引脚（参考工程是 PA11/PA12、低电平有效）仍需按本板原理图确认，
   确认后把 `bsp_config.h` 的 `BSP_DIN_PINS_CONFIRMED` 置 1。
5. **源文件是 GBK 编码，而 armclang 只认 UTF-8**，所以每次编译 `app_console.c` 都会报 13 个
   `illegal character encoding in string literal` 警告。这些警告在预期内：armclang 会原样透传
   这些字节，axf 里的中文仍是 GBK，串口终端按 GBK 收才能正常显示（已核对字节一致）。
   但要注意这个警告**保护不了真正的危险方向**——它只对 GBK 文件报警，对"新写进来的 UTF-8 文件"
   一声不响，而后者才会让串口输出变成乱码。彻底解决二选一：
   （a）全部源文件转 UTF-8，并同步把串口终端改成 UTF-8（推荐，但属于工作流变更）；
   （b）在 C/C++ → Misc Controls 加 `-Wno-invalid-source-encoding` 压掉（需配合
   "新增文件一律 GBK"的约定）。在那之前，这 13 个警告会一直占位、淹没后续真正的新警告。
