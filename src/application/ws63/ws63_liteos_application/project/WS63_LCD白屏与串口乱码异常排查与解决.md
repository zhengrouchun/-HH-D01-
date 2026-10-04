# WS63 LCD 白屏 + 串口乱码异常 — 排查与解决记录

> 关联固件：`D:\ws\fbb\src\application\ws63\ws63_liteos_application\project\clearchain_lcd_selftest.c`
> 烧录包：`D:\ws\fbb\src\output\clearchain\board_b_lcd_test\ws63-liteos-app_all.fwpkg`（见下方"关键修正"）
> 状态：**根因未完全闭环。"串口异常"已定位为良性现象；已新增"失败原因反复横幅"诊断，下一轮烧录后无需滚屏即可在稳定日志里看到 LCD 初始化失败的具体步骤（step + 错误码）。**

---

## 一、问题现象（用户描述）

1. 裸板（只接 HHD01 开发板）上电，串口一打开就出现"异常"（乱码）：
   `彽{!►B琸zic剠?■1/鈵9眵鞃boot.` 前缀 + 后续正常日志。
2. 接上 LCD（B 板 GPIO7→SCK、GPIO9→MOSI、GPIO8→CS、GPIO1→DC、GPIO14→RESET；3.3V→VCC、共地；LED 与 VCC 都接到 3.3V）后：
   - 烧录后 LCD **白屏、不显示图像**。
   - 打开串口"出现异常"。
3. 用户强调：**不管接不接 LCD，串口都有这个异常**。

---

## 二、当前已知情况（从串口日志读出的事实）

日志实际是一份**干净启动**，最后停在：
```
[LCD TEST] LCD/LVGL init failed
xo update temp:3,diff:0,xo:0x3083c
APP|[SYS INFO] mem: used:89588, free:105296; ...
```
要点：
- 启动全过程**没有崩溃**：调度器进入、雷达 timer 初始化成功、RF 校准 `rf cali OK`、最后 `SYS INFO` 正常打印。
- `[LCD TEST] LCD/LVGL init failed` 是 `clearchain_ui_init()` 返回 `-1` 的**受控错误返回**，不是异常/崩溃。
- 白屏 = 初始化失败后 `fill()/text_band()` 从未执行，面板只剩背光 → 白。

---

## 三、关键结论（已查证）

### 结论 1：串口"乱码异常"是 bootloader 阶段输出，与 LCD 无关，属良性
- 调试/日志串口是 **UART0，物理脚 GPIO17/18，波特率 115200**（见 `ws63_ssb.config`：`CONFIG_DEBUG_UART=0`、`CONFIG_DEBUG_UART_BAUDRATE=115200`；日志口 UART1=GPIO15/16@921600）。
- 你的 LCD 接在 **GPIO 1/7/8/9/14**，**完全不碰** UART0(17/18) 与 UART1(15/16)。所以 LCD 接线**不可能**短路串口 → 这就解释了"不管接不接 LCD 都有乱码"。
- 乱码前缀是 **bootloader / 早期启动阶段**以"应用未配置前的波特率"打印的内容；应用跑到 `main.c` 的 `sw_debug_uart_init()` 之后才把 UART0 设成 115200（日志里 `APP|dbg uart init ok` 之后才是干净文本）。终端速率不匹配 → 早期字节显示为乱码（含中文编码的二进制即 CJK 乱码）。
- `Flash Init Fail! ret = 0x80001341` 也是 bootloader 的 flash 型号告警，已在第 5 步确认：SDK 定义为"Flash 型号不受支持 + 通用参数回退"，板子仍能跑，不能据此认定 Flash 损坏。

### 结论 2：SPI 引脚是对的，排除"引脚复用接错"这个常见坑
- WS63 的 SPI_M0（即 `SPI_BUS_0`，基地址 `0x44020000`）官方约定引脚（`middleware/utils/hcc/host/hcc_spi_host.c`）：`CLK=GPIO_07, DO/MOSI=GPIO_09, DI/MISO=GPIO_11, CS=GPIO_10`。
- 你的接线 **SCK=GPIO7、MOSI=GPIO9 与官方一致**；CS 你用 GPIO8 作手动片选（代码里是 `uapi_gpio_set_val` 软控，不是硬件 CS），DC=GPIO1、RESET=GPIO14 都是普通 GPIO —— 全部合法（`pinctrl_porting_ws63.c` 中 GPIO_07/08/09/01/14 都允许 `PIN_MODE_3`）。
- 因此 **引脚路由不是白屏原因**。

### 结论 3：白屏 = LCD/LVGL 初始化失败的具体某一步返回 -1
- `clearchain_lcd_selftest.c` 的 `lcd_test_task`：`clearchain_ui_init()` 非 0 → 打印 `LCD/LVGL init failed` → `return NULL`（任务结束，受控）。
- `clearchain_ui_init()`（`clearchain_ui.c`）只有两处返回 -1：
  1. `clearchain_lcd_init()` 失败 → 打印 `[CLEAR UI] LCD driver init failed before LVGL`
  2. `lv_display_create()` 返回 NULL → 打印 `[CLEAR UI] LVGL display allocation failed`
- 失败的具体步骤在 `[CLEAR LCD] ... failed: 0x%x` / `[CLEAR LCD] SPI write failed: 0x%x` 这些行里 —— **它们在你贴出的日志之前（被终端滚掉了）**，所以目前还看不到是哪一步。

### 结论 4（本轮读驱动源码进一步收窄）：-1 只能来自 `uapi_spi_init` 本身或引脚/GPIO 设置，SPI 写本身不会失败
- **DMA 已排除**：`build/.../ws63_liteos_app.config` 中 `CONFIG_SPI_SUPPORT_DMA is not set`、`CONFIG_SPI_SUPPORT_POLL_AND_DMA_AUTO_SWITCH` 也未开 → `uapi_spi_master_write` 走纯轮询 `hal_spi_v151_write`，**960 字节整行 fill 不会切到 DMA**，所以"小命令成功、大块 fill 失败"的假设不成立。
- **tmod=1=TX 是正确的**：`hal_spi.h` 枚举 `TXRX=0, TX=1, RX=2`；`uapi_spi_master_write` 只拒绝 `tmod==RX`，我们设 TX 正好放行。SDK 官方 `spi_master_demo.c` 也是 `tmod=0(TXRX)/TX`，可印证。
- **MODE_MISMATCH 已排除**：`hal_spi_v151_init()` 在 `attr->is_slave!=1` 时会显式 `spi_porting_set_device_mode(bus, SPI_MODE_MASTER)`，`uapi_spi_init` 成功后置 `g_spi_is_initialised=true`；因此一旦 init 成功，`spi_param_check` 的 master 校验必过，`uapi_spi_master_write` 不会被 `ERRCODE_SPI_MODE_MISMATCH` 拦。
- **`hal_spi_v151_write` 对长度无上限**：只校验 `tx_bytes % frame_bytes==0`（8 位帧下恒为真），其余直接进 FIFO 轮询返回 SUCC；所以写入函数本身不为某条命令返回错误。
- → 因此 `[LCD TEST] LCD/LVGL init failed` 的 -1，**只能**来自：
  1. `uapi_spi_init` 自身返回非 0（→ `[CLEAR LCD] SPI init failed: 0x...`），或
  2. 某次 `uapi_pin_set_mode` / `uapi_gpio_set_dir` / `uapi_gpio_set_val`（SCK/MOSI/CS/DC/RESET 引脚配置）返回非 0（→ `[CLEAR LCD] <操作> failed: 0x...`）。
  - 新固件（当前正在构建）会把这一步的名字 + 错误码通过 `[CLEAR LCD] ...` 与每 3 秒刷新的 `[LCD TEST] LAST LCD ERROR step=<> code=0x<>` 直接打印出来，无需滚屏。

---

## 四、关键修正（用户提供的烧录路径需核对）

你写烧录文件是 `...\output\clearchain\board_b\ws63-liteos-app_all.fwpkg`，
但串口日志里是 `[LCD TEST] task started`（来自 `clearchain_lcd_selftest.c`）。
按 `build_dual.py`：
- `role='b'` 产出 `board_b\`，包含 `clearchain_display_client.c`，**不含** selftest。
- `role='lcd_test'` 产出 `board_b_lcd_test\`，包含 `clearchain_lcd_selftest.c`。

→ 当前**实际运行的是 lcd_test 固件**。请确认你烧的是 `output/clearchain/board_b_lcd_test/ws63-liteos-app_all.fwpkg`，不是 `board_b/`。路径写成 `board_b` 可能是简称，但务必核对，否则排查对象不对。

---

## 五、已做的代码改动（安全、可逆、为定位服务）

文件：`project/clearchain_lcd_selftest.c`

1. **任务栈 8 KB → 16 KB**（`0x2000` → `0x4000`）
   - 原因：LVGL 渲染路径（`flush_cb` → SPI 轮询循环）在 8 KB 栈下可能溢出 → 硬 fault → 寄存器 dump 被终端当二进制显示成乱码 → 你看到的"串口异常"也可能是这个（叠加在 bootloader 乱码之上）。
   - 风险：16 KB 仍远小于 SYS INFO 显示的 ~105 KB 空闲，安全。
2. **初始化失败路径加受控提示**
   - 把"init failed"改为明确说明"任务受控停止、非崩溃，回去看上面 [CLEAR LCD]/[CLEAR UI] 行"，并提醒白屏是预期现象。避免你把它误判为崩溃。

未改 `clearchain_ui.c`；但本轮**额外改了 `clearchain_lcd.c`**：
- 新增全局 `g_lcd_fail_step / g_lcd_fail_code`，在每次 `check_io` 失败和 SPI 写失败时记录"哪一步 + 错误码"。
- 新增 `clearchain_lcd_get_last_error()` 取值函数（声明在 `clearchain_lcd.h`）。
- `clearchain_lcd_selftest.c` 的失败分支改为：**每 3 秒反复打印 `[LCD TEST] LAST LCD ERROR step=<> code=0x<>`，无限循环**。这样即使早期 `[CLEAR LCD]` 行被终端滚掉，你也能在稳定后的日志里一眼看到失败步骤，不用再去滚屏找。

---

## 六、下一步验证（批次 1：取到完整日志，锁定失败步骤）

> 在哪执行：命令都在**开发机（Windows，git bash 或 powershell）**跑；串口用任意终端（Putty/串口助手），**波特率固定 115200**。

### 步骤 1：重新编译 lcd_test（已含上述栈修正 + 失败横幅）
```powershell
# ⚠️ 上一次失败的原因：用了 cmd 的 `cd /d` 写法，PowerShell 不认，
#    被当成 `C:\d\ws\fbb\src` 找不到目录，python 于是跑到 system32 下报错。
#    PowerShell 里请用下面这种（反斜杠或盘符直接切）：
cd D:\ws\fbb\src
python application\ws63\ws63_liteos_application\project\tools\build_dual.py lcd_test
```
- 若提示 `python` 不是命令，改用 SDK 自带解释器绝对路径：
  `C:\Users\Stu15\.workbuddy\binaries\python\versions\3.13.12\python.exe application\ws63\ws63_liteos_application\project\tools\build_dual.py lcd_test`
- 成功会打印 `SUCCESS ...\output\clearchain\board_b_lcd_test`。若失败看 `output/clearchain/board_b_lcd_test/build.log`。
- **确认烧的是新包**：`D:\ws\fbb\src\output\clearchain\board_b_lcd_test\ws63-liteos-app_all.fwpkg`（不是你之前那个 `..._lcd_diag_*.fwpkg` 旧包——那是没有本次诊断日志的旧固件）。

### 步骤 2：烧录（用你第 3 步验证过的命令行工具，ACK 超时就重试一次）
烧 `D:\ws\fbb\src\output\clearchain\board_b_lcd_test\ws63-liteos-app_all.fwpkg`。

### 步骤 3：抓**完整**串口日志（关键！）
- 终端 115200、8N1、勾"上电即录制/记录到文件"。
- **断电 → 上电**，从第一个字节开始全选复制（含最前面的乱码），一直到 `SYS INFO` 之后至少 10 秒。
- 重点看这两类行是否出现、以及出现在哪一步：
  - `[CLEAR LCD] init begin: SPI0 2MHz mode0 ...`
  - `[CLEAR LCD] <操作> failed: 0x%x` 或 `[CLEAR LCD] SPI write failed: 0x%x ...`
  - `[CLEAR UI] LCD driver init failed before LVGL` 或 `[CLEAR UI] LVGL display allocation failed`
- 把这一段贴回来。

### 步骤 4：判定（批次 1 验证点）
- **情形 A（最可能）**：乱码只在 `Flash Init Fail` 之前出现一次，之后 `SYS INFO` 干净结束，且能看到 `[CLEAR LCD]/[CLEAR UI]` 指明失败步骤 → "串口异常"= bootloader 噪声（良性），只需修 LCD 初始化那一步。
- **情形 B**：`SYS INFO` 之后又出现乱码 + 反复重启（看是否重复打印 `Flash Init Fail`/boot 字样）→ 存在真实硬 fault（LVGL 栈已加，但仍可能在 radar/RF 任务）。需要把崩溃前后的完整字节贴回来。

---

## 七、取到横幅后如何判定（错误码 → 修复矩阵）

新固件烧录后，串口稳定区会反复打印：
`[LCD TEST] LAST LCD ERROR step=<> code=0x<>`
`step` 取最近一次 `check_io` 失败的操作名；`code` 是 SDK 错误码（`include/errcode.h`）。按表对号入座：

| 横幅 step 内容 | 含义 | 直接修复方向 |
|---|---|---|
| `SPI init` | `uapi_spi_init` 返回非 0 | `clearchain_lcd.c` 里 `spi_attr_t` 配置问题：`bus_clk`/`freq_mhz`/`frame_size`/`frame_format` 取值；对照 SDK `spi_master_demo.c` 的 `spi_attr_t` 逐项对齐（尤其 `frame_format`/`spi_frame_format` 是否重复设了冲突值）。 |
| `SCK pinmux` / `MOSI pinmux` | GPIO7/9 切 `PIN_MODE_3` 失败 | 查 `pinctrl_porting_ws63.c` 中 GPIO7/9 的 `PIN_MODE_3` 是否真映射到 SPI_M0 的 SCK/DO；若芯片 IOMUX 把 SPI_M0 放在别的脚（如 GPIO10/11），需改 `clearchain_lcd_config.h` 的 `SCK/MOSI` 定义或改接板子引脚。 |
| `control pinmux` | CS/DC/RESET 切 `PIN_MODE_0` 失败 | GPIO1/8/14 中某个脚在 `PIN_MODE_0` 不是 GPIO（被固定功能占用）。换一个空闲 GPIO 作该信号，或确认该脚确实可作 GPIO。 |
| `GPIO output` / `GPIO initial high` | `uapi_gpio_set_dir/set_val` 失败 | 同上，脚未正确初始化为 GPIO 输出。先 `uapi_pin_set_mode(pin, PIN_MODE_0)` 再 `gpio_init`（代码已先调 `uapi_gpio_init()`，但仍可能是该脚不支持 GPIO 输出）。 |
| `RESET low` / `RESET high` / `CS low` / `CS high` / `DC value` | 控制脚 IO 失败 | 脚定义错或该脚无输出能力；核对 `clearchain_lcd_config.h` 的 `CS/DC/RESET` 引脚号与实物接线。 |
| `SPI write` | `uapi_spi_master_write` 返回非 0 | 见下方"写失败细分"。注意：init 阶段命令都是 1~16 字节，轮询必成功，若此处失败几乎只能是 init 没成功建立 master 模式（已被结论 4 排除）或脚被占用导致 FIFO 卡死 → 先解决上面的 pinmux 项。 |
| （step 为 NULL） | LCD 驱动本身返回 0，但 `clearchain_ui_init` 仍失败 | 失败在 LVGL 层：`[CLEAR UI] LVGL display allocation failed` → 堆不够，`g_draw_buffer`(30 KB)+`g_line`(960 B) 超出可用堆；调小 `lv_display_set_buffers` 的 `g_draw_buffer` 或增大 LVGL 内存池。 |

**写失败细分**（step=`SPI write` 时的 code）：
- `0x80001332` `ERRCODE_SPI_MODE_MISMATCH`：理论上已被结论 4 排除（init 成功即 master），若仍出现说明 `uapi_spi_init` 实际上没成功但你没抓到 `SPI init failed` 行 —— 优先看 `SPI init` step。
- `0x8000133E` `ERRCODE_SPI_INVALID_TMODE`：tmod 被设成 RX；但代码固定 `tmod=1(TX)`，不该出现，若出现说明 `clearchain_lcd_config.h` 或别处改了 tmod。
- `0x800013xx` 其它 SPI 超时/配置类：多半是引脚复用没真正生效导致 SPI 外设没时钟/没连上 → 回到 `SCK/MOSI pinmux` 项。

> 注意：`uapi_spi_master_write` 对"从机无应答"不报错——它只负责把 FIFO 排空。所以**若 `[CLEAR LCD] init begin` 之后一条 `failed` 都没有、却仍然白屏**，那一定是**控制器/初始化序列不匹配**（屏不认识这些命令，停在白），与上面这些"返回 -1"的情形是两回事，此时需要按屏 datasheet 重写 `setup[]`（见下方第八节候选 B）。

## 八、仍待确认的两个候选根因（取到日志后即可二分）

1. **LCD 驱动初始化某步返回非 0（结论 4 收窄后的主因）**
   - 验证：横幅 step 落在 `SPI init` / 任一 `pinmux` / `GPIO` / `SPI write` → 按第七节矩阵修。
2. **面板初始化序列与手头屏不匹配**（即使 SPI 通了也白屏，且不返回 -1）
   - 当前序列按本地 HiHope ST7796 参考写（`0x36/0x3A/0xF0 C3 96/0xB4/0xB7/0xC0..0xC5/0xE8/0xE0/0xE1/0xF0 3C 69/0x21/0x29`）。若手头是 ILI9488 / ST7789 等不同控制器，命令会被误解，面板停在白。
   - 验证：取到 `[CLEAR LCD] init begin` 之后**没有任何 failed** 却仍白屏 → 100% 是控制器/序列不匹配，需要按屏的 datasheet 重写 `clearchain_lcd.c` 的 `setup[]`。

---

## 八、涉及知识点（点给你）

- **UART 调试口固定占用**：WS63 的 UART0(GPIO17/18) 是调试+日志+AT 唯一通道，绝不能挪用；日志消失/乱码优先怀疑串口脚被抢。
- **bootloader 与应用波特率不一致**：上电早期是 bootloader 说话，应用 `sw_debug_uart_init` 后才切到 115200，速率不匹配的早段字节就是乱码（非崩溃）。
- **引脚复用（PINMUX）**：物理脚同一时刻只能给一个外设；`PIN_MODE_3` 在表里"允许"≠"映射到 SPI0"，真正路由由芯片 IOMUX 决定（已用 SDK 官方 SPI host 驱动交叉验证 GPIO7/9 即 SPI_M0）。
- **SPI 主机写不校验从机**：`uapi_spi_master_write` 只把 FIFO 排空，即使屏没接也返回 SUCC → "初始化返回成功但白屏"是可能的，需靠具体命令/分配失败来定位。
- **任务栈溢出 → 硬 fault**：LVGL 渲染栈较深，8 KB 偏小；现象是随机崩溃/寄存器 dump 乱码，加大栈即可排除。
- **受控错误返回 ≠ 崩溃**：`return NULL` 的任务结束与 hard fault 是两回事，日志里要看清是哪种。

---

## 九、风险与注意

- 我没有执行任何删除/杀进程/云服务器操作，本任务纯本地代码分析与小步修改。
- 改的是 `clearchain_lcd_selftest.c`（仅 selftest 固件编译进 `board_b_lcd_test`），不影响 `board_a/board_b` 业务固件。
- 切勿在 `git` 里 `reset/checkout` 覆盖 `fbb_ws63/` 与 `fbb/` 你已有的修改（按 AGENTS.md 先 `git -C D:\ws status --short`）。
- LCD 的 **LED 脚直接怼 3.3V** 需确认你的屏模块是否内置限流；若屏背光电流大，会拉低 3.3V 轨导致 MCU 掉电复位（这也会表现为"乱码+重启"）。批次 2 建议用万用表量一下接屏前后 3.3V 是否稳定。
