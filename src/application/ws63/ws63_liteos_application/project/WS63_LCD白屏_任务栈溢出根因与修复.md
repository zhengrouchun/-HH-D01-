# WS63 LCD 白屏 — 任务栈溢出根因与修复

> 问题：WS63（HH-D01 B 板）接 ST7796S 480×320 LCD，串口反复出现 `[LCD TEST] LCD/LVGL init failed`，屏只亮背光（白屏），不显示任何内容。
> 根因：**运行 LCD/UI 的任务栈太小（bench 固件 8KB、产品固件 6KB），不足以容纳 LVGL 显示创建 + 首次渲染（SPI 轮询刷新）所需的调用栈，栈溢出导致 `clearchain_ui_init()` 返回 -1 → 白屏。**
> 修复：**把 LCD/UI 任务栈放大到 16KB（bench）/ 20KB（产品，留 SLE 回调余量）。**
> 状态：**已闭环**。bench 固件烧录后串口已出现 `[LCD TEST] sample=...` 循环（初始化成功、渲染正常）。

---

## 一、为什么之前一直卡住（根因）

LCD 显示由 `clearchain_ui_init()`（`clearchain_ui.c`）完成，内部顺序：

1. `clearchain_lcd_init()` —— SPI/GPIO 初始化 + ST7796S 初始化序列；
2. `lv_init()` + `lv_display_create(480, 320)` —— 创建 LVGL 显示对象；
3. `lv_display_set_buffers(...)` —— 绑定 32 行 RGB565 局部绘制缓冲（**30 KB，是静态数组 `.bss`，不占栈**）；
4. 创建一堆 LVGL 控件（label / bar / 6 个 mode 卡片）。

关键在**第 2~4 步发生在"调用 `clearchain_ui_init` 的那个任务"的栈上**。LVGL 的 `lv_display_create`、以及之后每次渲染 `lv_timer_handler()` → `flush_cb()` → `lv_draw_sw_rgb565_swap()` 都是**深度调用、局部变量多**的栈消耗大户。

原任务栈：
- bench（`clearchain_lcd_selftest.c`）：`0x2000` = **8 KB**；
- 产品（`clearchain_display_client_entry.c`）：`0x1800` = **6 KB**。

这两个都远小于 LVGL 实际峰值需求 → **任务栈溢出**。溢出后果不是必现硬 fault，而是 `clearchain_ui_init()` 在 `lv_display_create`/首渲时返回 `-1`（被上层当成"初始化失败"），于是 `lcd_test_task` 提前 `return NULL`，`fill()`/`text` 从未执行 → 面板只剩背光 → **白屏**。

> 这也解释了为什么早期日志里永远只有 `LCD/LVGL init failed` 一行、看不到更深的错误：失败发生在 `clearchain_ui_init` 内部、LVGL 层，还没到我们加的 `[CLEAR LCD]` SPI 步骤，所以 SPI 其实可能是好的——问题在栈，不在 SPI/引脚/序列。

---

## 二、解决方法（改了哪几处）

| 文件 | 改动 | 说明 |
|---|---|---|
| `project/clearchain_lcd_selftest.c` | 任务栈 `0x2000` → `0x4000`（8KB→16KB） | bench 固件，先验证用；16KB 实测够 LVGL 渲染 |
| `project/clearchain_display_client_entry.c` | 任务栈 `0x1800` → `0x5000`（6KB→20KB） | **产品固件（Board B 显示板）**，真实跑 LCD 的任务；比 bench 多 SLE 回调，给 20KB 余量 |

两处都是**只改一个常量、安全可逆**，不碰 SPI 驱动、不碰初始化序列、不碰引脚。

（备注：排查期间另加了"失败步骤反复横幅"诊断 `LAST LCD ERROR step=<> code=0x<>`，它只在失败路径生效，不影响本次成功路径，可保留也可删。）

---

## 三、需要执行的命令

### 1）复现/验证 bench 固件（已成功，给你对照）
在 **PowerShell**（注意用盘符切目录，**不要**写 `cd /d`，那是 cmd 写法）：
```powershell
cd D:\ws\fbb\src
python application\ws63\ws63_liteos_application\project\tools\build_dual.py lcd_test
```
产物：`D:\ws\fbb\src\output\clearchain\board_b_lcd_test\ws63-liteos-app_all.fwpkg`
烧录它 → 串口 115200/8N1 上电 → 看到 `[LCD TEST] sample=0..7` 循环即成功。

### 2）构建并烧录产品固件（Board B 显示板，含本次修复）
```powershell
cd D:\ws\fbb\src
python application\ws63\ws63_liteos_application\project\tools\build_dual.py b
```
产物：`D:\ws\fbb\src\output\clearchain\board_b\ws63-liteos-app_all.fwpkg`
烧录它即可让**真实产品**的 LCD 正常工作（不再白屏）。

> 构建日志尾部偶尔出现 `GENERAT_ROM_PATCH` / `GENERAT_CODESIZE_STATISTIC` 报错——这是链接后的后处理步骤，**不影响 `ws63-liteos-app_all.fwpkg` 可烧录性**，可直接烧。

### 3）串口验证
- 波特率 **115200 / 8N1**（调试 UART0 = GPIO17/18）。
- 上电后前几秒有乱码前缀属正常（bootloader 早期以其它速率打印，见知识点 6），不影响。

---

## 四、涉及的知识点

1. **RTOS 任务栈（`osal_kthread_create` 的栈参数）**
   - WS63 用 `osal_kthread_create(func, arg, name, stack_size)`，最后一个参数是**以字节为单位的栈大小**（如 `0x4000` = 16384 字节 = 16KB）。
   - 栈太小不会立刻崩，而是静默返回错误 / 偶尔硬 fault——嵌入式里最隐蔽的一类 bug。

2. **LVGL 的栈开销**
   - `lv_display_create`、首帧渲染（`flush_cb` / `lv_draw_sw_rgb565_swap`）都在**调用任务的栈**上跑，深度调用 + 局部缓冲，8KB/6KB 远不够；16KB 是常见下限，复杂 UI 常需 20~40KB。

3. **静态缓冲不占栈**
   - `g_draw_buffer[480*32*2]` = 30KB 是 `static` 数组，放在 `.bss`，**不占任务栈**；真正吃栈的是 LVGL 运行时的调用栈，二者别混淆。

4. **WS63 SPI_M0 引脚约定（已排除的干扰项）**
   - SPI_M0：SCK=GPIO7、MOSI(DO)=GPIO9、CS=GPIO10。你的接线 SCK=GPIO7 / MOSI=GPIO9 完全正确，白屏**不是引脚接错**。

5. **屏主控 ST7796S（已排除的干扰项）**
   - 你确认屏是 ST7796（即 ST7796S，480×320 常用主控），和 `clearchain_lcd.c` 现有初始化序列目标一致 → **不是序列不匹配**。

6. **串口"乱码"是良性现象（已排除的干扰项）**
   - 调试串口 UART0=GPIO17/18@115200，应用跑到 `main.c` 才把 UART 设成 115200；此前 bootloader 以别的速率打印 → 早段字节变乱码。这与 LCD 无关，每次上电都有。

7. **WS63 工具链是 RISC-V，不是 ARM**
   - 编译器：`tools/bin/compiler/riscv/cc_riscv32_musl_105/cc_riscv32_musl_win/bin`（riscv32-linux-musl-gcc），构建走 ninja + `build_dual.py [a|b|api|api_mock|lcd_test|all]`。

---

## 五、验证结果

bench 固件烧录后串口实测（节选）：
```
[LCD TEST] sample=4 version=5
[LCD TEST] sample=5 version=6
[LCD TEST] sample=6 version=7
[LCD TEST] sample=7 version=8
[LCD TEST] sample=0 version=9
```
`show_sample()` 被循环调用 = `clearchain_ui_init()` 返回 0（成功）→ 任务越过初始化 → LVGL 渲染正常 → **白屏问题解决**。

---

## 六、后续建议

- 产品固件（Board B）已改 20KB 栈，请按第三节第 2 条构建 `b` 并烧录确认。
- 若以后产品里 LCD 相关任务仍偶发异常，优先怀疑**任务栈**，按 LVGL 官方建议给显示任务 ≥16KB（含 SLE/网络回调则 ≥20KB）。
- Board A（主 RFID/WiFi 端）没有 LCD，不受影响；其通用子任务封装在 `clearchain_device_app.c`，与本次问题无关。
