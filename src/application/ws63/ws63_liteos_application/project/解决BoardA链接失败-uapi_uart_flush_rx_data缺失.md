# 解决 Board A 链接失败：uapi_uart_flush_rx_data 缺失

## 关键卡住的问题

Board A clean build 在应用 ELF 链接阶段失败，错误点是 `project/r200_uart.c` 调用了 `uapi_uart_flush_rx_data(R200_UART_BUS)`，但当前 WS63 SDK 只在 `src/include/driver/uart.h` 中声明了这个函数，没有在参与链接的 UART 驱动源码或静态库中提供实现。

因此 C 源码可以编译完成，但最终链接找不到符号，Board A 不能生成可烧录固件。

## 是什么引起的

`r200_uart_prepare_receive()` 原本想在每次 R200 Inventory 前清掉 UART1 RX 残留数据，再清空本地环形缓冲。这个思路本身合理，但使用了 SDK 里缺实现的 `uapi_uart_flush_rx_data()`。

当前 SDK 已实现的可替代路径是 `uapi_uart_unregister_rx_callback()`。阅读 SDK `src/drivers/drivers/driver/uart/uart.c` 后确认，该函数会：

- 清空当前 RX callback；
- 关闭 RX、帧错误、奇偶校验错误、IDLE 中断；
- 在锁内读取 RX FIFO，直到 FIFO 空或达到 `CONFIG_UART_FIFO_DEPTH`。

这与 R200 读前“清旧 RX 数据”的目的一致，并且不用手写底层 UART 寄存器。

## 解决流程

1. 定位实际工程文件：用户要求的正式路径是 `D:\ws\fbb\src\application\ws63\ws63_liteos_application\project`，本次只修改该路径下的 `r200_uart.c`。
2. 搜索 SDK UART API：确认 `uapi_uart_flush_rx_data()` 只有声明，没有实现；确认 `uapi_uart_register_rx_callback()` 和 `uapi_uart_unregister_rx_callback()` 有实现。
3. 阅读 `uapi_uart_unregister_rx_callback()` 的实现：确认它会关闭 RX 相关中断并清 RX FIFO。
4. 修改 `r200_uart_prepare_receive()`：用注销 RX callback 清 FIFO，再清 R200 自己的 ring buffer 和 event，最后重新注册 R200 RX callback。
5. 抽出 `r200_uart_register_rx_callback()`：初始化和重新准备接收时复用同一套 callback 注册参数，避免两处参数不一致。
6. 同步源码到 WSL clean build 验证副本，执行 Board A Server clean build。

## 怎么解决的

修改文件：

- `D:\ws\fbb\src\application\ws63\ws63_liteos_application\project\r200_uart.c`

核心改动：

```c
static int r200_uart_register_rx_callback(void)
{
    return (uapi_uart_register_rx_callback(R200_UART_BUS,
        UART_RX_CONDITION_FULL_OR_IDLE, R200_UART_RX_BLOCK_SIZE,
        r200_uart_rx_isr) == ERRCODE_SUCC) ? 0 : -1;
}
```

`r200_uart_init()` 不再直接重复写注册参数，而是调用：

```c
if (r200_uart_register_rx_callback() != 0) {
    osal_printk("R200 UART RX interrupt register failed\r\n");
    return -1;
}
```

`r200_uart_prepare_receive()` 从：

```c
(void)uapi_uart_flush_rx_data(R200_UART_BUS);
```

改为：

```c
uapi_uart_unregister_rx_callback(R200_UART_BUS);
/* clear local R200 RX ring and event */
if (r200_uart_register_rx_callback() != 0) {
    osal_printk("R200 UART RX interrupt re-register failed\r\n");
}
```

## 使用到的方法

- 用 `rg --files` 定位实际 `r200_uart.c`、`r200_uart.h`、`r200_reader.c`。
- 用 `rg -n` 搜索 `uapi_uart_flush_rx_data`、`uapi_uart_register_rx_callback`、`uapi_uart_unregister_rx_callback`。
- 用 `Get-Content -Raw` 阅读 R200 UART 源码和 SDK UART 驱动实现。
- 用 `apply_patch` 做最小代码修改。
- 用 WSL 验证副本执行干净构建，避免 Windows 挂载目录清理 `output/.ninja_deps` 时的权限问题。

## 阅读到的文件和用到的位置

- `project/r200_uart.c`
  - `r200_uart_init()`：确认 UART1、GPIO15/GPIO16、R200 RX callback 注册方式。
  - `r200_uart_prepare_receive()`：原链接失败调用点，本次替换为 unregister/clear/register。
  - `r200_uart_rx_isr()`：确认 callback 只把字节写入 R200 环形缓冲，适合注销后重新注册。

- `project/r200_uart.h`
  - 确认 `r200_uart_prepare_receive()` 对外仍是 `void`，本次不扩大接口，不连带重构 Reader 层。

- `project/r200_reader.c`
  - 确认单标签读取和 batch 读取都会在发送 Inventory 前调用 `r200_uart_prepare_receive()`，所以本次修复同时覆盖两条 R200 读取路径。

- `src/include/driver/uart.h`
  - 确认 `uapi_uart_flush_rx_data(uart_bus_t bus)` 只有声明。
  - 确认 `uapi_uart_register_rx_callback()` 与 `uapi_uart_unregister_rx_callback()` 的公开接口签名。

- `src/drivers/drivers/driver/uart/uart.c`
  - `uapi_uart_register_rx_callback()`：确认 RX callback 注册参数和启用 RX/错误/IDLE 中断的行为。
  - `uapi_uart_unregister_rx_callback()`：确认会关闭 RX 相关中断并读空 RX FIFO。

## 使用到的指令

定位文件：

```powershell
rg --files D:\ws\fbb | rg "r200_uart\.(c|h)$|r200_reader\.(c|h)$|tcp_client_demo\.c$"
```

搜索 UART API：

```powershell
rg -n "uapi_uart_(flush_rx_data|register_rx_callback|unregister_rx_callback)|UART_RX_CONDITION" D:\ws\fbb\src\include D:\ws\fbb\src\drivers
```

读取源码：

```powershell
Get-Content -Raw D:\ws\fbb\src\application\ws63\ws63_liteos_application\project\r200_uart.c
Get-Content -Raw D:\ws\fbb\src\application\ws63\ws63_liteos_application\project\r200_uart.h
Get-Content -Raw D:\ws\fbb\src\drivers\drivers\driver\uart\uart.c
```

同步到 WSL 验证副本：

```powershell
wsl -e sh -lc 'set -eu; MIRROR=/home/tu15/clearchain-verify-src; SRC=/mnt/d/ws/fbb/src; rm -rf "$MIRROR/output"; rsync -a --delete --exclude=/output/ "$SRC/" "$MIRROR/"'
```

Board A Server clean build：

```powershell
wsl -e sh -lc 'set -eu; cd /home/tu15/clearchain-verify-src; python3 build/script/usr_config.py --command setconfig --chip ws63 --core acore --target ws63-liteos-app CLEARCHAIN_DISPLAY_SLE_SERVER=y CLEARCHAIN_DISPLAY_SLE_CLIENT=n CLEARCHAIN_DISPLAY_SLE_LINK_TEST=n SAMPLE_ENABLE=n; python3 build.py -c ws63-liteos-app -j8'
```

## 验证结果

Board A Server clean build 已通过。构建日志中应用阶段成功完成：

```text
[100%] Linking CXX executable ws63-liteos-app.elf
[100%] Built target ws63-liteos-app
######### Build target:ws63_liteos_app success
packet success!
```

这说明原来的 `undefined reference to uapi_uart_flush_rx_data` 链接失败已经解决。

Board B Client 重新 clean build 的命令已准备执行，但当前 Codex 命令执行环境出现两个限制：

- 普通命令通道仍报 `helper_unknown_error: setup refresh had errors`；
- 再次请求 WSL 构建审批时触发自动审批用量限制，命令未执行。

因此本记录只报告本次已实测的 Board A clean build 通过；Board B 本轮修改后尚未重新跑完 clean build。根据代码归属判断，本次只改 `r200_uart.c`，Board B Client 角色此前不编译 R200 UART 文件，理论上不受影响，但仍需在命令环境恢复后重新跑 Board B clean build 才能写成正式通过结论。

## 修改影响

- 解决 Board A 因 SDK 缺失 `uapi_uart_flush_rx_data` 实现导致的链接失败。
- 不改变 UART1 所属关系，UART1/GPIO15/GPIO16 仍只归 R200 使用。
- 不改 RSSI 解析、batch 数据模型、HTTP API、SLE 协议、TCA9555、按键业务规则。
- 每次 R200 读前准备会多一次 RX callback 注销和重注册，换来 SDK 已实现路径下的 RX FIFO 清理。
- 如果 callback 重新注册失败，会打印 `R200 UART RX interrupt re-register failed`，避免静默失败。

## 后续仍需确认

- 在真实 HH-D01 + R200 硬件上验证：连续 Inventory 前后不会混入旧帧。
- 命令环境恢复后重新执行 Board B Client clean build。
- 烧录 Board A 后实测 R200 UART1、SLE、Wi-Fi 并发运行日志。
