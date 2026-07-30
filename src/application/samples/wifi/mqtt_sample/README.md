# MQTT Sample

## 功能说明

本示例演示 WiFi STA 连接后通过 MQTT over TLS 发布消息到 MQTT Broker。

## 前置条件

MQTT Sample 依赖 STA Sample，需先启用 `Support WIFI STA Sample`。

## 配置说明

MQTT 用户名和密码通过 Kconfig 配置，使用以下任一方式进行图形化配置：

- 命令行：`python build.py menuconfig`
- HiSpark IDE 插件：使用 Kconfig 功能配置

在 menuconfig 菜单中依次进入：

```
Configuration
  └── Application
        └── Enable Sample  →  [*] (设为 y)
              └── Enable the Sample of WIFI.  →  [*] (设为 y)
                    └── Sample  →  选择 Support WIFI STA Sample
                          └── STA Sample Configuration
                                └── Support WIFI MQTT Sample  →  [*] (设为 y)
                                      └── MQTT Sample Configuration
                                            ├── (admin)  Set MQTT username
                                            └── (admin)  Set MQTT password
```

### 配置项说明

| 配置项 | 说明 | 默认值 |
|--------|------|--------|
| `WIFI_SSID` | 目标 WiFi 热点的 SSID | `my_softAP` |
| `WIFI_PWD` | 目标 WiFi 热点的密码 | `my_password` |
| `MQTT_USERNAME` | MQTT 认证用户名 | `admin` |
| `MQTT_PASSWORD` | MQTT 认证密码 | `admin` |

## 使用步骤

1. 通过 `python build.py menuconfig` 或 HiSpark IDE 配置 WiFi 和 MQTT 参数
2. 同时需要填充 MQTT Broker URI、客户端证书/私钥/CA 证书等参数
3. 编译并烧录固件
4. 设备启动后自动连接 WiFi 并发布 MQTT 消息
