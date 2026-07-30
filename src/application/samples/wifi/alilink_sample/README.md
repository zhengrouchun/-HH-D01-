# ALILINK Sample

## 功能说明

本示例演示 WiFi STA 自动连接后接入阿里云 IoT 平台（阿里云物联网）。

## 配置说明

WiFi SSID 和密码通过 Kconfig 配置，使用以下任一方式进行图形化配置：

- 命令行：`python build.py menuconfig`
- HiSpark IDE 插件：使用 Kconfig 功能配置

在 menuconfig 菜单中依次进入：

```
Configuration
  └── Application
        └── Enable Sample  →  [*] (设为 y)
              └── Enable the Sample of WIFI.  →  [*] (设为 y)
                    └── Sample  →  选择 Support ALILINK Sample
                          └── ALILINK Sample Configuration
                                ├── (my_softAP)  Set WIFI SSID
                                └── (my_password)  Set WIFI password
```

### 配置项说明

| 配置项 | 说明 | 默认值 |
|--------|------|--------|
| `WIFI_SSID` | 目标 WiFi 热点的 SSID | `my_softAP` |
| `WIFI_PWD` | 目标 WiFi 热点的密码 | `my_password` |

## 使用步骤

1. 通过 `python build.py menuconfig` 或 HiSpark IDE 配置目标 WiFi 的 SSID 和密码
2. 配置阿里云 IoT 三元组信息（ProductKey、DeviceName、DeviceSecret）
3. 编译并烧录固件
4. 设备启动后会自动连接 WiFi 并接入阿里云 IoT 平台
