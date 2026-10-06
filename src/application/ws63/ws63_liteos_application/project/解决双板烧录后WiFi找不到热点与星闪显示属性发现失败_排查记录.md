# 解决双板烧录后 Wi-Fi 找不到热点与星闪显示属性发现失败：排查记录

后续更新：本文件记录的是 17:58 前的源码与诊断 v1 状态。19:40 新日志确认属性本身存在，问题是 type=3 查询没有服务回调；当前源码 SSID 也已改为 `stu.gpnu.edu.cn`，旧板1包仍含 `@Ruijie-456`。请以同目录 `解决板2星闪属性发现条件失配与板1旧热点配置_双板联调进度.md` 为最新结论。

日期：2026-10-05。本文区分已确认事实、已修正代码和待板上验证的问题，不把诊断版当成已通过硬件验收的修复版。

## 1. 当前结论

用户已确认板1断开外设后烧录成功。最新日志接入外设后出现主 TCA9555 探测成功、R200 UART 初始化、Wi-Fi 扫描和 SLE 广播；板2出现 SLE connected。当前日志没有此前 osMain 的 Load access fault，排查重点已转到应用通信。

裸板日志中 TCA9555 0x20 探测失败符合未接模块的条件。`device_app_entry()` 遇到该失败会提前返回，所以仅凭裸板不崩溃不能证明原来的 sleep 路径已修好；接入元件后继续走到 Wi-Fi 任务才提供更强的验证证据。仍不能用这段运行日志保证内核补丁在所有场景正确。

现有三个独立阻塞点：

1. Wi-Fi 扫描能发现多个 AP，但未成功选中配置的目标，仍在连接认证之前。
2. SLE 物理链路连接及 MTU 交换成功，但应用服务/属性发现的联合条件不满足，显示通道尚未就绪。
3. 当前 `board_a_api` 配置关闭后端请求；此外，已归档固件的后端地址含额外引号。即使连接 Wi-Fi，原包也不能据此完成后端通信。

## 2. Wi-Fi 日志的来源及处理

阅读 `clearchain_config.h`：目标 SSID 为 `@Ruijie-456`，密码请在本地该文件核对，不在诊断文档复制。

阅读 `include/my_wifi_api.c` 的 `example_get_match_network()`：比较 SSID 长度，再逐字节比较内容。`wifi_connectTo_AP()` 在匹配函数失败时打印 `Can not find AP, try again !`，成功选中后才打印 `STA try connect.` 并调用连接接口。因此现阶段不要先归因为密码错误。

该通用错误也覆盖内存分配、获取扫描结果及复制失败；结合日志反复扫描出多个 AP，优先检查目标名称、频段和是否隐藏 SSID，不能仅凭这行日志排除所有其他错误。

现场操作：

- 开启 2.4 GHz、广播 SSID 的热点或路由器，核对名称的 `@`、连字符、大小写和空格。
- 若实际网络不是 `@Ruijie-456`，应改源码中的 SSID/密码再构建板1包，或把测试热点设置为固件现有配置。只改源码不烧录不会生效。
- 验收应看到 `STA try connect.`、成功获取地址，最终 `[DEVICE] Wi-Fi ready`。扫描 AP 数量增加不等于已连接。
- 不需要靠重复复位板2来解决板1的热点匹配问题。

## 3. 后端的两个独立问题

### 3.1 默认 API 包关闭网络请求

阅读 `profiles/board_a_api.config`：`CLEARCHAIN_UPLOAD_ENABLED=n`、`CLEARCHAIN_BACKEND_CONFIRMED=n`。

阅读 `clearchain_runtime_config.h`：两项同时开启才有 `CLEARCHAIN_UPLOAD_ALLOWED`。

阅读 `clearchain_device_http.c` 的 `exchange()`：这个开关在创建 socket 之前检查，覆盖 GET 状态及 POST 请求，不只是 RFID 上传。

阅读 `clearchain_device_app.c` 的 `state_poll_task()`：连续三个非 200 返回就报告后端离线。任务与 Wi-Fi 任务并行，而 Wi-Fi 启动还等待 5 秒。因此早期的 `backend offline after three failed polls` 不是服务器故障的证据。当前关闭开关的包即使后来联网，也仍然不会发送请求。

没有自动打开这两个开关。正式联调前需落实实际服务器地址、端口、目标接口和测试批次；`api_mock` 是面向自己控制的模拟后端的配置，不应把它当成随便替换正式包的方法。

本轮只读执行 `ipconfig`：电脑 WLAN 的 IPv4 为 `10.231.28.178`，另有两个无默认网关的适配器地址；没有看到 `192.168.4.1`。`netstat -ano` 筛选未看到本机 5000 端口监听。若后端在本电脑，则要重新核对地址及服务是否启动；若后端在树莓派或其他设备，这些结果不能证明远端服务是否正常。查询当前无线 SSID 的 `netsh wlan show interfaces` 返回权限不足，没有将未知 SSID 当成已确认，也未为此申请管理员权限。

### 3.2 已修正构建脚本把引号写入地址的问题

实测旧 `output/clearchain/board_a_api/mconfig.h` 是：

```c
#define CONFIG_CLEARCHAIN_DEVICE_HOST "\"192.168.4.1\""
```

这会让运行时地址含两个双引号。`clearchain_device_http.c` 直接把宏交给 `inet_addr()` / `gethostbyname()`，没有清理引号的步骤。

来源：profile 中 `CLEARCHAIN_DEVICE_HOST="192.168.4.1"` 原样进入 `subprocess.run()` 的参数数组；这里没有 shell 帮忙移除引号。`build/script/usr_config.py` 的 setconfig 分支又直接把等号后的值交给 `sym.set_value()`，导致引号变成字符串内容。

修改 `tools/build_dual.py`，增加 `profile_assignments()`：解析 profile，忽略空行/注释，对双引号包围的字符串使用 `json.loads()` 解码，然后传入 Kconfig。布尔、数字配置保持原值。

已用两个 API profile 和实际 Kconfig 生成流程验证，输出变成正确的 `#define CONFIG_CLEARCHAIN_DEVICE_HOST "192.168.4.1"`。这项修正不会更改板上已有固件，也不会自动确认该 IP 属于现场后端；板1需按最终网络配置重新构建才能获得修正。

## 4. 星闪已连接为什么仍不能显示

阅读 `clearchain_display_link.c`：服务 UUID `0x2222`，属性 UUID `0x2323`，属性包含 READ、WRITE、NOTIFY，服务启动成功后才广播。

阅读 `clearchain_display_client.c`：完成回调同时要求成功状态、当前连接、服务 UUID 匹配和带 NOTIFY 的属性 handle 非零，否则统一打印 `display property not found`。所以这句不能单独证明“服务端没注册属性”。服务回调未出现、UUID 长度/布局不匹配、权限不符、完成状态出错，都可能被合并成同一行。

交叉阅读 SDK `include/middleware/services/bts/sle/sle_ssap_client.h`、`sle_ssap_stru.h` 和 UART/hello/speed 客户端示例。示例存在 PROPERTY 和 PRIMARY_SERVICE 不同发现方式，不能仅凭枚举名称就武断断言 PROPERTY 请求一定不会产生服务回调。

已在板2 `clearchain_display_client.c` 加入有界诊断输出，不改变匹配条件、协议 UUID 或连接顺序：

- 启动标识：`[CLEAR SLE DIAG] client discovery-diag-20261005-v1`。
- 发现请求的 type、conn、返回 status。
- 服务回调状态、16 字节 UUID、UUID 长度、handle 范围、匹配结果。
- 属性回调状态、UUID、handle、操作权限、NOTIFY 位及匹配结果。
- 完成回调状态、service_match、property_handle。
- 就绪写命令的 handle 和返回状态。

下一次日志的判断方法：

- property 匹配且 handle 非零，但 service_match=0：继续区分服务回调缺失和服务 UUID 不匹配。
- 回调 UUID 存在但 match=0：按实际长度与表示核对协议，再修匹配逻辑。
- notify=0：核对服务端属性定义及实际运行包。
- status 非零：按 SDK 错误定义分析发现阶段失败。
- 全部通过后，应看到 `service discovery complete`，再验证 `[CLEAR DISPLAY]` 接收及 LCD 更新。

目前尚无这份诊断固件的板上日志，不宣布显示问题已经修复，也不删除服务检查来掩盖它。

## 5. 其他日志如何看

- 最新完整附件中 TCA9555 `0x20 probe ok`，但 `0x21` ACK 失败。`include/errcode.h` 的 `0x80001314` 是 I2C ACK 错误；`clearchain_key.c` 用 0x21 扩展额外按键。只接一块 0x20 扩展器时，不要把额外按键失败与主扩展器故障混为一谈。
- 当前主 I2C 接线按源码 GPIO13/14 核对，旧引脚说明有不同编号，不能混用。
- `0x80001341` 定义为 Flash 不支持，SDK 有读 ID 后回退初始化路径。系统后续能运行，不代表这个告警完全无害，也不能据此说 Flash 已损坏；当前不靠擦除全片或修改 efuse 处理它。
- `verify ... secure verify disable!` 是安全校验配置输出，不是烧录失败证明。
- 串口开头一小段乱码不改变后面完整可读启动日志的分析；单凭乱码不能断定串口参数始终错误。

## 6. 本轮构建环境与使用的命令

最初照旧说明调用 `C:/Python314/python.exe`，缺少 kconfiglib；安装到工作区 `output/clearchain/build_deps` 后还缺 windows curses。检查发现配套的 `D:/ws/tools/python/python.exe` 是 Python 3.11.4，已具备 kconfiglib、menuconfig 和 pycparser，后续采用它，不改系统 Python。

旧 CMake 缓存指向另一套 STM32CubeCLT 的 Ninja/CMake，出现拒绝访问。已对三个目标的 `CMakeCache.txt` 和 `cmake_command.txt` 留下 `before-sle-diag` 备份，将 Ninja 指向 SDK 的 `D:/ws/tools/Windows/ninja/ninja.exe`，触发重新生成，不递归删除用户目录。

默认 ccache 目录在用户 AppData；本轮编译在其阶段停滞，停止本次构建后将缓存指定到工作区再运行，随后编译开始推进。这里不据此宣称板端问题由 ccache 引起。

主要命令（在 `D:/ws/fbb/src` 执行）：

```powershell
git status --short
git diff -- application/ws63/ws63_liteos_application/project/clearchain_display_client.c
Get-Content -Encoding utf8 output/clearchain/board_a_api/mconfig.h
Get-Content -Encoding utf8 output/clearchain/board_b/build.log -Tail 30
D:/ws/tools/python/python.exe -c "import menuconfig, pycparser; print('SDK dependencies OK')"
D:/ws/tools/Windows/ninja/ninja.exe --version
$env:CCACHE_DIR='D:/ws/fbb/src/output/clearchain/ccache'
D:/ws/tools/python/python.exe application/ws63/ws63_liteos_application/project/tools/build_dual.py b
```

另用 `rg -n` 定位日志字符串、HTTP 开关、SSID 匹配函数和 SSAP 回调；用 Python 临时 Kconfig 文件实际生成头文件验证地址引号修正。读取的最新完整附件为 `C:/Users/Stu15/.codex/attachments/d48a06f2-04f5-4d80-a501-4780eb47551d/已粘贴的文本.txt`，并对照用户直接粘贴的双板日志。

## 7. 官方资料

- [润和 HH-D01 / NearLink 官方资料入口](https://gitee.com/hihopeorg_group/near-link/blob/master/README.md)：开发板资料、示例与工具目录。
- [HiSpark 星闪实验指导手册](https://gitee.com/HiSpark/fbb_ws63/blob/master/vendor/HiHope_NearLink_DK_WS63E_V03/doc/%E6%98%9F%E9%97%AA%E5%AE%9E%E9%AA%8C%E6%8C%87%E5%AF%BC%E6%89%8B%E5%86%8C.md)：配套 Windows Python 3.11.4、Kconfiglib 14.1.0 等环境说明。
- [海思 WS63 平台方案参数](https://developers.hisilicon.com/cn/caselibrary/86weifd03ktrmt1s7tnvqo3xwfl56e53)：标注 Wi-Fi 6 使用 2.4G 频段，支持排查热点频段；不是本项目应用日志的解释来源。

官方资料用于确认板卡和工具流程；本文对自定义 ClearChain 日志的判断以本仓库源码及实际日志为依据，不能由其他示例的“连接成功”替代本项目属性发现验收。

## 8. 本轮最终产物和验收步骤

17:58 已成功完成完整构建、签名与打包，输出 `packet success!` 和 `SUCCESS .../board_b`。本次没有发生先前所述 safe-delete 闸门阻止出包的情况，因此不能继续笼统说当前环境无法生成 fwpkg。

只用于板2（CLIENT）的诊断包：

```text
D:\ws\fbb\src\output\clearchain\_flash_ready\ws63-liteos-app_all_BOARDB_SLE_DIAG_20261005-175822.fwpkg
```

- 大小：1720680 字节。
- SHA256：`0b857affaef5ac7eda070cce6d4edf5dcb43b60eae1723e28739c7e4e13a9c88`。
- 同名 `.json` 保存用途、角色、源码哈希和 `hardware_verified: false`。
- 已逐项核对 `board_b/manifest.json` 的大小和 SHA256；配置为 CLIENT，不是 SERVER。
- 按 fwpkg 目录解析，应用成员从 0x28258 开始、1556224 字节，与归档签名 bin 完全一致；包内存在 `client discovery-diag-20261005-v1` 标识。
- `git diff --name-only -- build/config/target_config/ws63/menuconfig/acore/ws63_liteos_app.config` 无输出，构建的临时角色配置已恢复。
- 本轮没有重新构建或烧录板1；地址引号修正尚未进入板1原有包。没有进行任何板端烧录或声称硬件验收通过。

现场按此顺序操作：

1. 板1保留目前能启动的固件。板2断电后暂时断开外设，单独连接 USB，选中板2的实际 COM，勿默认它也是 COM14。
2. BurnTool 选择 WS63 和上面的完整板2包。关闭占用该串口的终端，Connect 后按板上实体 RST，等整个包显示成功。
3. 板2断电后恢复 LCD 接线。启动板1，看到 `server advertising` 后启动板2。用 115200 的串口终端记录板2从复位开始的输出。
4. 首先检查 `client discovery-diag-20261005-v1`；若没有，先核对烧录文件和 COM。随后提供所有 `[CLEAR SLE DIAG]` 行及周围错误，从扫描到 `discovery complete` / `display property not found`。
5. Wi-Fi 可并行核对 SSID/2.4 GHz。星闪显示的等待/离线状态不依赖后端成功联网，无需等后端恢复才做本轮发现诊断。

下一步仍待确认：实际 Wi-Fi 名称、后端部署在哪台设备及其 IP/端口，以及诊断版板2回调日志。源码和构建已完成的修正，与这些尚待现场验证的事项分开记录。
