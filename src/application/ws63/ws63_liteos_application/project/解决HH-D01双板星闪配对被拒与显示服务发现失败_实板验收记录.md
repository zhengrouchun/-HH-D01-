# 解决 HH-D01 双板星闪配对被拒与显示服务发现失败：实板验收记录

日期：2026-10-05。依据用户最新板2串口全文，以及板1附件 `C:/Users/Stu15/.codex/attachments/14ab7cc7-1586-487a-9344-5adcd40b0bac/已粘贴的文本.txt`（完整读取）。

## 当前已经解决到哪里

这次实板已通过：指定对端清理完成 → 建立连接 → 认证成功 → 配对成功 → MTU 交换成功 → 主服务发现 → 属性发现 → ready 写入请求成功 → 接收并解码两条板1状态消息。

这证明原先的配对拒绝阻塞已经在本次启动解除，先查询服务再查询属性的修改也通过实板验证。尚未证明长时间连接稳定、所有复位/重连情形都可恢复、LCD 物理画面完全正常、RFID 实际读卡、后端 HTTP 全流程成功。

使用中的固件：板1 `BOARDA_PEER_PAIR_RECOVERY_20261005-210011`；板2 `BOARDB_PEER_CLEANUP_20261005-213507`。板2启动标记 `client peer-cleanup-two-stage-20261005-v3` 与归档包一致。本轮只记录验收结果，没有改应用代码或重新生成固件。

## 两个问题及解决流程

### 一、连接成功后反复配对被拒

旧日志：板2连接后反复报 `0x8000600f`，对应 SDK `ERRCODE_SLE_PAIRING_REJECT`。单独清理板1后，删除成功但仍失败，因此不能把那一步记录为充分修复。

核对本地 SDK `sle_uart_client.c`、`sle_hello_client.c`：示例在连接前清理指定目标设备的本地配对记录；`sle_speed_client.c` 在认证/配对失败时也有指定对端清理。`sle_connection_manager.h` 说明删除结果通过 `pair_remove_cb` 异步回报。

修改板2 `clearchain_display_client.c`：首次找到目标后仅清理该目标、等待匹配地址的完成回调再连接；配对状态 NONE 才发起请求，PAIRED 才直接进入 MTU 交换，PAIRING 等待；增加配对/认证结果日志；断开后在任务内延后 2 秒重扫。

新日志证据：

```text
client peer removal complete status=0x0
connect request status=0x0
client link conn=0 state=1 pair_state=1 reason=0x0
pairing request conn=0 status=0x0
authentication complete conn=0 status=0x0
pairing complete conn=0 status=0x0 connected=1
ssapc exchange info, conn_id:0, err_code:0
```

本次成功发生在加入客户端定向清理后，支持“客户端配对恢复流程缺失参与了故障”的判断；但记录清理、复位及状态判断修正同时存在，日志没有直接展示旧密钥，因此不能宣称已经唯一证明某条旧配对记录损坏。

`local_pair=0 query=0x8000600a` 不代表已经查询到“配对状态为 0”。`0x8000600a` 是 `ERRCODE_SLE_STATUS_ERR`，当时处于尚未建立连接的查询阶段，输出变量 0 只是初始化值。后面连接、认证、配对都成功，说明这一查询诊断没有阻断本次流程。不能据此再判成“配对失败”。

### 二、属性已找到却报告 display property not found

先前诊断包发现属性 `0x2323`、句柄 17，但 `service_match=0`。原程序只发 PROPERTY 查询，却要求服务回调也确认服务 UUID；这一流程依赖没有满足。

将客户端改为两阶段：先查 PRIMARY_SERVICE（type=1），确认 `0x2222` 并保存句柄范围；再在该范围查 PROPERTY（type=3），确认 `0x2323` 和通知能力。

新日志证据：

```text
discovery request conn=0 type=1 status=0x0
service uuid_len=2 uuid=37bea880fc7011eab720000000002222
service range=16..17 match=1
property request conn=0 range=16..17 status=0x0
property uuid_len=2 uuid=37bea880fc7011eab720000000002323
property handle=17 operations=0xd match=1 notify=1
discovery complete conn=0 status=0x0 type=3 service_match=1 property_handle=17
service discovery complete
ready write handle=17 status=0x0
[CLEAR DISPLAY] seq=20 cmd=8 stage=1 phase=1 percent=0 tags=0 samples=0 error=0
[CLEAR DISPLAY] seq=21 cmd=8 stage=1 phase=1 percent=0 tags=0 samples=0 error=0
```

ready 写入的返回值证明请求被接受；后续状态消息进一步证明通知与解码路径已走通，不仅仅是 Connected。

## 用到的文件及代码位置

- `clearchain_display_client.c`：约 142–196 行连接前定向清理及异步完成；208–280 行连接/配对/认证；290–386 行两阶段发现和 ready 写入；392–414 行通知解码、序列检查及心跳日志过滤；422–467 行注册回调和延后重试。
- `clearchain_display_link.c`：208–269 行服务端清理/配对；约 312–340 行服务及属性注册；425 行起发送状态或心跳。通过比较两端 UUID、句柄及回调流程排除名称、属性失配。
- `include/middleware/services/bts/sle/sle_errcode.h:88/99`：核对状态错误与配对拒绝的不同含义。
- `include/middleware/services/bts/sle/sle_connection_manager.h:34/491/597/724/743`：配对状态、完成回调、删除回调、配对请求与删除请求。
- `application/samples/bt/sle/sle_uart/sle_uart_client/src/sle_uart_client.c:144/195/219`：SDK 客户端清理目标、发起配对、成功后交换 MTU；另外交叉读取 `sle_hello_client.c` 与 `sle_speed_client.c`。
- `clearchain_display_protocol.h:30/43`：`cmd=8` 是完整状态快照，`phase=1` 是等待态；`clearchain_device_state.h`：`stage=1` 对应 S1/Factory。零标签、零样本、进度 0 是该状态的内容，不是传输错误。
- `clearchain_ui.c:118–143`：分别呈现 `SCR_OFFLINE  SLE link unavailable` 与 `SCR_OFFLINE  Server offline`。前者是星闪连接或消息新鲜度问题，后者是后端离线标志；画面离线不能直接判为配对又坏了。
- `clearchain_display_client.c:410`：仅非 HEARTBEAT 消息打印 `[CLEAR DISPLAY]`。服务器每秒发送心跳，因此只有两条非心跳状态日志不证明数据停止；这也不代替对心跳持续到达的实板验收。
- `clearchain_device_http.c:40`、`clearchain_runtime_config.h:11`、`profiles/board_a_api.config`：HTTP 开关判定及当前关闭配置。

## 使用到的方法与指令

方法：将两端日志对齐到同一连接阶段；以 SDK 枚举解释错误码；用 SDK 示例及 API 文档核对异步时序；逐阶段加日志；通过构建产物标记、包内应用和 SHA256 排除旧包；以实板回调和状态包验收。

本轮阅读指令：

```powershell
Get-Content -Encoding UTF8 -LiteralPath 'C:/Users/Stu15/.codex/attachments/14ab7cc7-1586-487a-9344-5adcd40b0bac/已粘贴的文本.txt'
rg -n 'pair_state|pair_remove|auth_complete|property request|service discovery complete' application/ws63/ws63_liteos_application/project/clearchain_display_client.c
rg -n 'CMD_FULL_STATE|DISPLAY_WAITING|CMD_HEARTBEAT' application/ws63/ws63_liteos_application/project/clearchain_display_protocol.h
```

前轮生成本次已验证包的命令（现在无需重编）：

```powershell
$env:CCACHE_DIR='D:/ws/fbb/src/output/clearchain/ccache'
D:/ws/tools/python/python.exe application/ws63/ws63_liteos_application/project/tools/build_dual.py b
Get-FileHash -Algorithm SHA256 output/clearchain/_flash_ready/ws63-liteos-app_all_BOARDB_PEER_CLEANUP_20261005-213507.fwpkg
```

板2 SHA256：`e58c3b5279aec415cc611e1d360cd9af71844aab19c2daf3d8a2abe130b298eb`。前轮已通过 manifest 全文件哈希、CLIENT 配置、新标记、7 个包成员索引与包内应用逐字节一致性核对。

## 仍要确定的断连和屏幕状态

板1附件在 22:24:52.015 建立连接，在 22:25:12.089 断连（`pair_state=3 reason=0x7`），在 22:25:16.600 又连接。后续到 22:26:06.467 未显示另一次断连。板2粘贴文本有一次 `boot.`，但没有时间戳，不能确定它是否对应板1这次断连。

因此已询问用户：该时刻是否复位/断电过板2，屏幕现在显示什么。如果确实复位，这是人为操作导致的断连候选；如果完全没有操作，则需要单独排查运行时断连。当前 SDK 公开断链枚举未说明 `0x7`，不擅自把它认定为超时、干扰或芯片故障。

先保持这对固件，不再重复烧录，双方同时开串口并连续观察 2–5 分钟。确认屏幕有画面、状态符合当前业务、连接不反复掉线；再只复位板2一次，检查完整配对/发现/状态接收流程能否再次成功。该复位是一次明确记录的恢复性测试，不要与无操作的稳定性观察混在一起。

## 后端及硬件下一步

后端：板1复位后再次 DHCP 成功并显示 Wi-Fi ready，但 API 包的上传和后端确认开关都关闭，GET/POST 在 socket 前被拦截。`backend offline after three failed polls` 可由该配置触发；若屏幕显示 `Server offline`，首先核对后端配置而不是重刷星闪程序。需要确定实际后端 IP、端口和板1到服务端的网络可达性；当前 `192.168.4.1:5000` 默认地址是否有效仍未知。阶段按键在 API 模式下发送 HTTP 选择命令，后端未打通时不能用“按键不切换界面”直接判硬件故障。

硬件：主 TCA9555 `0x20 probe ok`；额外 `0x21` 不应答，若没有第二颗扩展器可预期，若已接第二颗则查地址选择、供电、共地、GPIO13/14；R200 仅完成 UART 初始化，尚未证明读到标签；LCD 初始化加消息接收已过，实际画面由用户确认。

启动 `0x80001341` 仍在，未在本轮修复；后续应用已执行，不把它等同本轮星闪通信失败。原来的初始化调度 Load access fault 在此次日志也未出现。
