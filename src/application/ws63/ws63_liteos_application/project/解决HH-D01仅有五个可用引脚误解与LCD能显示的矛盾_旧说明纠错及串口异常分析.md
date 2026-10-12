# 解决 HH-D01 仅有五个可用引脚误解与 LCD 能显示的矛盾：旧说明纠错及串口异常分析

核对日期：2026-10-11。工程：`D:/ws/fbb/src/application/ws63/ws63_liteos_application/`。

本次解决的是资料与实际运行相矛盾的解释问题。没有修改业务 C 源码、构建配置、编译或烧录固件，没有做实板电压或波形测量。LCD 当前正常显示来自用户本轮描述；之前蜂鸣器接线导致异常的物理根因尚未闭环。附件中的建议作为待核对的资料，不作为本次操作指令。

## 1. 直接结论

不是你简单地把文档读错了。旧说明本身把 SDK 的 QSPI 引脚定义过度推断成了 HH-D01 的实际 Flash 占用，并把串口乱码和 Flash 提示当作已经证明的硬件根因。

本次完整读取的用户 PDF 第 2 页写的是只有 GPIO12、13、14 三个安全可用，并非五个。因此“板子最多只能用五个引脚”没有被这份 PDF 或当前工程证实。可用数量随板卡线路、当前固件角色、引脚复用和外接电路而变化。

当前 LCD 驱动占用五个 GPIO：GPIO7=SCK、GPIO9=MOSI、GPIO8=CS、GPIO1=DC、GPIO14=RESET。LCD 背光 LED/BL 如果按现有接线说明接 3.3V，虽然多接了一根线，却不占第六个 GPIO。MISO、触摸和 SD 卡没有被当前 LCD 驱动使用。用户此次第六根线的真实去向尚待确认；如果接的是 GPIO 或 MISO，必须按实际接法重新计数。

TCA9555 扩展方案有效且可以继续使用，但这不等于 HH-D01 硬件只能提供三路或五路 GPIO，也不证明之前所有串口异常都是 Flash 引脚冲突。

## 2. 为什么旧说明与 LCD 运行矛盾

旧 PDF 第 1 页把 GPIO0～GPIO11 逐个列为不建议使用，第 3 页以 `platform_core.h` 中两组 QSPI 宏和 `XIP_EXIST` 为依据，认定这些脚都参与启动 Flash。

关键推理错误是：某 GPIO 出现在 SDK 的某组外设定义中，不等于该开发板已经把这个 GPIO 接到启动 Flash，也不等于当前固件正在启用该功能。`XIP_EXIST` 表明平台定义了 XIP 能力，不能独自确定 Flash 具体连到哪组焊盘，更不能证明两组 QSPI 对应的十二个 GPIO 同时全部被占用。

厂商 HH-D01 V03 规格书第 8 页表 2-1 明确列出：GPIO07 可复用 SPI0_SCK，GPIO09 可复用 SPI0_OUT，GPIO08 可复用 SPI0_CS1_N，GPIO01 与 GPIO14 有 GPIO 用途。第 5 页规格还列出内嵌 4MB Flash，因此旧说明不能仅凭 XIP 宏就确定为排针上的外部 Flash 占用。

开发板原理图第 1 页显示这些 GPIO 从 U3 模组引到 J5/J6；第 2 页显示 USB 串口桥 CH340K、GPIO13 的 USER 按键，以及 GPIO05 到板载彩灯输入的线路。这些是实际的板载共享关系。原理图没有展开模组内部全部线路，本次没有从它推导全部 GPIO 在所有上电条件下都可任意外接负载。

SDK 的 `pinctrl_porting_ws63.c` 允许 GPIO7/9 使用模式 3，也允许 GPIO1/8/14 使用模式 0；官方 `hcc_spi_host.c` 使用 GPIO7 作时钟、GPIO9 作 SPI 输出。当前 LCD 驱动恰好给 SCK/MOSI 设置模式 3，给 CS/DC/RESET 设置模式 0。CS 由软件拉高、拉低，并不依赖硬件片选路由。

因此，LCD 运行可以由当前接线、正确复用设置以及未占用调试串口解释，不能再用旧资料的“GPIO0～GPIO11 全禁用”解释它。

## 3. 六根线为什么只占五个 GPIO

以下是当前固件和既有接线说明要求的接法，不代表本次已观察到实物：

- LCD SCK → 板2 GPIO7：输出 SPI 时钟。
- LCD MOSI/SDI → 板2 GPIO9：发送命令参数和像素数据。
- LCD CS → 板2 GPIO8：选择屏幕，低电平有效。
- LCD DC → 板2 GPIO1：区分命令和数据。
- LCD RESET → 板2 GPIO14：复位屏幕控制器。
- LCD LED/BL → 屏幕电源 3.3V：保持背光打开，不占 GPIO。
- LCD VCC → 屏幕电源 3.3V；LCD GND → 板2及屏幕电源共地。

35002 手册 PDF 第 5 页、印刷第 3 页定义 LED 为高电平使能的背光控制输入。第 6 页背光电路有三极管，不能把这个控制输入混同为裸屏大电流背光电源。上述接法只适用于本次核对的 35002 模块。

如果要软件开关背光，把 BL 接另一可用 GPIO，才增加一路 GPIO 占用。如果第六根是 MISO，则增加一根回读数据线，但当前程序未使用回读。正常显示无法证明这根额外线配置正确。

板1和板2是两颗独立芯片。板1 GPIO14 控制 TCA 的 SDA，板2 GPIO14 控制 LCD RESET，两者不冲突。板1的 R200 占 GPIO15/16，也不会自动占用板2的同名 GPIO。当前 B 角色的 CMake 分支不编译 R200、TCA、按键和蜂鸣器业务源码。

## 4. 为什么以前直连外设时串口异常，改用 TCA 后正常

可确认的事实是：用户记录了 GPIO6 接蜂鸣器后复位时串口异常，运行后再接可能正常；另有旧笔记记录换脚后仍乱码、涉及 USB 串口占用。现有资料没有保存足够的前后对照原始日志、电路测量和当时的全部固件配置，不能把其中的归因视为实测证明。

需要区分三种现象：

1. **直接占用了串口引脚或更改了串口复用。** 当前 USB 调试保留 UART0 的 GPIO17/18；板1 R200 保留 GPIO15/16。实际误接到这些线，或程序把它们改成 GPIO，可能影响日志、接收或读卡。这是功能冲突，不由外设数量超过某个数字触发。
2. **外接电路影响上电或运行。** 蜂鸣器若存在较大负载、输入上拉、接错电源、共地问题、输出互相驱动等，会改变电源或电平；按钮按下会主动拉低线路，LED 电阻支路也会改变负载。LCD 的控制/数据输入与这些电路不同，不能仅按“都用了 GPIO”判断影响相同。对本次历史故障，这些是待验证的候选原因，尚不能确认是哪一种。
3. **把启动输出或 Flash 型号提示当作串口硬故障。** 开头短暂乱码、随后应用正常，与持续乱码、崩溃和反复重启有不同含义。具体是否为早期二进制输出或波特率差异，需要原始日志。旧记录直接写“bootloader 波特率不同”并不充分，本次查到 flashboot 默认参数也为 115200，不能一概断定 bootloader 都用另一波特率。

特别需要纠正：`Flash Init Fail! ret = 0x80001341` 在 `errcode.h` 中是 `ERRCODE_SFC_FLASH_NOT_SUPPORT`。SFC 驱动未匹配 Flash ID 时选用通用标准读、页编程参数；初始化成功后仍可以返回该型号提示码。flashboot 会打印提示并继续后面的启动流程。因此，这个特定错误码本身不能证明 GPIO6 干扰 Flash。其他错误码、无法读取程序或反复复位需要分别分析。

TCA9555 是 I2C 16 位 IO 扩展器，可通过 SCL/SDA 两根信号线控制更多输入输出，官方说明见 [TI TCA9555 产品资料](https://www.ti.com/product/TCA9555)。当前工程 SCL=GPIO13、SDA=GPIO14，将 LED/蜂鸣器/按键接在 TCA 端口后，不再让每个外设直接接入 WS63 的不同 GPIO，也减少了误占串口、错误复用和直接拉电平的机会。

这解释了 TCA 方案为什么合理，以及换方案后为何可能稳定；但换方案同时改变了接线、端口初始化和外设负载所在位置，单凭结果不能区分这些因素。TCA 也不是电源稳压器或电气隔离器，不能保证修复全部供电、干扰或软件故障。

当前 GPIO13 仍与板载 USER 按键共享，使用软件 I2C 时按下该键会把 SCL 拉低。TCA 正常不代表所有板载共享关系都消失。

## 5. 本次解决流程与使用的方法

1. 查看 Git 状态，确认存在用户未提交修改，保留现场。
2. 阅读工作区导航和索引，只用于定位主工程与厂商资料，不把旧索引当作当前实现。
3. 阅读 CMake 和角色配置，区分板1的读卡/扩展器与板2的 LCD。
4. 完整读取用户 PDF 三页，并渲染检查，定位“仅三个可用”和“全部 QSPI 脚禁用”的原文。
5. 读取并渲染 HH-D01 规格书接口页、开发板两页原理图和 35002 接口/背光电路页，逐项核对引脚和电气作用。
6. 核对 LCD 配置和初始化函数、SDK 复用合法性表、官方 SPI 驱动，确认五个 GPIO 与模式设置。
7. 沿错误码、SFC 未知型号回退、flashboot 打印提示三个位置追踪 `0x80001341`，纠正“提示即证明硬件冲突”。
8. 核对软件 I2C、R200、TCA/LED/蜂鸣器源码，解释扩展器占用与各板角色。
9. 将文档纠错、确认事实、候选原因和剩余验证分开，生成本文件。

## 6. 阅读文件及实际运用位置

下列工程相对路径以本文件所在的主工程为基准：

- 用户 `../HH D01可以使用的引脚及引脚不能用的解释pdf.pdf`：全部三页，用于核对原结论与依据。
- `../HH-D01可以使用的引脚.md`：“不用扩展板”和“使用 TCA9555”两段；发现全组禁用和旧 SCL=GPIO12 建议，当前不能照抄。
- `../CMakeLists.txt:30` 附近的角色源码分支：确认 B Client 不加入 R200/TCA 业务，而加入 LCD/UI。
- `profiles/board_b.config`：确认目标 B 角色启用 SLE Client、SPI 主机并关闭样例。当前工作配置为 A Server，不能据此认定实体 B 板烧的是 A 固件。
- `clearchain_lcd_config.h:12`～`:16`：五个 LCD 引脚定义；`clearchain_lcd.c` 的 `clearchain_lcd_init()` 和 `write_bytes()`：SCK/MOSI 模式 3、控制脚模式 0、软件 CS、只发送数据。
- `clearchain_soft_i2c.c:8`～`:9` 与 `release/drive_low`：确认当前 SCL=GPIO13、SDA=GPIO14，使用释放线路/主动拉低模拟开漏。
- `r200_uart.c:10`～`:13`：确认 UART1 与 GPIO15/16。
- `clearchain_tca9555.c:17` 附近的设备配置：第一颗低四位作输出，第二颗作输入；`clearchain_led.c:6`～`:10`、`clearchain_buzzer.c:6` 附近：确认灯用 P00/P01/P02，蜂鸣器用 P03。
- `解决双板元件接线混乱与旧引脚说明冲突_完整接线及逐项检查.md` 第 7、8 节：当前 LCD 五 GPIO、背光接 3.3V、MISO 不接及板1/板2接线区别。
- SDK `src/drivers/chips/ws63/include/platform_core.h:470`～`:496`：检查 QSPI 宏、条件编译分支和 XIP 定义，说明其不能直接证明板级全组占用。
- SDK `src/drivers/chips/ws63/porting/pinctrl/pinctrl_porting_ws63.c:41`～`:60`：检查引脚模式支持；`src/middleware/utils/hcc/host/hcc_spi_host.c:364`～`:367`：检查 GPIO7 时钟、GPIO9 输出的官方用法。
- SDK `src/include/errcode.h:236`：确认错误码名称；`src/drivers/chips/ws63/porting/sfc/driver/sfc.c` 的 `build_cmds()`、`uapi_sfc_init()`：确认未知型号参数回退和提示返回；`src/bootloader/flashboot_ws63/startup/main.c:40`、`:90` 附近：默认 115200 和打印提示后继续执行。
- `D:/ws/HH D01原理图及相关资料/HH-D01 星闪开发板规格说明书-20241212-V03.pdf` 第 5、6、8 页：内嵌 Flash、USER 按键、接口复用表。
- 同目录 `HH-D01 开发板原理图.pdf` 两页：模组到排针、USB 串口桥、GPIO13 按键和 GPIO05 彩灯共享。
- `D:/ws/lcd/转接板资料/SPI/3.5寸SPI接口模块35002数据手册.pdf` 第 3、5、6 页：3.3V、主接口定义、背光控制电路。
- `D:/markdown笔记/网络信息中心.md` 实验记录段、`D:/ws/文件用途说明/逐文件说明/解决HH-D01屏幕接线判断与无后端LCD自测阻塞_实施记录.md` 前段：定位历史接线现象和已存在的 QSPI 总括风险解释，未当作本轮实板验证。
- `WS63_LCD白屏与串口乱码异常排查与解决.md`、`WS63_LCD白屏_任务栈溢出根因与修复.md`：用于识别历史解释互相演变；没有依据这两份总结重新确认栈溢出，也没有用它们代替本次错误码源码分析。
- `D:/ws/AGENTS.md`、工作区导航、扫描说明及两份索引：用于定位资料和确认保护现有修改的要求。

## 7. 使用指令与逐句解释

以下是本次实际操作的精简可复用版本。每条命令前说明用途；不包含重编译、烧录或要求改变接线的命令。

```powershell
# 查看当前未提交文件，避免覆盖用户已有工作。
git -C D:/ws/fbb status --short
# 列出引脚说明和 LCD 配置的文件位置。
rg --files D:/ws/fbb -g '*引脚*' -g '*lcd*' -g '*9555*'
# 按 UTF-8 读取实际编译入口，避免中文显示乱码。
Get-Content -Encoding UTF8 -LiteralPath D:/ws/fbb/src/application/ws63/ws63_liteos_application/CMakeLists.txt
# 查看 LCD 的五个 GPIO 配置。
Get-Content -Encoding UTF8 -LiteralPath D:/ws/fbb/src/application/ws63/ws63_liteos_application/project/clearchain_lcd_config.h
# 定位 SDK 的两组 QSPI 宏及 XIP 能力定义。
rg -n 'QSPI0_D0|QSPI1_D0|QSPI1_CS|XIP_EXIST' D:/ws/fbb/src/drivers/chips/ws63/include/platform_core.h
# 沿 Flash 型号提示码和未知型号回退标记查找实现。
rg -n 'ERRCODE_SFC_FLASH_NOT_SUPPORT|g_sfc_unknown_flash' D:/ws/fbb/src/include/errcode.h D:/ws/fbb/src/drivers/chips/ws63/porting/sfc/driver/sfc.c
# 将 PDF 读取依赖安装在本工程临时目录，供这次资料核对使用。
python -m pip install --target D:/ws/fbb/tmp/pdf-analysis-deps pymupdf --disable-pip-version-check
# 让本次 Python 进程找到刚安装的 PDF 读取库。
$env:PYTHONPATH = 'D:/ws/fbb/tmp/pdf-analysis-deps'
# 让 Python 输出中文时使用 UTF-8。
$env:PYTHONIOENCODING = 'utf-8'
# 切换到工程根目录，使脚本中的工程相对路径正确。
Set-Location D:/ws/fbb
# 读取资料文字并渲染检查图片；脚本每句都附作用解释。
python 'tmp/pdfs/核对HH-D01引脚说明与LCD实际接线.py'
```

另外使用图片查看工具检查 PDF 渲染页，并通过网页工具查询 TI 官方产品资料和润和官方资料入口。第一次通过 PowerShell 标准输入传入含中文路径的 Python 文本时，中文被编码成问号导致路径无效；改为 UTF-8 脚本文件后读取成功，这属于电脑工具编码问题，与开发板串口故障无关。

## 8. 还需要什么才能判定之前的物理根因

需要当时每个外设对应的 GPIO、蜂鸣器是裸器件还是带 IN 的模块、外设供电和共地接法、实际固件及完整上电日志。优先区分：乱码只在开头一次，还是应用阶段持续；是否有反复启动、异常寄存器转储；Flash 提示的完整错误码是什么。

若需要恢复实验，应在保持同一固件和串口设置的前提下，从裸板开始，每次断电接入一个外设，记录冷启动和复位结果；必要时测量 3.3V 供电及相关信号。当前 TCA 接法已正常时，没有必要为证明旧文档而贸然恢复旧接线。

本次已确认：旧资料的全组 Flash 禁用结论证据不足；当前 LCD 五 GPIO 与厂商 SPI0 复用一致；BL 接固定 3.3V 不增加 GPIO 占用；TCA 扩展解释与源码一致；`0x80001341` 不等于已证明外接电路损坏或干扰 Flash。尚未确认：用户此次第六根线的实际接点，以及此前蜂鸣器/按键/LED 导致异常的单一物理根因。
