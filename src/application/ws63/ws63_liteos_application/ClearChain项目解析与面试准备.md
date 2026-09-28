# ClearChain 项目解析与面试准备

> 文件夹：`D:\ws\fbb\src\application\ws63\ws63_liteos_application`
> 性质：基于 WS63（HiHope HH-D01，RISC-V 32 位 MCU）+ 华为 LiteOS 的嵌入式 RFID 溯源终端固件

---

## 一、项目一句话定位（写简历用）

一个**供应链/冷链溯源扫码终端**：在工厂、FDA、仓库、 checkpoint、医院等节点用 RFID 读取货物标签，
经 WiFi 把扫码记录上报后端，后端依据标签状态返回「通过 / 待核验 / 风险」三色结论，终端用 LED+蜂鸣器做就地反馈。

---

## 二、技术栈

| 层 | 技术 |
|---|---|
| 硬件平台 | WS63（HiHope HH-D01），RISC-V 32 位，集成 WiFi/BT |
| 操作系统 | 华为 LiteOS（LiteOS-M 风格 RTOS） |
| 构建 | CMake + `cc_riscv32_musl_fp_win` 工具链，目标 `WS63-LITEOS-APP` |
| 下载/调试 | 串口烧录（COM15, 115200）/ JLink SWD（port 3333） |
| 射频 | R200 UHF RFID（UART，私有二进制协议）+ RC522/MFRC522 13.56MHz（软件 SPI） |
| 外设 | TCA9555 I2C GPIO 扩展（软件 I2C 模拟）+ LED/蜂鸣器/按键 |
| 网络 | WiFi STA + lwIP TCP Socket，HTTP/1.1 POST JSON |
| 后端 | Python Flask 代理（`server.py`，本地 5000 端口，转发到 ngrok 隧道） |

---

## 三、系统架构（数据流向）

```
        RFID 标签
        /      \
  R200(UART1)  RC522(软件SPI)
   读 EPC       读 UID
        \      /
         tcp_client_demo.c 主任务
              |
    +---------+----------+-------------------+
    |         |          |                   |
 TCA9555    WiFi(STA)   clearchain_http    clearchain_key
 (LED/蜂鸣/按键)  连接@Ruijie   POST /scan JSON   5 个阶段选择按键
                              |
                          lwIP TCP
                              |
                      Flask 后端(server.py)
                      → 本地 GREEN / 转发队友 ngrok
                              |
                  返回 color: GREEN/ORANGE/RED
                              |
                   clearchain_feedback 亮灯+蜂鸣
```

---

## 四、文件清单与作用

### A. SDK 启动 / 系统层（海思/HiHope 提供，非你原创，但被本工程直接使用）
| 文件 | 作用 |
|---|---|
| `main.c` / `main.h` | LiteOS 应用入口与系统初始化：时钟/看门狗/Flash/各任务创建、`main()` → `osKernelStart()`。定义任务表 `g_app_tasks` 与栈/优先级。 |
| `reset_vector.S` | RISC-V 上电复位与异常向量（`mtvec`）、设置 GP/SP、初始化栈、跳 `runtime_init`。 |
| `clock_init.c` / `.h` | 板级时钟与射频电源初始化（`open_rf_power` / `switch_clock` / `set_uart_tcxo_clock_period`），仅在 `BOARD_ASIC` 下编译。 |
| `CMakeLists.txt` | 把 `main.c`、`reset_vector.S`、`clock_init.c` 与 `project/` 下全部业务源文件编成一个组件 `ws63_liteos_app`。 |
| `demo001.hiproj` | HiHope Studio 工程配置：series=cfbb、board=ws63、工具链、烧录口 COM15、调试 JLink SWD、生成 `.fwpkg` / `.elf` / `.map`。 |
| `HH D01可以使用的引脚及引脚不能用的解释pdf.pdf` | **WS63 引脚分配参考手册**（哪些 GPIO 可用/不可用），开发时查引脚用，非代码。 |

### B. 应用业务层（`project/` 下，这是你的核心工作）
| 文件 | 作用 |
|---|---|
| `tcp_client_demo.c` | **主应用任务（程序灵魂）**。`wifi_tcp_client_demo()`：先 `probe` TCA9555，失败则死循环停机；再初始化反馈→连 WiFi→初始化 R200→启动按键任务；然后 `while(1)` 读 R200 EPC、防重复、POST `/scan`、按返回亮灯/蜂鸣。`app_run(tcp_client_demo_entry)` 注册为启动入口。 |
| `clearchain_config.h` | 集中配置：WiFi 名/密码、后端 HOST/PORT/PATH（ngrok）、`SCANNER_ID`、`STAGE_CODE`、`TAG_ID`。 |
| `clearchain_http.c` / `.h` | 在 lwIP 上**手写 HTTP/1.1 POST**：拼 JSON 体、解析 HTTP 响应状态码与 `color`/`status` 字段，返回 `GREEN/ORANGE/RED/UNKNOWN` 枚举；含 JSON 字段匹配与 3xx 重定向处理。 |
| `clearchain_tca9555.c` / `.h` | **软件 I2C 模拟驱动** TCA9555（addr 0x20，SCL=MGPIO13/SDA=MGPIO14）：起始/停止/读写字节、寄存器读写、引脚读写、probe 自检。 |
| `clearchain_key.c` / `.h` | 5 个阶段选择按键（TCA9555 P1.0~P1.4），20ms 轮询 + 2 次消抖，选中供应链阶段（Factory/FDA/Warehouse/Checkpoint/Hospital），独立任务 `ClearChainKey`。 |
| `clearchain_led.c` / `.h` | 红/绿/黄三色 LED（TCA9555 P0.0~0.2）亮灭、闪烁。 |
| `clearchain_buzzer.c` / `.h` | 蜂鸣器（TCA9555 P0.3）按毫秒鸣叫。 |
| `clearchain_feedback.c` / `.h` | **把 LED+蜂鸣器组合成用户反馈语义**：读到标签 / 通过(绿) / 待核验(黄) / 风险报警(红) / 通信失败。 |
| `r200_reader.c` / `.h` | R200 高层读卡逻辑：扫描窗口 2.5s、最多 4 次解析尝试、帧超时 800ms、扫描间隔 300ms、防抖与版本打印。 |
| `r200_uart.c` / `.h` | UART1 驱动（TX=MGPIO15/RX=MGPIO16/115200/8N1）：**RX 中断 + 事件（`osal_event`）**，用 `FULL_OR_IDLE` 条件一次收一帧。 |
| `r200_protocol.c` / `.h` | R200 私有多帧协议：`AA TYPE CMD LEN_H LEN_L DATA CHECK DD`，构造 inventory 命令、解析 inventory 通知→EPC、累加校验和。 |
| `rc522_reader.c` / `.h` | MFRC522（13.56MHz）读卡：request/防冲突(anticollision)/halt，读 4 字节 UID，SPI 寄存器级操作。 |
| `rc522_ws63_gpio.c` / `.h` | **软件 SPI（bit-bang）** 驱动 RC522：NSS=MGPIO8/SCK=MGPIO7/MOSI=MGPIO9/MISO=MGPIO11/RST=MGPIO10，含复位时序与寄存器读写。 |
| `my_wifi_tcp.c` / `.h`、`my_wifi_api.c` / `.h`、`my_wifi_udp.c` / `.h` | WiFi STA 连接 + lwIP TCP/UDP socket 封装（connect / send / recv / close）。注意有两个 `my_wifi_tcp.c`（根 `project/` 与 `project/include/`），职责略有不同。 |
| `server.py` | **PC 端后端（Python）**：`HTTPServer` 实现 `/scan`，无 `TEAMMATE_NGROK` 时本地返回 GREEN，有则转发到队友 ngrok 隧道。用于联调。 |
| `clearchain_http.exe` | 一份 Windows 下编译过的可执行（疑似早期在 PC 上测试 HTTP 客户端的产物），非板端代码。 |
| `__pycache__/` | Python 缓存，来自 `server.py` / 绘图脚本。 |
| `build.log` | 构建日志（当前内容显示旧路径 `d:\ws63\project\build.py` 找不到，是历史残留，非当前报错）。 |

### C. ⚠️ 与本嵌入式项目无关的文件（别写进简历，面试会露馅）
| 文件 | 说明 |
|---|---|
| `paper_figures_final_draft.py` | 一个 **matplotlib 数据可视化脚本**，读 `附件2.xlsx`(光伏实际功率)/`附件3.xlsx`(预测)，画微网「光伏+储能」能量平衡图。这是**数学建模/课程论文**的东西（Problem 3），与 ClearChain RFID 终端毫无关系，只是被放进了同一文件夹。 |

---

## 五、简历写法

### 项目描述（一段式）
> 基于 WS63（RISC-V + 华为 LiteOS）开发了一款供应链 RFID 溯源扫码终端 ClearChain。
> 通过 UHF(R200/UART) 与 13.56MHz(RC522/SPI) 双模 RFID 读取货物标签，经 WiFi 将扫码事件以
> HTTP JSON 上报后端；后端返回通过/待核验/风险三态，终端用 TCA9555 扩展的 LED+蜂鸣器就地反馈。
> 实现了软件 I2C/SPI 位操作驱动、RFID 私有协议解析、按键消抖与标签去重状态机。

### 要点（STAR 拆条，挑你真做过的写）
- 在资源受限 MCU 上用**位操作（bit-bang）手写 I2C/SPI**，驱动 TCA9555 GPIO 扩展与 RC522，规避了硬件外设限制。
- 实现 R200 **私有二进制协议**的组帧/解析/校验，结合 UART RX 中断+事件机制稳定收帧。
- 设计**标签去重 + 等待移卡**状态机，避免同一标签重复上报。
- 在 lwIP 上**手写 HTTP/1.1 POST**（非现成库），完成端云数据闭环。
- 用 RTOS 多任务拆分 RFID 扫描、按键轮询、网络上报，合理分配栈与优先级。

（英文版可直译上述要点；面试若被问"是你自己写的吗"，如实说：应用层 `project/*` 是自己写的，
`main.c`/`reset_vector.S`/`clock_init.c` 是海思 SDK 启动框架，我只做配置与裁剪。）

---

## 六、面试准备（高频问题与回答要点）

**Q1：整体架构 / 数据流？**
讲上面第三节的图：标签→双 RFID→主任务→WiFi→后端→三色反馈。强调"端侧采集+云侧判定+就地反馈"三层。

**Q2：为什么用软件 I2C/SPI（bit-bang）而不是硬件外设？**
要点：开发板/SDK 在所用 GPIO 上未开放硬件 I2C/SPI 控制器，或为了学习与完全可控的时序；
代价是占用 CPU、速率低。可补一句"生产上会优先用硬件外设 DMA 降低占用"。

**Q3：R200 协议怎么解析？**
帧格式 `AA TYPE CMD LEN_H LEN_L DATA CHECK DD`；校验 = 从 TYPE 到 DATA 所有字节累加取低 8 位；
先验证帧头帧尾、长度自洽、校验通过后按 `TYPE=0x02 NOTIFY + CMD=0x22` 提取 EPC 并转十六进制字符串。

**Q4：按键消抖怎么做？**
20ms 轮询一次，连续 2 次稳定在同一电平才确认状态变化，下降沿（低电平=按下，按键接地+上拉）触发阶段切换。

**Q5：标签去重逻辑？**
维护 `last_chip_uid` + `last_scan_stage` + `wait_tag_removed`：同一标签且同阶段不重复发；
发送后置 `wait_tag_removed=1`，必须等标签移出读卡区（`wait_tag_removed` 清零）才接受下一次；
连续 10 轮读不到则清空旧记录。

**Q6：HTTP 为什么用明文、不走 HTTPS？**
当前走 ngrok TCP 隧道 + 裸 HTTP，主要为联调简便；可演进点：加 mbedTLS 做 DTLS/TLS、或走 MQTT+TLS。

**Q7：RTOS 任务怎么划分？**
主扫描任务 + 按键轮询任务（独立 `ClearChainKey`）；UART 收帧用中断+事件唤醒，解析/上报留在任务上下文；
各任务栈 0x800~0x2000、优先级 24 左右，注意栈溢出与优先级反转。

**Q8：上电启动流程？**
`reset_vector.S`(设 mtvec/SP/GP) → `runtime_init`(拷贝数据段、清 BSS、cache 初始化) → `main()`(时钟/看门狗/各驱动) → `osKernelStart()`。

**Q9：怎么调试？**
串口 `osal_printk` 日志；JLink SWD（port 3333）；后端用本地 `server.py` 先自测 GREEN，再接 ngrok。

### 可能被深挖的薄弱点（提前准备）
- 软件 I2C 的时序可靠性、速率上限；
- lwIP 裸 TCP 写 HTTP 的对齐/分包/超时处理；
- 没有用硬件 I2C 的中断/DMA；
- 安全：明文传输、无设备鉴权、无 OTA 升级；
- `main.c` 等是 SDK 代码，要能讲清"哪些是你写的、哪些是框架"。

---

## 七、可继续优化的方向（面试加分项）
1. 用硬件 I2C/SPI + DMA 替代 bit-bang，降低 CPU 占用。
2. 传输加 mbedTLS（DTLS/TLS）或改 MQTT+TLS，增加设备鉴权。
3. 增加 OTA 远程升级、断网本地缓存队列。
4. 低功耗：空闲降频 / 休眠 + 外部中断唤醒。
5. 双 RFID 融合策略（UHF 远距离盘点 + 13.56MHz 近场认证）。
