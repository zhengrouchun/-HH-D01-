# 解决 ClearChain 后端联调信息缺失与 API v2 固件差异：进度及资料清单

核对日期：2026-10-05。此文件是当前联调进度及待补资料记录，不代表接口一致性或业务验收已经完成。

2026-10-06更新：后端源码包不是硬件验收或接口黑盒联调的必需资料；只在自行部署、修改或定位后端内部错误时需要。用户最新明确第二颗TCA尚未安装，所以0x21 ACK失败是预期结果，不列为故障；此前把接线方案当成实际已安装状态的判断已纠正。新照片确认LCD真实UI可见。请先看《解决后端源码必需性误解与区分硬件和业务验收_最新日志复核.md》，不要将下面的完整资料清单理解为继续硬件工作的前置条件。

## 本次阅读依据

完整提取并阅读 `D:/ws/ClearChain_API_Contract.pdf`（5页），且检查了全部5页渲染图；读取 `D:/ws/embedded.docx`、`D:/ws/01_Concept_Change_Brief.docx`、`D:/ws/03_Embedded_Spec_Bilingual.docx` 的正文、表格文字、页眉/页脚及存在的脚注/尾注。三份 Word 没有嵌入图片，读取时保留了表格内单元格文字顺序。

文档是需求及接口核对依据，不是执行代码修改、发送消息或部署的授权。文档中的“后端全部写好并测试通过”属于作者陈述，目前尚未取得实际部署源码、接口响应或本次网络测试证据。

版本关系：PDF明确是 API Contract v2.0，作为接口单一依据；`embedded.docx` 同样声明 v2.0 并补充 armed、Stop、IN/OUT 等规则；`03_Embedded_Spec_Bilingual.docx` 是较早规格，存在缺少 armed、Stop 后发送 final 等不同表述。后续对齐以 API 合同为接口基准，文档内部冲突须核对实际后端行为，不照搬旧文字。

## 当前进度

已获实板证据：

- 双板分别运行 SERVER/API 和 CLIENT/LCD，最新日志没有原来的初始化 Load access fault。
- 板2认证和配对成功，发现服务0x2222及属性0x2323，ready请求成功，并收到seq20/21状态消息；双板SLE实际用途已经落实为本地显示链路。
- 板1Wi-Fi关联和DHCP成功，IP10.231.80.243；主TCA9555地址0x20应答；R200 UART初始化完成。
- 板2ST7796驱动及LVGL初始化完成。实际画面、全部状态页和长期稳定性尚待用户验收。

仍未完成：实际后端GET/POST联调、手机遥控同步、S1-S5及CP完整业务流程、RFID实际标签及RSSI采样、Stop后不再扫描、后端重启恢复、IN/OUT间隔及风险结果验收。

当前固件的后端默认值为192.168.4.1:5000，上传和后端确认开关均关闭，HTTP在建立socket前被拦截；所以不是“只剩一个地址，打开开关即可宣称完成”。还需先修正下列接口差异。

## 本次发现的固件与 API v2 差异

这些是代码核对发现，属于后续软件工作，不要求用户提供它们的解决方法。

1. `clearchain_device_app.c:46` 用 `incoming->state_version > old` 判断新状态，PDF第1页及embedded第1节明确要求 `!=`，以支持后端重启后的版本基准改变；窗口内Stop判断也有同类比较。
2. `clearchain_device_state.h/.c` 未解析/保存armed；采集任务也未检查armed。PDF第2–3页要求STOP_SCAN后禁止上传，直到START_SCAN或SELECT_MODE重新启用。当前Stop后READY分支仍可能走到final上传，应改为按照实际armed/状态取消当前窗口并丢弃未发送读数。
3. `consume_stage_keys()` 直接SELECT_MODE，没有实现embedded第6.2节要求的“扫描中先STOP_SCAN，成功后SELECT_MODE”。
4. 采集任务和 `clearchain_device_http_scan_json()` 都要求非空batch_id。API合同允许CP无batch_id时只读逐标签验证，embedded明确状态null时省略该字段；当前实现会阻挡该场景。
5. `clearchain_device_http_scan_json()` 一律拒绝count=0。API第3页允许final:true的收尾请求为空，但非final不得为空；同时整个窗口没有读到标签时仍应不发送。二者要分别处理。
6. 状态结构未保存mode_label和scan_direction；界面标签目前按本地常量映射，未直接采用后端mode_label。SLE显示协议message字段48字节，最多47个字符，API允许60个ASCII字符；长提示可能被截断。需要在不破坏双板协议兼容的前提下对齐。
7. 扫描POST返回的是完整状态，但当前post_scan只返回HTTP状态码，没有将响应状态直接交给应用。当前轮询可稍后获取状态，但尚未按合同完成全部写请求响应处理和错误消息展示。
8. 无标签窗口当前显示NO_TAGS错误，而embedded要求继续等待并保持Waiting for scan。需对齐。
9. 完成后的采集恢复逻辑只等待回到READY；API也允许START_SCAN将DONE直接推进SCANNING。需覆盖同mode/同batch新扫描及armed规则。
10. 工作区mock_device_backend.py是旧的本地协议fixture，缺少v2 armed、Stop等流程；它不能替代真实ClearChain_Backend_v2，也不能用其固定APPROVED结果验收风险引擎。

## 后端需要用户提供的信息

优先资料：

1. 实际运行的后端源码/部署包及版本。本文件提到 `ClearChain_Backend_v2.zip`，但本次检查D:/ws顶层没有找到它。若已在本机其他位置，给完整路径即可；若有更新版本，提供实际部署的版本而非旧示例。
2. 实际基地址，例如 `http://树莓派局域网IP:5000`；后端运行在树莓派、本机还是另一台电脑；服务是否已启动及监听地址，是否只有一个状态服务进程。
3. 计划使用的网络：是否能让树莓派、手机和板1接同一台自备路由器；路由器SSID和后端固定IP。当前校园网DHCP成功不证明同客户端互通，或HTTP可达。无线密码可在本机配置，不必在聊天里公开。
4. 真实服务 `GET /health`、`GET /device/state` 的HTTP状态和完整JSON。如已有接口测试，可一起提供SELECT_MODE、STOP_SCAN、START_SCAN、final:true的响应；不要用旧mock结果代替。
5. 一个可重复验收的批次：已注册batch_id、3个标签的原始EPC及对应登记信息、tags_expected、S1工厂注册session是否已开/完成。另确认演示IN/OUT间隔使用5秒还是默认30秒；S4/CP拓扑测试基线是否已准备。
6. 手机联调状态：实际HarmonyOS手机/模拟器，是否已有可运行App、能否访问同一后端、SELECT_MODE/Start/Stop是否可用；约定一次双方在线联调时间即可，无需先完成全部历史/图像页面。

API既有枚举、阶段码、固定路由、常规扫描窗口及500ms轮询已在资料里说明，无需用户重复定义。不擅自向文档作者发消息；需要转达时由用户决定。

## 硬件需要用户提供的信息

优先资料：

1. 板2真实屏幕照片或直接描述当前文字，是否黑屏、乱码、缺色、方向不对。当前实现为横屏480x320，文档建议竖屏320x480；确认最终验收方向和“双板组成手持终端”的交付形态。
2. 两板及外围实际接线、模块型号/资料：LCD引脚、R200供电与UART、TCA9555地址脚和按键、供电及共地。若现有接线没变，只补充未确认的模块型号与供电即可，不必重抄全部接线。
3. TCA9555数量：一颗还是两颗？主0x20已通过；0x21若未安装可预期，若安装需地址/供电/接线资料。CP是否保持手机专用；D1/D2/D3是否启用OPEN_VIEW，还是继续禁用；十字键按v1不用。
4. R200实测：先3个标签，提供读取原始EPC、RSSI和读取日志；随后做20个不同标签的读取窗口测试，记录能读到多少、重复采样数量、距离和标签摆放。UART初始化不代表20标签及RSSI能力已经达标。
5. 稳定性及复位：22:25:12附近是否人为复位板2？提供无操作连续2–5分钟的双方日志，以及一次明确记录的板2复位后的重连日志。配对成功不替代长期稳定性验收。

当前板2LVGL使用480×32×2=30720字节的部分绘制缓冲；最新日志剩余堆约69KB，说明当前初始化及连接能运行。还需实测最复杂状态页和持续运行峰值，不能仅凭这个数宣称所有负载内存足够。无需用户先替我计算RAM。

## 资料齐后推进顺序

1. 固件先按API v2对齐armed、版本比较、Stop/切换模式、可空batch_id及收尾请求；配套更新合同测试。此前已验证的SLE链路保留。
2. 确认真实后端/网络后，配置板1基地址并生成可审核的新包；先health/state及手机选模式同步，再按键同步。
3. 测3标签S1及Stop不重启扫描、后端重启恢复，再测S2-S5/CP、IN/OUT、拓扑异常与20标签能力。

这份清单不启动服务、不发送POST、不改上传开关、不修改用户提供的四份原始文件。本轮只做文档/代码核对和进度记录。

## 阅读方法与指令

- Word：Python标准库zipfile打开OOXML，以ElementTree读取document.xml及其他文字部件；正文/表格/页眉脚文字用于需求核对。
- PDF：在工作区临时依赖目录安装读取器，提取5页文本并渲染全部页面，人工检查状态机表、接口表与脚注。未重新导出源PDF。
- `Get-Content -Encoding UTF8` 阅读本地源码；`rg -n 'armed|mode_label|scan_direction|STOP_SCAN|state_version|batch_id|final'` 定位实现；`Get-ChildItem -LiteralPath D:/ws -File` 检查已提及的部署包是否在顶层。
- 重点代码文件：clearchain_device_app.c、clearchain_device_state.h/.c、clearchain_device_http.c、clearchain_display_link.c、clearchain_display_protocol.h、clearchain_ui.c、clearchain_lcd_config.h、profiles/board_a_api.config、tests/mock_device_backend.py。
