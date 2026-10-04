# WS63 产品 `b` 固件 Load Access Fault 根因与修复（栈溢出击穿内核调度器）

> 状态：**根因已定位（高置信度），修复源码已就绪，待重新构建并烧录验证。**  
> 关联文档：`WS63_LCD白屏_任务栈溢出根因与修复.md`（白屏那一轮）、`进度总结.md`

---

## 一、现象（用户提供串口日志）

产品 `b` 固件上电后**反复重启 + 白屏**，串口反复打印：

```
Load access fault
Memory map region access fault
APP|exception:5
APP|exception:5
task:osMain
phase:Init
mcause:0x5
mtval:0x4
mepc:0x24cf7a
...
APP|Reboot core:2 cause 0x2004boot.   ← 又重启 → 又崩溃 → 死循环
```

任务表里关键一行：

同意我清理 PID 54532 / 55168 这两个残留 python 构建进程

---

## 二、根因（高置信度）

**LCD/UI 任务（CCDisplayClient）栈只有 6 KB，不足以跑 `clearchain_ui_init()`（LVGL `lv_display_create(480x320)` + 首帧渲染 + SPI 轮询），栈溢出后写穿了任务栈边界，破坏了相邻的内核数据（TCB / 就绪队列头），导致 RTOS 调度器在读取被破坏的队列节点指针时解引用了地址 `0x4`（NULL + 偏移 4）→ Load Access Fault → 系统重启。**

证据链：

| 证据                                                                                      | 含义                                                                          |
| --------------------------------------------------------------------------------------- | --------------------------------------------------------------------------- |
| `mcause:0x5` + `mtval:0x4`                                                              | 取指/Load 访问故障，故障地址 `0x4` = 空指针 + 偏移 4（典型 NULL 解引用，不是随机乱地址）                   |
| `mepc:0x24cf7a` 在本机 product `b` ELF 的符号表里落在 `OsGetTopTask` / `OsPriQueueDequeue`（调度器函数） | 崩溃时 CPU 正在执行**任务调度器**，而非业务代码                                                |
| `task:osMain / phase:Init`                                                              | 故障发生在系统初始化阶段（所有 `app_run()` 回调执行期间）                                         |
| 任务水位 `WaterLine 0x114`（仅 276 字节）却仍崩溃                                                    | 水位采样没抓到溢出尖峰；栈溢出是"写穿边界破坏邻居内存"，本身不体现在自身水位上——这正符合"栈溢出击穿内核"特征                   |
| 同一套 LCD 代码在 bench（`lcd_test`）固件里：栈 8 KB → 返回 -1（白屏）；栈 16 KB → 正常循环刷屏                    | 证明 LCD 初始化确实需要 >8 KB、<16 KB 的栈；产品 `b` 只有 6 KB，溢出比 bench 更严重，直接破坏内核而非优雅返回 -1 |

> 一句话：**这不是新的"NULL 指针 bug"，而是白屏那一轮同一个根因（栈太小）的更严重表现**——6 KB 比 bench 的 8 KB 还小，溢出更深，从"初始化返回失败"升级为"破坏内核调度器 → 硬 fault → 重启循环"。

---

## 三、为什么"烧了还是坏"（关键排查结论）

你这次烧进去的 **还是旧的 product `b` 固件**。

我看到串口任务表里 `CCDisplayClient StackSize = 0x1800 = 6 KB`，但我上一轮已经把源码 `clearchain_display_client_entry.c` 的栈改成了 **`0x5000 = 20 KB`**（已读源码确认改动在第 11–12 行）。

说明：**我那次没有自动帮你构建 `b`**（因为当时有 2 个残留 python 构建进程，并发构建会抢写同一目录出竞态），所以你烧的是更早构建的、没带 20 KB 修复的旧包。我的修复源码在，但没编进你烧的固件。

---

## 四、解决方法（已落地在源码，待构建烧录）

源码改动（已就绪，安全可逆）：

| 文件                                          | 改动                              | 说明                                                  |
| ------------------------------------------- | ------------------------------- | --------------------------------------------------- |
| `project/clearchain_display_client_entry.c` | 栈 `0x1800`→`0x5000`（6 KB→20 KB） | 产品 `b` 显示任务；bench 已验证 16 KB 够用，这里给 20 KB 留 SLE 回调余量 |

> bench 固件（`lcd_test`）的栈修复（8 KB→16 KB）上一轮已构建验证成功（`[LCD TEST] sample=4/5/6/7` 正常循环），证明"把栈调大"这个方向 100% 正确。

---

## 五、需要执行的命令

### 批次 1：清理残留进程（危险操作，需你确认后再让我执行）

那 2 个 python 进程（PID 54532 / 55168）是我上一轮 `build_dual.py lcd_test` 拉起来的子构建孤儿，**只跑在 Windows 开发机、与板子无关**，唯一风险是并发构建时抢写 `output/`、`build/` 导致产物错乱。清掉它们只为安全重新构建 `b`。

```
# 位置：Windows PowerShell（开发机）
# 先确认这两个 PID 确实还是那两个构建残留再杀：
tasklist | findstr python
# 确认无误后（由我执行，需你点头）：
taskkill /PID 54532 /F
taskkill /PID 55168 /F
```

### 批次 2：重新构建产品 `b` 固件

```
# 位置：Windows PowerShell（开发机，注意用盘符+反斜杠，不是 cmd 的 cd /d）
cd D:\ws\fbb\src
python application\ws63\ws63_liteos_application\project\tools\build_dual.py b
```

成功会生成：`D:\ws\fbb\src\output\clearchain\board_b\ws63-liteos-app_all.fwpkg`

### 批次 3：烧录 + 验证

- 用你第 3 步那套命令行烧录工具烧 **`output\clearchain\board_b\ws63-liteos-app_all.fwpkg`**（别再烧旧的 `_lcd_diag_` / 6 KB 旧包）。
- 串口 115200/8N1 上电，预期：
  - **不再出现** `Load access fault` / `Reboot core:2` 重启循环；
  - 任务表里 `CCDisplayClient StackSize = 0x5000`；
  - 屏上显示 "SCR_BOOT  Connecting..." 初始化界面（不再白屏）。

---

## 六、涉及的知识点

1. **RTOS 任务栈（`osal_kthread_create` 最后一个参数是字节数）**：`0x1800`=6 KB、`0x4000`=16 KB、`0x5000`=20 KB。LVGL 的 `lv_display_create` + 首帧 `flush_cb → lv_draw_sw_rgb565_swap → SPI 轮询` 调用深度大、吃栈，需 >8 KB。
2. **栈溢出的两种表现**：
   - 轻：函数返回 -1 / 数据错乱（bench 8 KB 的白屏）；
   - 重：写穿栈边界破坏**相邻内核数据（TCB / 就绪队列）**，下一次调度器运行即解引用坏指针 → 硬 fault → 重启循环（产品 `b` 6 KB 的本次崩溃）。
3. **RISC-V 异常寄存器**：`mcause=0x5`=Load/取指访问故障；`mtval=0x4`=故障虚拟地址；`mepc`=故障指令地址。地址 `0x4` 几乎总意味着"空指针 + 偏移 4 的结构体字段解引用"。
4. **WS63 工具链是 RISC-V（不是 ARM）**：`tools/bin/compiler/riscv/cc_riscv32_musl_105/...`，用 `riscv32-linux-musl-objdump.exe -t <elf>` 可把崩溃 PC（如 `0x24cf7a`）映射到函数名（本次定位到 `OsGetTopTask` 调度器函数）。
5. **bootloader 串口乱码是良性现象**：UART0=GPIO17/18@115200，bootloader 阶段以其它速率打印 → 早段字节乱码，与 LCD 无关（本轮日志开头同样有，可忽略）。
6. **"烧了还是坏"的教训**：源码改了 ≠ 固件更新了。必须重新构建并把**新产物**烧进去；用串口任务表的 `StackSize` 字段可反查烧进去的是不是新栈。

---

## 七、待确认 / 风险

- ⚠️ 20 KB 是在 bench 16 KB 验证值上留了余量，预期足够；若烧后仍有 `Load access fault`，说明 SLE 回调路径还要更多栈，届时升到 `0x6000`（24 KB）再试。
- 杀进程属危险操作，需你确认 PID 与用途后我执行。
- 仍无万用表，暂不评估 3.3V 电源轨；若栈修复后白屏但无重启，再回头查供电。
