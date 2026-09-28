# GPIO17-18被UART0占用，如何从SDK查证？占用后实验会出现什么问题？

> 问题：GPIO17-GPIO18 是 USB 串口（UART0）占用，用了会影响串口日志/调试——
> 这个信息能查 SDK 哪个文件？是 SDK 文件吗？实验中用了会出什么问题？

---

## 一、结论先说

**"GPIO17/18 被调试串口占用"由两层信息共同决定，单靠 SDK 源码查不全：**

| 层 | 信息 | 出处 |
|---|---|---|
| ① SDK 可查 | **调试/日志/AT 串口用的是 UART0**（`CONFIG_DEBUG_UART=0`，波特率 115200） | SDK 配置文件 |
| ② 板级/芯片手册 | **UART0 的 TX/RX 物理上落在 GPIO17/GPIO18**（板上 USB 转串口芯片接线 + PINMUX 复用表） | 引脚 PDF（就是你的"HH D01完整引脚"）+ 芯片手册 C_PINMUX_CTL 复用表 |

SDK **不会**出现一行"GPIO17 = UART0_TX"这样的代码——引脚↔外设的复用关系是芯片硬件事实，
SDK 驱动里只有通用的"引脚号 + PIN_MODE"操作，所以必须配合引脚复用表（PDF）一起看。

---

## 二、SDK 里具体查哪些文件（可直接打开验证）

### 1. 确认"调试串口 = UART0"（最直接证据）
文件：`D:\ws\fbb\src\build\config\target_config\ws63\menuconfig\acore\ws63_ssb.config`

```
CONFIG_DEBUG_UART=0              # 调试串口 = UART0
CONFIG_DEBUG_UART_BAUDRATE=115200
CONFIG_AT_UART=0                 # AT 命令通道也在 UART0
CONFIG_LOG_UART=1                # 日志口是 UART1（921600）
CONFIG_UART0_BAUDRATE=115200
```
→ 这说明 UART0 同时承担**调试日志 + AT 通道**，绝不能挪用。

### 2. 确认 UART 总线定义与调试口引脚宏
文件：`D:\ws\fbb\src\drivers\chips\ws63\include\platform_core.h`

```c
#define UART_BUS_0 = 0   // UART L（低速口，对应 UART0）
#define LOG_UART_BUS   CONFIG_LOG_UART
#define LOG_UART_TX_PIN  S_AGPIO12 ...
#define SW_DEBUG_UART_BUS CONFIG_DEBUG_UART   // = UART0
```
→ 告诉你 SDK 把 DEBUG/LOG 口当固定资源管理。

### 3. 确认 GPIO17/18 的复用能力
文件：`D:\ws\fbb\src\drivers\chips\ws63\porting\pinctrl\pinctrl_porting_ws63.c`

```c
static uint8_t const g_pio_pins_avaliable_mode[PIN_MAX_NUMBER] = {
    ...
    PIO_MODE_0 | PIO_MODE_1 | PIO_MODE_2,   // GPIO_17
    PIO_MODE_0 | PIO_MODE_1 | PIO_MODE_2,   // GPIO_18
};
```
→ GPIO17/18 只有 3 种复用模式；具体"哪种 MODE 是 UART0"由芯片手册的复用表给出（PDF 内容）。

### 4. 对照你自己项目里的正常用法
你的 `project/r200_uart.c`：R200 用 **UART1 = TX GPIO15 / RX GPIO16（PIN_MODE_1，115200）**——
这就是"避开 UART0 的引脚、选别的 UART"的现成范例。

---

## 三、实验中占用 GPIO17/18 会出现什么问题

`main.c` 启动时 `uapi_pin_init()` 已把 GPIO17/18 配成 UART0 功能。
你再用 `uapi_pin_set_mode(GPIO17/18, PIN_MODE_0)` 抢走它们，典型现象：

1. **串口日志立刻消失或乱码**
   `osal_printk` / `printf` 没有输出，或 PC 串口助手收到乱码——因为 UART0 的 TX 脚被你切去干别的了。
2. **AT 通道失效**（CONFIG_AT_UART=0 同在 UART0）。
3. **"代码对了但外设没反应"的诡异现象**
   GPIO 读写调用返回成功，但电平不变——本质是 PINMUX 还停在 UART 功能上（或改完没切回来）。
4. **电平拉扯风险**
   USB 转串口芯片持续驱动这两根线，你外接的器件也在驱动 → 电平打架，通信不稳，极端情况损坏 IO。
5. **失去唯一的排障手段**
   板子跑飞/死循环后（比如你项目里 TCA9555 probe 失败进入的 while(1)），没有任何日志可看，只能盲猜。
   特征：复位后前几秒还有 boot 日志，随后断掉——正是你的应用代码抢引脚的时机点。

**正确做法**：GPIO17/18 直接跳过，选空闲引脚（对照引脚 PDF），外设串口走 UART1/UART2。

---

## 四、面试/答辩怎么讲（知识点）

- **PINMUX（引脚复用）**：一根物理引脚在同一时刻只能复用给一个外设；先到先得，后配会覆盖。
- **板级引脚分配流程**：先查引脚表/PDF → 排除调试口/电源/晶振等固定占用 → 再给业务外设分引脚。
- **排障思路**："日志消失"先怀疑串口引脚被抢占，再用"复位后有 boot 日志、应用启动后断流"定位到应用层改了 PINMUX。
- 一句话总结：**GPIO17/18 是 USB 串口（UART0 调试口）的专用引脚，占用 = 拿掉自己的调试通道。**
