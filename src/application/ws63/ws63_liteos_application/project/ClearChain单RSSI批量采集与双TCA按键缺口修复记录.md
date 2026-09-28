# ClearChain 单 RSSI 批量采集与双 TCA9555 按键缺口修复记录

## 问题与原因

现有 R200 协议层已经解析 `frame[5]` 为 `int8_t` RSSI，但 Reader 正式批次仍按旧的 9 标签、A/B/C 三位置、samples 与 median 建模；主循环只读取单 EPC 并发 `/scan`。第二片 TCA9555 的驱动抽象已存在，但九个导航和 D 键未进入按键任务。HTTP 仅有 `/scan`，已有双板 SLE 链路尚未与未确定的批量业务契约分开记录。旧的 `Our Next Move.docx`、`Embedded.pdf` 含与当前 report 冲突的方案，是模型错位的来源；本轮以 `report(1).pdf` 和用户已确认规则为准。

## 资料与实际用到的位置

- `report(1).pdf`：第 1 页确认 D1-D4、History 阶段开放；第 2 页确认 CP 为独立只读 `/verify_scan`；第 2-3 页确认每 EPC 一个 RSSI、约 20 标签和无固定位置；第 3-4 页确认 `/factory_scan`、`/verify_scan` JSON 与继续使用 ngrok。
- `clearchain第三批问题.docx`：段落 0-23 是 batch 响应与调用顺序疑问；段落 25-209 列举尚未选择的 `batch_id` 来源；段落 211-213 冻结真实风险分数与进度；段落 419-465 的演示方案不作为最终业务实现。
- `Our Next Move.docx`、`Embedded.pdf`：只用于识别旧 A/B/C、多位置与旧资源判断，不作为当前实现依据。
- `r200_protocol.c`：核对 `r200_protocol_parse_inventory()` 已从 `frame[5]` 取有符号 RSSI，保持未改。
- `r200_uart.c`：核对 UART1、GPIO15/16、RX 回调、环形缓冲和 `r200_uart_wait_frame()`；保持未改。
- `r200_reader.h/.c`：定位旧 `observations`、`samples`、`median`、`r200_reader_collect()`，替换为首读 RSSI 批次。
- `clearchain_tca9555.h/.c`、`clearchain_soft_i2c.c`：核对 0x20/0x21 器件抽象及固定 GPIO13/14。
- `clearchain_key.h/.c`：沿用 S1-S5 模式变量和两次稳定采样的消抖，扩展 0x21 按键。
- `clearchain_http.h/.c`、`tcp_client_demo.c`：保留单标签 `/scan`，增加已确认 API，并在业务边界留 TODO。
- `clearchain_display_link.*`、`clearchain_display_client.*`、`clearchain_display_protocol.h`、主工程 `CMakeLists.txt`、`src/application/Kconfig`：核对 SLE Server/Client 入口、协议和编译选择。

## 修改过程与方法

1. 使用 `rg --files`、`rg -n`、`Get-Content`、`git status --short` 检查现有文件和未提交改动；用 Python 的 `pypdf.PdfReader` 与 `docx.Document` 完整提取四份附件，明确 report 优先级及冻结项。
2. 保留 `r200_protocol_parse_inventory()` 和 `r200_reader_read_epc()`。将正式批次改为 `r200_batch_t`：`R200_MAX_TAGS=32U`，每项只有 `chip_uid` 与 `rssi_dbm`。`r200_reader_read_batch(batch, timeout_ms)` 在调用方指定的测试窗口内反复发 Inventory、等待帧、解析 EPC/RSSI、按 EPC 去重；已存在 EPC 直接忽略，首次 RSSI 不覆盖。时间用 `uapi_systick_get_ms()` 实测，记录开始、每个新标签数量与耗时、结束数量与总耗时。没有 `tag_count==20`、平均数或假进度。
3. 使用第二片 0x21 的 `clearchain_tca9555_device_read_pin()` 轮询 P00-P04、P10-P13，复用 S1-S5 的消抖思想。新键只生成可取出的事件和启用状态；没有给任何实体键绑定 CP 入口或虚构 UI 页面栈。S1-S5 原阶段选择、模式查询与显示通知保留。
4. 增加 `/factory_scan` 的仅必填 `chip_uid` 调用和 `/verify_scan` 的已确认 JSON、结果解析。风险分数仅在实际响应中有 0-100 数值时标记有效；没有给普通业务伪造 `risk_score`。HTTP 地址仍引用原 `clearchain_config.h` 中的 ngrok 常量。
5. 在旧单标签主循环调用 `/scan` 处标明 batch 调用关系待后端确认。`/register_batch`、`/verify_batch`、跨设备 `batch_id` 与真实 `SCAN_PROGRESS` 留在接口边界。

## A. 文件状态

- 本轮新增：本记录文件。没有新增需要加入 CMake 的 `.c` 模块。
- 本轮修改：`r200_reader.h/.c`、`clearchain_key.h/.c`、`clearchain_http.h/.c`、`tcp_client_demo.c`、`clearchain_display_link.h`（仅真实进度 TODO）。
- 保留未改：`r200_protocol.*`、`r200_uart.*`、`clearchain_tca9555.*`、`clearchain_soft_i2c.*`、`clearchain_config.h`、RC522 源码、OLED 源码。主工程 CMake、Kconfig 和其他 SLE 源文件在本轮开始前已有未提交改动，本轮没有覆盖它们；CMake 已列入两板相应源文件，未列入 RC522、LCD 或旧 UART Bridge。

## B-C. R200 数据流与最终批次

`r200_uart_rx_isr()` 从 UART1 RX 把字节写入环形缓冲 → `r200_uart_wait_frame()` 组完整帧 → `r200_protocol_parse_inventory()` 从 `frame[5]` 取有符号 RSSI 并转换 EPC → `r200_reader_read_batch()` 调 `r200_reader_store_first()` 去重 → `r200_batch_t.tags[]`。例如 `0xCC` 对应 -52 dBm。同一个 EPC 后续 RSSI 不新增、不覆盖、不采样、不求 median。最大容量 32，约 20 仅为业务目标；无 A/B/C、3×3 槽位或固定物理位置。当前主循环仍走 `r200_reader_read_epc()` 的单标签测试路径，批量读取接口尚未绑定未确认的 HTTP 业务顺序。

## D-E. TCA9555 与按键

- 共享软件 I2C：GPIO13=SCL，GPIO14=SDA，未修改。
- 0x20：P00 红 LED、P01 绿 LED、P02 黄 LED、P03 蜂鸣器；P10-P14 分别为 S1-S5，继续更新原 `g_mode`，由 `clearchain_key_get_mode()` 查询。
- 0x21：P00 UP、P01 DOWN、P02 LEFT、P03 RIGHT、P04 OK；P10 D1 History、P11 D2 View Original、P12 D3 View Image、P13 D4 Back。P05-P07、P14-P17 保持预留输入。
- `clearchain_key_take_event()` 提供导航与 D1-D4 事件；`clearchain_key_availability()` 给出权限：D1 在 S1-S5/CP 可用；D2 仅 CP 可用；D3 仅 S4/S5/CP 可用；D4 可用并仅生成 Back 事件。导航键也只生成事件。History、图像、原始阵列和页面返回的 LCD 实际业务尚未实现。CP 模式枚举保留，实体入口未绑定。

## F. HTTP 状态

- `/scan`：原有单标签 S2-S5 能力保留；当前旧测试主循环仍会在 S1 使用，已标 TODO，未把它声明为最终 S1 业务。
- `/factory_scan`：`clearchain_send_factory_scan()` 已实现仅必填 `chip_uid` 的 POST；未自动接入 S1。
- `/verify_scan`：`clearchain_send_verify_scan()` 已实现已确认请求和结果解析，`AUTHORIZED/MONITOR/ALERT` 映射绿/黄/红；未绑定 CP 实体入口，风险数值只取真实响应。
- `/register_batch`、`/verify_batch`：请求方向由 `r200_batch_t` 支撑，最终 HTTP 调用链和响应解析冻结，等待后端契约与真实 `batch_id` 方案；没有自造字段或 API。

## G. Board A → Board B SLE

Board A 的 `tcp_client_demo_entry()` 初始化 `clearchain_display_link_init()`；原主循环和阶段键分别调用 `clearchain_display_show_waiting()`、`clearchain_display_show_result()`、`clearchain_display_stage_changed()`。Server 经 `clearchain_display_publish()` 入队，`clearchain_display_task()` 通过 `clearchain_display_send()` 的 `ssaps_notify_indicate()` 发通知。Board B 由 `clearchain_display_client_entry()` 启动 `clearchain_display_client_run()`，搜索 `clear_main`、发现服务与特征、在 `clearchain_display_notification()` 校验协议并入队，由 `clearchain_display_print()` 使用 `osal_printk` 输出。协议仍是 WAITING、STAGE_CHANGED、SCAN_PROGRESS、RESULT；正式路径未发伪造进度，固定 60%/82 只处于编译开关控制的链路测试代码。SLE 源码没有 UART1 初始化、反初始化、RX 回调或 GPIO15/16 pinmux 修改。

## 编译验证与未解决问题

- 当前工作区的普通命令沙箱仍在创建进程前报 `helper_unknown_error: setup refresh had errors`；通过已经获批的 `require_escalated` 命令通道完成检查、编辑验证及 WSL 构建。此为执行通道的临时恢复，不代表底层沙箱故障已经修复。
- 使用 SDK `python3 build.py -c ws63-liteos-app -j8`，在 WSL 用户目录的原源码副本中分别配置两板，从干净输出编译；这是因为 Windows 挂载目录上的 `shutil.rmtree()` 被 `.ninja_deps` 权限阻挡。Board B 生成宏 `CONFIG_CLEARCHAIN_DISPLAY_SLE_CLIENT=1`，完整 clean build 与打包成功。
- Board A 生成宏 `CONFIG_CLEARCHAIN_DISPLAY_SLE_SERVER=1`，所有本轮 C 源码完成编译，但最终链接失败：`r200_uart.c:221: undefined reference to uapi_uart_flush_rx_data`。该调用及缺少实现均为本轮前的基线问题；用户限制本轮只修新增错误和已明确的基线语法错误，因此没有擅自改 UART 驱动或删除该调用。Board A 构建尚未通过，硬件实测亦未完成。
- 仍待后端或硬件确认：`/factory_scan` 与 `/register_batch` 的调用关系，`/scan` 与 `/verify_batch` 的调用关系，`batch_id` 跨设备恢复，两个 batch API 的完整请求/成功/错误响应契约，正式业务 `risk_score` 来源，真实 `SCAN_PROGRESS` 来源与整批扫描窗口，CP 进入方式，LCD 型号/接口/驱动，以及 Board A 的上述 UART 链接阻塞。另发现现有构建配置包含 `CONFIG_LOG_UART=1` 和 `CONFIG_UART1_BAUDRATE=921600`，而 R200 在运行时将 UART1 配成 115200；日志 UART 与 R200 的实际资源关系需要在板上核对，本轮未擅改系统配置。

## 用到的主要命令

`rg --files`、`rg -n`、`Get-Content`、`git status --short`、`git diff --check -- <本轮文件>`、`python -c` 调用 `pypdf.PdfReader`/`docx.Document`、`wsl -e sh -lc`、`rsync -a --exclude=/output`、`python3 build.py -c ws63-liteos-app -j8`。构建前核对了清理目标位于工程 `src/output` 内。WSL 本地验证副本仅用于编译，没有把修改后的代码移出指定项目目录。
