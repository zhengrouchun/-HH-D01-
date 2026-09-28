# 解决 SLE 显示链路 UART1 冲突与角色混编的实施记录

## 问题、原因与本轮结果

1. 官方 `sle_uart` 是 UART↔SLE↔UART 桥接，默认占用 UART1 与 GPIO15/16；ClearChain 的 R200 已占用这些资源。直接编入示例会重复初始化 UART1 和注册 RX 回调。解决方式是在 `project` 中独立实现 SLE Server、Client 与显示协议，示例目录只读参考，未作修改。
2. 最初采用可选 Kconfig choice 时，`setconfig` 接受角色赋值却不产生角色宏；改成默认关闭、Server、Client 三选一。CMake 原先也没有因 `.config` 变化而重新选择源文件，并且 SDK 归档器会保留旧 `.a` 的成员。组件 CMake 现在追踪 Kconfig 文件变化，归档前删除旧静态库。实测 A→B 后库中只有 Client 对象，B→A 后库中只有 ClearChain 与 Server 对象。
3. Windows 初次构建卡在工具环境：`ccache` 未在 PATH，随后交叉编译器 `cc1.exe` 因 `libssp-0.dll` 所在的工具链 `bin` 未在 PATH 而退出。仅在构建进程的 PATH 中加入两处目录即可编译。旧 WSL CMake 缓存曾造成路径不匹配，备份旧 `output` 生成目录后从干净目录重建。
4. Board B 完整构建、链接和打包成功。Board A 的应用组件编译成功，但最终链接仍被既有 `r200_uart.c:221` 的 `uapi_uart_flush_rx_data` 未定义引用阻断。SDK `include/driver/uart.h:652` 仅声明该函数，当前源码搜索没有找到实现。本轮按要求未改 R200；Board A 暂无本轮可烧录固件，硬件联调尚未执行。

## 实施过程与使用的方法

- 通读 ClearChain 入口、R200 UART、按键、软件 I2C、HTTP、CMake/Kconfig；只读对照 `sle_uart_server.c`、`sle_uart_server_adv.c`、`sle_uart_client.c`、`sle_uart.c` 的注册、广播、扫描、配对、MTU、发现、通知和桥接入口。
- 用 `rg` 定位 UART1、GPIO13~16、`app_run`、SLE API、构建角色及符号定义；用 Kconfig 工具检查角色取值；用宿主 GCC 对 A、B、原始配置分别做语法检查。
- 分别生成 A/B Kconfig 配置，使用 RISC-V GCC 编译组件、Ninja 链接 ELF、SDK `build.py` 执行完整打包；使用 `riscv32-linux-musl-ar t` 核对 A/B 静态库成员，使用 `Get-FileHash` 核对复制后的 Board B 固件。
- 构建期间只移动并保留 `output` 里的旧生成目录，完成后恢复了原始 `ws63_liteos_app.config`。没有修改 `application/samples` 下任何文件，也没有改 R200 batch 算法、HTTP API、TCA9555、引脚映射、OLED、LCD 或 LVGL。

## 读取的关键文件及使用位置

- `project/tcp_client_demo.c`：`tcp_client_demo_entry` 是 Board A 唯一启动入口；扫描完成的 HTTP 灯色结果映射为显示结果；标签移除时发送 WAITING。原中文句号已改为合法 C 语法。
- `project/clearchain_key.c`：阶段键确认 `g_stage` 后发送 STAGE_CHANGED；原按键业务继续运行。
- `project/r200_uart.c`：UART1 初始化、GPIO15/16 pinmux、唯一 RX callback；`r200_uart_prepare_receive` 中的未定义引用是 Board A 最终链接阻塞点。
- `project/clearchain_soft_i2c.c`：GPIO13/14 保持软件 I2C。
- `project/clearchain_http.c`：核对 GREEN/ORANGE/RED 来源；未添加或推算 `risk_score`。
- `application/samples/bt/sle/sle_uart/sle_uart_server/src/sle_uart_server.c` 和 `sle_uart_server_adv.c`：参考服务注册、Notification、断线重播、广播字段编码；未复制 UART 桥接任务。
- `application/samples/bt/sle/sle_uart/sle_uart_client/src/sle_uart_client.c`：参考扫描、连接、配对、MTU、服务发现及 Notification 回调；新 Client 使用有界 AD 字段解析，不用 `strstr` 将广播数据当字符串。
- `application/samples/bt/sle/sle_uart/sle_uart.c`：确认示例 UART 转发与本工程冲突，只读，未编入 A/B。
- SDK 的 `sle_connection_manager.h`、`sle_device_discovery.h`、`sle_ssap_server.h`、`sle_ssap_client.h` 与 `include/driver/uart.h`：核对回调签名、SSAP 结构、通知调用及 UART API 声明。
- `application/Kconfig`、`application/ws63/ws63_liteos_application/CMakeLists.txt`、`build/cmake/build_function.cmake`、`build/cmake/build_component.cmake`：核对 Kconfig 到 CMake 的传播、源文件选择及归档时机。

## 本轮文件

新增在 `project`：`clearchain_display_protocol.h`（V1 字节格式与枚举）、`clearchain_display_link.h/.c`（Board A 接口、队列、SLE Server）、`clearchain_display_client.h/.c`（Board B 扫描与协议解析）、`clearchain_display_client_entry.c`（Board B 唯一入口）、`board_b_display_client_all.fwpkg`（已构建的 Board B 包）、本实施记录。

修改：`project/tcp_client_demo.c`（初始化、WAITING、RESULT 和语法修复）、`project/clearchain_key.c`（阶段事件）、`application/Kconfig`（互斥角色、测试选项）、`application/ws63/ws63_liteos_application/CMakeLists.txt`（按角色选择源文件、配置变更时重配、归档前清除旧成员）。`clearchain_display_link.h` 在 SLE 关闭时提供空实现，原业务配置仍可编译。

## 数据与启动调用链

协议：`version(1) | command | payload_length | payload`。V1 仅有 WAITING、STAGE_CHANGED、SCAN_PROGRESS、RESULT；只手工编码/解码字节。未知风险分数为 `0xFF`，业务不把未知当 0；`60%` 和 `82` 只在 `CONFIG_CLEARCHAIN_DISPLAY_SLE_LINK_TEST=y` 时测试发送。

- A：`app_run(tcp_client_demo_entry)` → `clearchain_display_link_init` → 独立低优先级任务 → `enable_sle` → SSAP 服务/属性注册 → `clear_main` 广播。主 RFID/Wi-Fi/HTTP 任务不等待显示板。
- B：`app_run(clearchain_display_client_entry)` → Client 任务 → `enable_sle` → 扫描有界匹配 `clear_main` → 连接/配对/MTU → 服务和属性发现 → Notification 回调校验协议 → 接收队列 → `osal_printk`。断开后重新扫描。
- A 发送：业务调用 `clearchain_display_show_*` → 非阻塞写显示队列；显示任务调用 `ssaps_notify_indicate`；B 收包回调校验并入队，打印任务输出。A 未连接时只保留最新显示状态，队列满时返回错误且保留待重放状态。

## 静态与构建结果

- UART1 的 `uapi_uart_deinit/init/register_rx_callback` 以及 GPIO15/16 pinmux 只出现在 `project/r200_uart.c`。`clearchain_soft_i2c.c` 仍用 GPIO13/14。Board B 静态库只有 `main.c`、`reset_vector.S`、`clock_init.c`、`clearchain_display_client.c`、`clearchain_display_client_entry.c`，没有 UART Bridge、R200 或 ClearChain 业务对象。
- Board A Server：组件编译成功；最终 ELF/打包失败。首个真正的链接错误：`project/r200_uart.c:221: undefined reference to uapi_uart_flush_rx_data`。这不是本轮 SLE 代码里的符号；按本轮范围未改 R200。
- Board B Client：组件编译成功，ELF 链接成功，`ws63-liteos-app_all.fwpkg` 打包成功。固件副本：`project/board_b_display_client_all.fwpkg`，SHA-256 `FE537E5D7A3960C0A7CBDC578317E2A83B0101139CEF95DFD43D0C00B708F693`。
- Wi-Fi 与 SLE 共存、R200 实际读取、TCA9555、HTTP、无 B 时持续扫描、断线重连、真实日志都需要两块板烧录后验证；源码检查和构建不等于硬件验证。

## 复现构建时使用的命令

在 `D:\ws\fbb\src` 的 PowerShell 中执行：

```powershell
$env:PATH='D:\ws\fbb\src\tools\bin\compiler\riscv\cc_riscv32_musl_105\cc_riscv32_musl_win\bin;D:\ws\tools\cfbb\thirdparty\ccache;' + $env:PATH
python build\script\usr_config.py --command setconfig --chip ws63 --core acore --target ws63-liteos-app CLEARCHAIN_DISPLAY_SLE_CLIENT=y CLEARCHAIN_DISPLAY_CLIENT_MTU_SIZE=520 SAMPLE_ENABLE=n
python build.py -ninja -j4 ws63-liteos-app
```

Board A 使用同一 PATH，配置与命令为：

```powershell
python build\script\usr_config.py --command setconfig --chip ws63 --core acore --target ws63-liteos-app CLEARCHAIN_DISPLAY_SLE_SERVER=y CLEARCHAIN_DISPLAY_SLE_LINK_TEST=y SAMPLE_ENABLE=n
python build.py -ninja -j4 ws63-liteos-app
```

正式业务版本将 `CLEARCHAIN_DISPLAY_SLE_LINK_TEST=n`。两种角色共用同一个目标名；每次切换须重新构建并以构建结果为准，不能把 Board B 包烧到 A。配置文件已恢复到本轮开始前的状态，当前不是 Server 或 Client 测试配置。

## 烧录和联调步骤

1. Board B 可使用本目录的 `board_b_display_client_all.fwpkg`，通过现有 HH-D01 烧录工具写入 B。Board A 必须先解决上述 R200 链接缺失，再按 Server 配置构建新的 `output/ws63/fwpkg/ws63-liteos-app/ws63-liteos-app_all.fwpkg`；不要使用旧包冒充本轮 A 固件。
2. 两块板分别接其原有 USB UART 调试口，打开各自串口日志；保留 A 的 UART1 GPIO15/16 给 R200。先启动 A，预期 `[CLEAR SLE] server advertising`；再启动 B，预期 `[CLEAR SLE] scanning clear_main`、`[CLEAR SLE] connected`；A 预期 `[CLEAR SLE] display connected`。
3. 测试配置在每次连接完成后发送 `SCAN_PROGRESS(1,60)` 和 `RESULT(REJECT,82)`；B 预期打印 `[CLEAR DISPLAY] cmd=SCAN_PROGRESS stage=1 percent=60` 和 `[CLEAR DISPLAY] cmd=RESULT result=REJECT risk=82`。业务 HTTP 结果则应打印 `risk=UNKNOWN`。
4. B 断电时在 A 上继续刷 RFID，核对 R200 EPC 读取、Wi-Fi 连接、HTTP 请求、TCA9555 按键及 LED/蜂鸣器仍正常；B 重新上电后核对重新扫描、连接和显示日志。完成后关闭受控测试选项，避免把 60/82 测试值当业务数据。

## 需要队友确认的具体问题

“当前 WS63 SDK 的 `include/driver/uart.h` 声明了 `uapi_uart_flush_rx_data(uart_bus_t)`，但完整 Board A 链接在 `r200_uart.c:221` 找不到实现。请确认这个 API 在 HH-D01 使用的 SDK 版本中由哪个源文件/静态库提供，是否需要启用某个 UART 组件宏，或应改用哪个有相同清空 RX 缓冲语义的 API。请提供 SDK 版本、对应源码/库路径及一个在 UART1 RX callback 模式下可用的调用示例。”

另请队友提供两块板的实际固件烧录方式、调试串口号/波特率，以及 Wi-Fi 与 SLE 并发的实机测试日志；这些属于源码构建无法代替的验证。
