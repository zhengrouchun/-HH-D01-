# 解决离线固件不主动读卡与LCD颜色难以验收：独立诊断操作及汇报

日期：2026-10-06。此文件说明新诊断固件、最小接线和实板验收方式。编译/主机测试通过不等于R200或LCD已实板验收。诊断程序不访问后端，也不承担正常双板SLE显示链路。

## 用户当前可以汇报的进度

已完成双板启动和基础联调：板1Wi-Fi关联及DHCP成功，第一颗TCA9555地址0x20应答正常；两块HH-D01之间的SLE已完成连接、认证配对、显示服务发现及实际状态传输，板2LCD能够显示启动页和正式状态界面。最新日志中的断连已由用户确认是手动复位/断电板2造成，约4.5秒后重新连接，随后约9分46秒未再记录断连或原来的初始化崩溃。

当前推进R200真实标签/EPC/RSSI及LCD颜色、状态页验收。第二颗TCA尚未安装，0x21失败属预期，不是核心链路障碍。API v2接口说明已具备，真实后端地址尚待提供；当前固件HTTP关闭，后端离线显示不表示SLE失败。后续仍需修正固件API v2差异，完成实际后端、手机同步、工厂批次基准、IN/OUT及风险结果验收。

本次准备的独立诊断包使读卡和屏幕测试不再被后端离线阻挡；其结果需要用户烧录后提供，不能提前称作读卡成功或颜色已修好。没有可靠工作量权重，不把几个通过项目换算成总完成百分比。

## 是否需要全部外设和后端

不需要。两项分别测试，可以只开一块板。

- R200测试最少：板1、R200、匹配天线、至少一张UHF标签、稳定5V与串口日志连接。无需LCD、板2、TCA、按键、LED、蜂鸣器、Wi-Fi、批次或后端。
- LCD测试最少：板2、ST7796无触摸SPI屏、稳定3.3V与串口日志连接。无需板1、R200、TCA、Wi-Fi或后端。
- 已正确连接的TCA/按键等可以留在板1，但诊断程序不会操作它们；不用为了测试补装第二颗TCA。
- 诊断期间不会建立应用SLE链路，不以没有Connected或原客户端等待服务器作为失败。正常链路在恢复原SERVER/API和CLIENT/LCD固件后再检查。

## 诊断烧录包

板1只烧R200诊断：

`D:/ws/fbb/src/output/clearchain/_flash_ready/ws63-liteos-app_all_BOARDA_R200_DIAG_20261006_v1_0504a3e9.fwpkg`

板1启动标记：`[R200 TEST] r200-standalone-20261006-v1`。

板2只烧LCD色块诊断：

`D:/ws/fbb/src/output/clearchain/_flash_ready/ws63-liteos-app_all_BOARDB_LCD_COLORS_20261006_v1_36129784.fwpkg`

板2启动标记：`[LCD TEST] lcd-colors-inversion-20261006-v1`。两包已复制至_flash_ready，不要混烧两块板。

## 烧录和启动步骤

1. 先确认是哪块板对应哪个COM口，不根据附件数量猜测角色。串口监视器先关闭，避免占用烧录端口。
2. BurnTool选择该板对应诊断包，Connect后按该板真正复位键进入工具要求的连接过程；按此前已经成功的烧录流程操作。
3. 等待100%及明确成功。若仍是Sending file timeout/0%，没有新固件验收证据，不往下解释诊断输出。必要时按已验证方法烧裸板，成功后断电再接外围。
4. 关闭/断开BurnTool，释放端口后打开串口监视器，debug UART使用115200、8N1、无流控或沿用当前能正常读取APP日志的参数。
5. 确认出现对应新启动标记，再开始下述测试。如果仍是server peer-bond-recovery或client peer-cleanup标记，就还在跑正常双板包，不是诊断包。

## 板1R200最小接线

```text
板1USB → 调试电脑，供板子电源并读取调试日志
稳定5V → R200 VCC
5V电源GND → R200 GND、板1GND
板1GPIO15/TX → R200 TTL_RX
板1GPIO16/RX ← R200 TTL_TX
R200射频接口 → 配套天线
```

5V只是R200供电，GPIO的UART是3.3V TTL；不能把5V正极接到GPIO。先断电接线，上电/读卡前接好天线。板1的调试串口与R200 UART1是不同用途。

本诊断重复使用当前源码已有的0x22库存命令 `AA 00 22 00 00 22 DD`，不修改模块RF功率、地区/频点、标签内容或后端登记。

### R200实际操作

1. 启动后看到标记及UART1 TX=GPIO15 RX=GPIO16 115200 8N1。程序自动轮询，不需要按S1，也不需要发送Start/API命令。
2. 先不放标签，保留至少两次SUMMARY。观察是NO_TAG回复还是RX timeout。
3. 放一张已知兼容的UHF标签，先从距天线约20～30cm处调整距离/方向，保持约20秒。需要真正出现EPC及RSSI，不以UART初始化或TX打印代替读卡通过。
4. 示例日志只是格式，不是已实测值：

```text
[R200 TEST] TX bytes=7 AA 00 22 00 00 22 DD
[R200 TEST] RX bytes=... AA ... DD
[R200 TEST] EPC=<真实EPC> RSSI=<实际有符号数值> dBm unique=1 samples=...
[R200 TEST] SUMMARY window=... unique=1 samples=... replies=...
```

5. 移走标签，观察后续窗口中不再继续产生该标签读数。去重表每5次轮询清零一次，所以刚移走时当前窗口的unique仍可保留，到下一空窗口才应归零；不能把旧计数理解为还在读卡。
6. 依次单独读三张标签，记下三个EPC；再三张同时摆放，保持多个窗口，检查是否能在同一窗口看到三个不同EPC。unique是5次轮询窗口中的去重数，不保证三张在同一瞬间被读取。
7. 保存从启动到测试结束的完整板1文本日志，记录各阶段何时放入/移走标签。RSSI会随距离/朝向/环境变化，不要求每次固定到某个数字。

### R200如何判读

- `EPC=... RSSI=...`：有效库存回包已按当前协议解析，实读及RSSI路径有证据。
- `reader replied NO_TAG`：收到协议规定的无标签响应，说明UART至少收到模块回复；再检查标签兼容性、天线、摆放、功率/频段等，不能先判UART断线。
- `RX timeout: no valid protocol frame`：没有得到符合当前帧规则的完整回包。检查稳定电源、共地、TX/RX交叉、波特率和厂家协议。它不严格等同于物理UART完全没有收到字节。
- 有RX hex但`reply rejected status=...`：保留原始十六进制，可能是模块错误回复或协议差异，需按实际回包分析。
- `bad_frames/dropped/uart_errors`持续增加：先处理帧/缓存/串口错误，不把不可靠的数据用于业务验收。

若始终无有效回包，需要卖家针对这块R200开发板提供的UART命令协议/测试软件，商品图片只含型号和电气参数，没有完整命令定义。

## 板2LCD最小接线

```text
板2USB → 调试电脑
稳定3.3V → LCD VCC
3.3V电源GND → LCD GND、板2GND
LCD SCK      → 板2GPIO7
LCD SDI/MOSI → 板2GPIO9
LCD CS       → 板2GPIO8
LCD DC/RS    → 板2GPIO1
LCD RESET    → 板2GPIO14
LCD LED/BL   → 3.3V，背光常亮
LCD SDO/MISO、触摸、SD卡 → 不接
```

保留已有正确接线。外部3.3V电源输出不与板载3.3V输出并联。当前为480×320横屏、SPI 2MHz mode0。

### LCD实际操作与预期

1. 启动后看到lcd-colors-inversion-20261006-v1标记和LCD初始化成功。只有背光亮不能算SPI显示通过。
2. 先显示`INVERSION=OFF command=0x20`，持续8秒。屏幕从左到右应是BLACK、WHITE、RED、GREEN、BLUE五条；记录实际颜色与文字是否匹配。
3. 接着显示`INVERSION=ON command=0x21`，持续8秒，重复相同图案。记录哪一阶段黑白及红绿蓝正确。程序没有自动判断面板颜色，正确阶段需实物观察。
4. 随后以OFF状态循环8个模拟页面：IDLE、READY、SCANNING（60%、3/5）、APPROVED、MONITOR、REJECT、ERROR、后端OFFLINE，每页约6秒；完成后重复两种色块测试。
5. 给两张色块照片分别标注OFF或ON，再提供串口日志。如果只红蓝互换而黑白/绿色正确，检查BGR顺序；如果黑白及绿色/品红整体反转，重点是反相选择。
6. 模拟APPROVED/MONITOR/REJECT是屏幕测试数据，不是实际RFID、后台或风险算法验收结果。

通过标准：完整横屏图案，边缘无明显裁切，文字清晰，至少一组反相设置下颜色与标签一致，页面可持续轮换，无持续花屏、SPI错误或崩溃。原正常CLIENT包的初始化参数仍保留，确认实物结果后再决定正式固件的反相设置。

## 测完恢复正常双板

板1恢复：

`output/clearchain/_flash_ready/ws63-liteos-app_all_BOARDA_PEER_PAIR_RECOVERY_20261005-210011.fwpkg`

板2恢复：

`output/clearchain/_flash_ready/ws63-liteos-app_all_BOARDB_PEER_CLEANUP_20261005-213507.fwpkg`

各自等待烧录成功，再启动双板，核对原SERVER/CLIENT标记及SLE连接、服务发现和状态接收。诊断包只是临时分项测试；不能拿R200-only与LCD-only组合等待SLE连接。

## 后端何时才需要资料

以上测试不需要后端地址、源码包、batch_id或手机App。真实API联调时才需要实际可访问的Base URL及所属网络；地址不是板1的STA IP或DHCP网关。接口格式已在API合同和embedded.docx中，无需重复索要样例。后续工厂登记/批次基准和手机同步再分阶段准备。

## 本次问题、解决方法及文件位置

原API采集任务依赖有效后端状态且后端离线时等待，导致纯硬件读卡无法主动验证；旧LCD样例仅给RGB图案，无法直观区分黑白反相和红蓝顺序。解决方式是新增独立应用入口/构建profile并隔离应用源码，把读卡、色块、模拟页面变为主动运行的台架测试。

- `application/Kconfig`：新增R200_SELFTEST，仅允许OFF角色且关闭samples；正常双板默认关闭诊断。
- `application/ws63/ws63_liteos_application/CMakeLists.txt`：R200诊断只编译UART、协议及自测入口，不编译TCA、Wi-Fi应用或正常SERVER/API入口；LCD诊断复用既有SELFTEST分支。
- `project/clearchain_r200_selftest.c`：调度启动后任务自动发送0x22，打印TX/RX、EPC、signed RSSI和5次轮询去重汇总，明确NO_TAG/timeout/错误回复。
- `project/profiles/board_a_r200_test.config`：OFF角色、关闭上传/后端确认、关闭普通sample，不需要接TCA。
- `project/clearchain_lcd_selftest.c`：五色块及OFF/ON两阶段，复用8个模拟状态页；等待期间持续render，使3秒模式横幅过期后真正显示结果页。后台结果只模拟显示，不伪称业务完成。
- `project/clearchain_lcd.h/.c`：新增任务内调用的set_inversion校准接口，初始化成功前拒绝调用；正常初始化仍为原参数。
- `project/tools/build_dual.py`：新增r200_test角色，检查生成宏和应用归档成员，分别复制包/配置/manifest，退出后恢复用户配置。
- `project/tests/test_lcd_trace.c`：复用SPI跟踪测试，补充反相切换命令及未初始化拒绝的验证。
- 既有`r200_uart.c/.h`、`r200_protocol.c/.h`：复用UART缓存、完整帧检查、库存命令和EPC/RSSI解析，没有新增写标签或后端操作。
- 已有最新日志、接线笔记、ST7796S第165–166页和35002模块接口表：确定进度、最小接线、0x20/0x21及供电逻辑。

## 实际使用命令与验证

```powershell
cd D:/ws/fbb/src
$env:CCACHE_DIR='D:/ws/fbb/src/output/clearchain/ccache'
& 'D:/ws/tools/python/python.exe' application/ws63/ws63_liteos_application/project/tests/run_host_tests.py
& 'D:/ws/tools/python/python.exe' application/ws63/ws63_liteos_application/project/tools/build_dual.py r200_test
& 'D:/ws/tools/python/python.exe' application/ws63/ws63_liteos_application/project/tools/build_dual.py lcd_test
```

主机4组原有检查通过，涵盖协议帧/批次/RSSI、关闭HTTP时的门控、LCD地址窗口/SPI及反相命令、当前状态/JSON实现。旧API测试不能代替v2全部规则修正或实板验收。

构建脚本检查应用归档和角色，打包后按manifest核对SHA256，检查sign.bin包含新启动标记，复制至带角色/日期/哈希的_flash_ready路径。全过程没有自动连接COM、烧录硬件或访问网络服务。

原目标配置SHA256为CA5949130199A0E59297EFA505D2596C2BF69B30A687EE570ECC728B83BA8046，全部构建结束后已核对完全恢复一致。构建使用SDK Python，实际编译、签名和打包成功，并校验真实产物。

## 最终构建确认

板1R200诊断已生成，大小1530216字节，SHA256：

`0504a3e92726475b552f228d1164c0a1f34b36618ad158cd824220e06e0f73e7`

板2LCD色块诊断已生成，大小1718696字节，SHA256：

`36129784fb1e9c4271e285e824282c4a9bdf51f077eb47ec64f3bb5f6e6d6202`

两个最终包已检查sign.bin启动标记、应用归档成员、角色宏、manifest中全部文件哈希及_flash_ready复制后的包哈希。原目标配置已恢复同一SHA256。两包metadata中的hardware_verified维持false，等待用户实板测试。
