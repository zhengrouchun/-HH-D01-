# 解决离线包 Wi-Fi 测试时机疑问与真实读卡证据判读：双板日志验收

日期：2026-10-06。范围：用户提供的两份新烧录串口记录；没有本轮 LCD 照片、烧录进度截图或 10 分钟完整日志。板1为接 R200/阶段键的设备板，板2为 LCD 板。

## 问题与原因

用户此前确认 Wi-Fi 与 SLE 曾正常连接，疑惑为何这次离线包不测试 Wi-Fi、何时填写广东现场 Wi-Fi，同时希望判读新的读卡日志。`clearchain_device_app.c` 中 `CONFIG_CLEARCHAIN_DEVICE_BENCH` 分支不启动 `wifi_task`；该包在板1本地模拟阶段控制，只采集真实 RFID 并经 SLE 传给 LCD。因此当前日志没有 `Wi-Fi ready` 是包的预期行为，不能据此判断 Wi-Fi 失效。Wi-Fi SSID/密码应在队友 API v2 地址可从板1网络访问、运行 `configure_backend_and_build_board1.cmd` 时填写。板1在广东，填广东现场板1使用的网络；板2不使用 Wi-Fi。跨广东与天津时，队友笔记本的普通局域网 IP 不能直接作为可达目标，需提供板1网络可路由到的 HTTP IPv4 地址及端口。

## 本次串口已证明的事实

- 板1启动了 `OFFLINE BENCH`，TCA9555 `0x20`、R200 UART 115200 初始化成功，SLE server advertising。板2显示 `state-v4`、LCD 480×320 初始化与反相 OFF，SLE 连接、认证配对、服务 `0x2222`/属性 `0x2323` 发现和 `ready write status=0x0` 成功。
- 板1 S3 一轮读到 EPC `000000000000000000078909`、RSSI `-61 dBm`，最终 `unique=1 samples=1`；板2同阶段收到 `tags=1`。板1 S4 一轮读到 EPC `000000000000000000078911`、RSSI `-59 dBm`，最终 `unique=1 samples=1`；板2同阶段收到 `tags=1`。这些是实际标签读数和板间数量传输证据，不是后端判定。
- S1 先后被按两次，前一窗口被新按键切换，后一窗口最终 `unique=0 samples=0`；S3 另一次窗口也为 0。零读数只说明这些窗口未收到有效标签帧，需按固定位置、方向及窗口重新测，不据此判 R200 故障。
- 板2日志中的 `samples=0` 来自 `clearchain_display_publish_device_state()` 对完整状态快照固定写零；当前应核对板1 `unique` 与板2 `tags`。重复标签增加 `samples` 的规律还未在这份日志中得到实测。
- 板1在 16:45:45 记录一次 SLE 断开、16:45:49 重新连接。是否有人复位板2未知，不指定断连原因。此后提供的窗口含数据传输，但还不足以宣称十分钟稳定。
- 两板均有 `Flash Init Fail! ret=0x80001341` 后继续启动应用；SDK 将其定义为 Flash 型号不支持提示/回退路径。它是独立待观察告警，当前不作为 RFID/SLE 失败根因。未见本轮 `Load access fault`。

## 接下来按最小改动顺序操作

1. 保留当前离线板1与 v4 板2。移走无关标签，固定天线与标签距离和朝向。S1 单独测 1、2、3 张，每次按一次，等 10 秒结束并记录板1 `final accepted unique/samples` 与板2 `tags`；S2～S5 每轮 3 秒。窗口结束后再次按阶段键开始新轮，避免扫描中按键取消前轮。
2. 用一张标签在同一窗口内重复读取，验证 `samples` 增加而 `unique=1`；再只复位板2一次，记录断开、重新扫描、服务发现、ready 和显示恢复；保持两板运行约 10 分钟并定期触发读卡。拍一张 LCD 实际画面，确认 `Tags read` 与串口对应。
3. 当前离线包不启动 Wi-Fi。此前 Wi-Fi 已有实板成功记录，无需为了重复这一项而换旧包、再换回离线包。取得队友可达的真实 `http://IPv4:端口`、确认 API v2 的 `/health` 与 `/device/state` 后，先运行 `deployment_setup.py --url ... --check-only`，再用 `configure_backend_and_build_board1.cmd` 输入该地址及广东现场 Wi-Fi SSID/密码，工具生成启用上传的新板1包。只更新板1；当前配套 v4 板2保留。新板1启动后再核对 `Wi-Fi ready`、`HTTP enabled=1`、目标地址及真实 API 返回。

## 方法、文件和指令

- 阅读用户提供的两份串口附件，按 `OFFLINE BENCH`、EPC/RSSI、`unique/samples`、SLE 服务发现、`ready`、板2 `tags` 与断连时间定位证据。
- 阅读 `clearchain_device_app.c` 的 bench/Wi-Fi 任务条件、读卡与阶段控制；`clearchain_display_link.c` 的完整状态快照字段；`tools/deployment_setup.py` 的 API v2 预检查、Wi-Fi 输入和板1包发布顺序；《进度总结.md》第21次及《解决队友电脑后端地址误限与配置窗口退出_新窗口交接.md》用于确认版本和职责。
- 本轮使用 `Get-Content` 读取附件和源码，`rg -n` 定位日志标志与代码字段，`git status --short` 检查工作区。没有改动固件、运行测试或执行烧录。

## 追加：16:59～17:05 新一轮双板日志

新附件显示板1继续运行 `OFFLINE BENCH`，TCA9555、R200 UART 初始化成功；板2 LCD 初始化、SLE 服务发现和 `ready write status=0x0` 成功。板1约五分钟内完成十二个窗口，`final accepted` 全为 `unique=0 samples=0`，板2对应阶段均收到 `tags=0`。其中 S1 七轮、S2 一轮、S3 一轮、S4 三轮；未见 S5 的新一轮。板1在启动初期有一次断开后重连，原因没有现场操作记录，不能归因。

这份新日志未出现 `EPC=`，因此没有新增实物标签读卡证据；先前 S3/S4 各读到一张的证据仍有效。若本轮未在天线范围放标签，零读数正常。若已放标签，下一步先用此前读到的已知标签固定距离和朝向，按一次 S1 并完整等待十秒，连续做两到三轮；若依然全零，再核对 R200 5V 供电、天线连接、标签类型与距离，并采集该轮完整串口。现在无需重复大量空窗口，也无需重烧固件。完成一张稳定读卡后再做两张、三张、重复样本；S5 按键、一次明确的板2复位重连及约十分钟运行尚待验证。

追加分析使用 `rg -n` 定位两个附件的读卡/SLE标志，`Get-Content` 核对启动至结束时间及窗口收尾；没有执行设备操作或构建。

