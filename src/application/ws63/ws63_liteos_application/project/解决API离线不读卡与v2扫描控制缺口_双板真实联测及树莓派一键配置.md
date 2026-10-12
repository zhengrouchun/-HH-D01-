# 解决API离线不读卡与v2扫描控制缺口：双板真实联测及树莓派一键配置

> 2026-10-06部署方案更新：用户已取消树莓派，改由队友在用户笔记本运行后端，用户仍只负责硬件。部署步骤以《解决取消树莓派后硬件访问地址与职责不清_笔记本后端联调流程.md》为准；本记录的固件修复及测试证据保留。新入口为tools/configure_laptop_and_build_board1.cmd。

日期：2026-10-06。交付范围是WS63双板固件、离线真实读卡联测、API v2接入工具和主机验证。真实树莓派、手机应用及新固件实板验收尚未执行，不将编译或模拟后端测试记为业务验收完成。

## 1. 当前问题是什么，为什么靠近标签没有输出

完整读取本次两份附件：板1 `a9a7e21c-5ec0-4719-8d52-0e86f427103d` 共461行，板2 `17261893-e89f-448b-a303-9460a3b929ad` 共99行。板1出现主TCA探测成功、R200 UART初始化、Wi-Fi ready及SELECT_MODE HTTP -1；板2认证/配对/服务/属性发现成功，ready写请求成功，收到状态seq364/365。未发现原来的Load access fault。用户已明确五个阶段按钮全部测试正常。

旧API包关闭HTTP开关，且没有真实后端扫描状态。它按业务状态等待扫描，并不因标签靠近而自动盘点。`Server offline`和标签数0不能证明R200坏了，也不需要再次修改SLE配对流程。

此前独立诊断已从真实UART原始帧确认R200可以读卡：214个帧全部通过长度/校验，109个标签采样、105个NO_TAG，7个不同EPC，完整窗口最多5个EPC；最后完整窗口有3个EPC、13次采样。证据在 `output/clearchain/bench_validation_20261006/r200_observed_evidence.json`。LCD实际照片已验证反相OFF（命令0x20）、黑白红绿蓝和480×320横屏。旧LCD自测页的3/5、风险40/80%、APPROVED/REJECT等是模拟页面。

本次日志0x21有ACK，但用户此前明确没有安装第二颗TCA。ACK不等于物理安装已确认；此点单独留待核对，不影响只有0x20的S1～S5及本轮测试。

## 2. 采用哪些资料，解决了哪些代码缺口

以 `D:/ws/ClearChain_API_Contract.pdf` v2.0为接口依据，具体使用：第1～2页状态对象、armed、state_version、ASCII消息及端点；第3页重复RSSI、空final、原始EPC和状态机；第4页S1～S5/CP、IN/OUT、最低间隔及工厂基准；第5页错误码与单进程限制。

已复核 `embedded.docx`、`01_Concept_Change_Brief.docx`、`03_Embedded_Spec_Bilingual.docx` 的本地提取文本，提取件在 `output/clearchain/document_review/`。其中旧嵌入式说明与API v2对Stop/空final的表述有冲突，本轮按v2契约执行：Stop不发final，空readings仅允许final=true。文档中的批次编号和地址示例不当作实际部署资料。

修改的关键文件及位置：

- `clearchain_device_state.c:47` 与 `.h`：增加armed、mode_label、scan_direction、color，消息缓冲64字节；state_version按uint32解析，更新判断采用不等于，支持后端重启后较小版本。
- `clearchain_device_session.c` 与 `.h`：独立管理扫描窗口、最多32个不同标签和32个待发采样；重复采样保留，各自RSSI不丢失；Stop/模式或批次变化取消窗口，DONE不自行重扫。
- `clearchain_device_app.c:83`：阶段按钮先丢弃旧窗口；若后端SCANNING，先STOP再SELECT。`post_pending`接收POST返回的完整状态；失败不自动重发不确定已提交的数据。`reader_tick`使用非阻塞UART取帧，保留前导零EPC及有符号RSSI。
- `clearchain_device_app.c:183`：设备初始化和反馈延时移到调度后的工作线程；正常版等待真正可扫描状态。离线版不启动Wi-Fi/HTTP，按钮启动本地窗口，读取真实标签并经SLE发给LCD，明确无后端判定。
- `clearchain_device_http.c:29`：非阻塞connect加2秒等待、收发超时和长度检查；处理分片接收/发送、409、截断及断线。POST解析返回状态，CP无批次时省略batch_id，空非final禁止、空final允许。
- 开启HTTP构建时额外查出SDK不存在POSIX `ioctl`链接符号。读取 `open_source/lwip/lwip_v2.1.3/src/include/lwip/sockets.h:777` 与`:805`的兼容映射，改用SDK提供的 `lwip_ioctl`（映射为ioctlsocket），并同步主机桩函数；这项必须通过开启通信的SDK链接验证，不能仅靠关闭HTTP包构建判断。
- `clearchain_feedback.c`：LED/蜂鸣器在独立反馈线程响应后端结果，避免蜂鸣阻塞扫描控制；启动自检保留红/绿/黄灯。离线包增加250ms蜂鸣测试，待机绿灯不代表业务APPROVED。
- `clearchain_display_protocol.h:7` 与 `.c`：内部SLE数据协议升到v4，完整携带消息、阶段名称、armed、颜色、IN/OUT/MIXED；不是改变后端API版本。`clearchain_display_link.c`同步新增字段，并避免未变化的在线标志触发多余通知。
- `clearchain_ui.c:90`：消息可换行；结果显示方向；无判定不显示假风险。离线标志变化即使state_version未变也刷新，恢复后清除离线页；本地读卡显示LOCAL READS - NO VERDICT。
- `include/my_wifi_api.c`、`.h`：增加连接状态查询，掉线后重连，STA开启及事件注册不重复执行。真实路由器下的重连效果仍待实测。
- `application/Kconfig`、父级`CMakeLists.txt`、`clearchain_config.h`、`profiles/board_a_api_bench.config`：加入离线版及可配置Wi-Fi参数，保留原用户配置作为默认回退，不输出密码。
- `tools/deployment_setup.py:54`：预检查真实/health API版本及/device/state字段，拒绝标记为fixture的模拟后端；输入URL/SSID/密码后生成本地配置、构建、签名并发布板1包。
- `tools/build_dual.py`、`tools/publish_role.py`：角色隔离、构建后恢复原配置、归档成员检查、启动标记及哈希校验；复制带哈希的新文件名，防止旧包混淆。
- `tools/device_control.py:29`：电脑替代手机先执行STATE、SELECT_MODE、START_SCAN、STOP_SCAN和RESET；选模式时自动先Stop，访问码从客户端既有单一配置读取。不会生成RFID读数、不会注册标签、不会替代手机UI。

未继续修改此前已落地的LiteOS调度门控。新代码与先前SLE配对修复及LCD反相校准共同保留。

## 3. 本次测试与出包证据

主机7组C检查及1组本机HTTP联调通过，共8组。C代码以 `-std=c99 -Wall -Wextra -Werror` 编译。覆盖：

1. SLE协议边界、损坏包与字段往返，R200手册向量、RSSI、容量与窗口。
2. HTTP关闭时不创建连接，不误上传。
3. LCD反相OFF、480×320地址窗、RGB565、SPI 2MHz mode0。
4. API v2 armed门控、重复样本、空final和CP无批次。
5. 直接测试实际device_app代码：Stop不发final，先Stop再切模式，DONE不重扫，后端版本降低仍更新，POST失败不重复提交。
6. 直接测试实际UI代码：同版本离线/恢复刷新、60字符消息、OUT、等待Start、本地与未知结果文案。
7. 开启HTTP的实际客户端：分片I/O、完整返回状态、409、超时、发送失败及截断响应。
8. 实际loopback HTTP：GET/命令/上传、权限、CP、Stop/DONE拒绝、电脑控制工具以及含引号/反斜杠配置往返；模拟服务明确标记MOCK，判定结果均为模拟。

SDK完成正常板2、离线板1、正常API板1三种构建/签名/打包。另用127.0.0.1和虚拟Wi-Fi参数验证开启HTTP/Wi-Fi的构建，测试配置不发布为烧录包。全部新交付包的hardware_verified保持false；必须由用户完成实板验收。

- api_bench: `output/clearchain/_flash_ready/ws63-liteos-app_all_BOARDA_OFFLINE_REAL_RFID_20261006_3ca20202.fwpkg`
  - size: 1538664 bytes; SHA256: `3ca20202dce00e2054e13642b15667a3433b0b44db4dc735e192cc080d560a86`
  - display_protocol=4; HTTP enabled=false; hardware_verified=false.
- b: `output/clearchain/_flash_ready/ws63-liteos-app_all_BOARDB_V2_LCD_SLE_20261006_d0899e0d.fwpkg`
  - size: 1722664 bytes; SHA256: `d0899e0de50d1e702cbba1fe4466e5ff1feec9981c4b72f8495798463b9df49d`
  - display_protocol=4; HTTP enabled=false; hardware_verified=false.
- api: `output/clearchain/_flash_ready/ws63-liteos-app_all_BOARDA_API_V2_20261006_a4fb46d5.fwpkg`
  - size: 1544296 bytes; SHA256: `a4fb46d5436f4e7d10c6668d6a3b782779099011a54da80f54bb90764aa702a9`
  - display_protocol=4; HTTP enabled=false; hardware_verified=false.

目标配置恢复校验：SHA256 `ca5949130199a0e59297efa505d2596c2bf69b30a687ee570ecc728b83ba8046` (matches original configuration)。

复用的验证指令（PowerShell，工作目录 `D:/ws/fbb/src`）：

```powershell
& 'D:/ws/tools/python/python.exe' application/ws63/ws63_liteos_application/project/tests/run_host_tests.py
& 'D:/ws/tools/python/python.exe' application/ws63/ws63_liteos_application/project/tests/test_backend_v2.py
$env:CCACHE_DIR='D:/ws/fbb/src/output/clearchain/ccache'
& 'D:/ws/tools/python/python.exe' application/ws63/ws63_liteos_application/project/tools/build_dual.py api_bench
& 'D:/ws/tools/python/python.exe' application/ws63/ws63_liteos_application/project/tools/publish_role.py api_bench
& 'D:/ws/tools/python/python.exe' application/ws63/ws63_liteos_application/project/tools/build_dual.py b
& 'D:/ws/tools/python/python.exe' application/ws63/ws63_liteos_application/project/tools/publish_role.py b
```

角色构建必须顺序执行，不并行使用公共SDK输出目录。结果见 `output/clearchain/host_tests/results.txt`、`bench_validation_20261006/final_build_*.log` 及各角色manifest。新的离线联测使用下面的v4配套包，旧独立诊断包及旧v3正常包仍保留，不能混搭协议。

## 4. 现在无需后端即可完成的测试

不需要把所有可选元件补齐。保留已经测试正常的接线：板1+第一颗TCA及阶段键+R200和天线，板2+LCD。两板各自USB供电、各自与外设共地，板间通过SLE。不用补第二颗TCA或D键，不改LCD横屏。

当前主要接线：板1GPIO13=SCL、GPIO14=SDA、TCA地址0x20，P10～P14为S1～S5；GPIO15/TX→R200 RX、GPIO16/RX←R200 TX，R200稳定5V供电，UART为115200 8N1，信号为3.3V TTL。板2LCD用3.3V，SCK=GPIO7、MOSI=GPIO9、CS=GPIO8、DC=GPIO1、RST=GPIO14，BL常亮，MISO不接。完整LED/蜂鸣器及电源注意事项沿用《解决双板接线资料矛盾与验收顺序不清_当前接线及操作清单.md》。

操作步骤：

1. 板1烧本轮BOARDA_OFFLINE_REAL_RFID，板2烧本轮BOARDB_V2_LCD_SLE。两块都要更新，v3/v4不能混搭。关闭对应COM口的串口助手再烧，等100%成功；随后打开串口，分别复位，确认SLE发现和ready成功。
2. 板1应出现 `OFFLINE BENCH: real EPC/RSSI/counts; no backend verdict`；板2出现 `state-v4` 和反相OFF。若缺少标记，先核对包名与角色。
3. 将其他标签移远，按一下S1，出现 `capture begins mode=S1` 后把一张已读过的UHF标签放入天线读区。S1采集10秒；结束记录EPC、RSSI、unique和samples。重复读数算多个samples，unique仍为1。
4. 依次用2张、3张标签再按S1测试。LCD最终Tags read应与该窗口串口unique一致；未读全先看读距、朝向和隔离，不把窗口数量当成保证全部同时读到。窗口结束后不会自动重扫，需要再次按阶段键。
5. S2～S5每次采集3秒。扫描中按其他阶段键会取消旧窗口再切换，不把两个阶段样本混起来。按钮已测通，不要求重做电气测试。
6. 观察启动红/绿/黄灯及250ms蜂鸣（只对已正确接入的反馈模块验收）。复位板2后应重连并收到完整状态；持续运行10分钟，记录非人为断连、重启、读卡错误及屏幕异常。

离线版本的EPC/RSSI、unique和LCD Tags read来自真实标签。阶段命令和时间窗口由本地测试逻辑驱动；它不连接真实后端，因此没有工厂登记、链路履历、IN/OUT判定、拓扑或风险结果。LCD出现LOCAL TEST/NO VERDICT是预期状态。

## 5. 拿到树莓派地址和Wi-Fi后，尽量只配置一次

需要完整Base URL，如 `http://192.168.1.20:5000`，不是板1获得的DHCP地址。后端按现有v2文档运行单一进程，并确保电脑及WS63能访问它；手机与WS63均访问这个后端，手机不直接连WS63。

1. 先让电脑连接可访问树莓派的网络，确认后端已启动。
2. 双击 `tools/configure_pi_and_build_board1.cmd`，依次输入完整API URL、Wi-Fi名称、Wi-Fi密码。密码隐藏输入；程序先GET检查v2，再开启两个HTTP开关并自动构建、签名、校验板1包。
3. 烧程序最后输出的板1新包。板2保留本轮v4 LCD/SLE包，通常不再重烧。不要把目前HTTP关闭的BOARDA_API_V2模板误当成已填好真实地址的部署包。
4. 不用手机也可先用电脑控制工具查询或启动/停止，下面IP/批次名均需替换成实际值：

```powershell
& 'D:/ws/tools/python/python.exe' application/ws63/ws63_liteos_application/project/tools/device_control.py --url http://192.168.1.20:5000
& 'D:/ws/tools/python/python.exe' application/ws63/ws63_liteos_application/project/tools/device_control.py --url http://192.168.1.20:5000 --command SELECT_MODE --mode S1 --batch-id ACTUAL_REGISTERED_BATCH --expected 3
& 'D:/ws/tools/python/python.exe' application/ws63/ws63_liteos_application/project/tools/device_control.py --url http://192.168.1.20:5000 --command STOP_SCAN
& 'D:/ws/tools/python/python.exe' application/ws63/ws63_liteos_application/project/tools/device_control.py --url http://192.168.1.20:5000 --command START_SCAN
```

SELECT_MODE会重新准备一次扫描；SCANNING时先Stop。START_SCAN用于Stop后的重新开始或DONE后的下一轮。按硬件阶段按钮可以选S1～S5；CP由手机或电脑选，无需第二颗TCA。断网、上传失败或容量超限时暂停，避免不确定重复提交；恢复后可能需要重新SELECT或按Stop再Start，不自动补发旧数据。

## 6. 后端与硬件还剩哪些真实验收，不能代做什么

对固件配置，补实际URL、SSID和密码即可。完整业务结果还需要真实服务和标签登记数据；不是继续索要已经给出的接口文档，也不是要求必须有后端源码。

已生成 `output/clearchain/bench_validation_20261006/backend_registration_handoff.json`，提供3个曾真实读出的EPC给后端登记：

- `E28069150000502091030968`
- `E28069150000402091031568`
- `E28069150000402091031968`

这只是候选标签清单，实际测试时需要使用对应实物；不确定对应关系时先用离线包逐张核对。后端确认这3张属于哪个真实batch_id、工厂会话/基准是否已经保存。未登记时v2可能正常返回REJECT和Batch not registered，这与通信失败不同。演示IN/OUT间隔建议后端设5秒；若不改则按默认至少30秒，S4不受此规则限制。两次扫描分别代表到达IN和离开OUT，实际方向由后端返回，固件不编造。

后端算法、数据库/工厂基准、手机应用页面及手机同步不能只凭这些固件代为完成验收；不需要后端源码来测硬件，但需要运行中的服务来验真实结果。手机暂未就绪，可先用电脑工具完成设备侧流程，最终再由手机执行相同命令核对同步和访问权限。

硬件基础已有证据：SLE配对/状态接收、LCD颜色、R200真实EPC/RSSI和用户确认的S1～S5按钮。仍待本轮新包在实板上确认真实RFID→SLE→LCD联动、反馈、掉线恢复及持续运行；批次20标签规模与真实网络Stop时延尚未验收。固件单窗口容量32个不同标签，超限暂停，不能宣称不限规模。

可用于汇报：基础硬件和星闪链路已经打通，本轮完成离线真实读卡联测包、API v2扫描控制、完整结果字段显示及树莓派一次配置工具；8组主机检查和三种SDK包构建通过。新包等待实板联测，真实后端/标签登记/IN-OUT及手机端业务验收仍需现场完成，不按主观百分比报告已全部完成。
