# RADAR SoftAP Sample

## 功能说明

本示例演示雷达通过 WiFi SoftAP 模式创建热点，并通过 Socket 传输雷达数据。

## 配置说明

WiFi SSID 和密码通过 Kconfig 配置，使用以下任一方式进行图形化配置：

- 命令行：`python build.py menuconfig`
- HiSpark IDE 插件：使用 Kconfig 功能配置

在 menuconfig 菜单中依次进入：

```
Configuration
  └── Application
        └── Enable Sample  →  [*] (设为 y)
              └── Enable the Sample of RADAR.  →  [*] (设为 y)
                    └── Support RADAR SoftAP Sample  →  [*] (选中)
                          └── RADAR SoftAP Sample Configuration
                                ├── (my_softAP)  Set WIFI SSID
                                └── (123456789)  Set WIFI password
```

### 配置项说明

| 配置项 | 说明 | 默认值 |
|--------|------|--------|
| `WIFI_SSID` | SoftAP 热点的 SSID | `my_softAP` |
| `WIFI_PWD` | SoftAP 热点的密码 | `123456789` |

## 使用步骤

1. 通过 `python build.py menuconfig` 或 HiSpark IDE 配置 SoftAP 的 SSID 和密码
2. 编译并烧录固件
3. 设备启动后会自动创建 WiFi 热点（SSID 格式：`配置的SSID-{iso值}`）
4. 其他设备可连接该热点进行雷达数据传输
