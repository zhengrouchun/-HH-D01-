#ifndef CLEARCHAIN_CONFIG_H
#define CLEARCHAIN_CONFIG_H


// =============================
// WiFi配置
// =============================

#define WIFI_SSID_NAME "@Ruijie-456"

#define WIFI_SSID_KEY "12345679"


// =============================
// 后端服务器配置
// =============================

// Flask服务器IP
#define SERVER_IP "0.tcp.jp.ngrok.io"


// Flask端口
#define SERVER_PORT 25279


// HTTP接口
#define SERVER_PATH "/scan"

/* New device API. Override the address in the Board A API build profile. */
#ifndef CONFIG_CLEARCHAIN_DEVICE_HOST
#define CONFIG_CLEARCHAIN_DEVICE_HOST "192.168.4.1"
#endif
#ifndef CONFIG_CLEARCHAIN_DEVICE_PORT
#define CONFIG_CLEARCHAIN_DEVICE_PORT 5000
#endif
#define CLEARCHAIN_DEVICE_HOST CONFIG_CLEARCHAIN_DEVICE_HOST
#define CLEARCHAIN_DEVICE_PORT CONFIG_CLEARCHAIN_DEVICE_PORT

/* The documented stage codes live in one place. */
#define CLEARCHAIN_ACCESS_S1 "PROD-7f2a"
#define CLEARCHAIN_ACCESS_S2 "FDA-91xq"
#define CLEARCHAIN_ACCESS_S3 "WARE-3kd8"
#define CLEARCHAIN_ACCESS_S4 "PUB-c72m"
#define CLEARCHAIN_ACCESS_S5 "PRIV-a9z1"
#define CLEARCHAIN_ACCESS_CP "VERIFY-q4m8"


// =============================
// ClearChain扫描点配置
// 智能模式字段
// =============================


// 当前扫描设备编号
// 后端根据 scanner_id 自动判断地点和供应链阶段
#define SCANNER_ID "scanner_checkpoint"


// 扫描类型
#define SCAN_TYPE 1


// 阶段编码
// Checkpoint阶段
#define SCAN_STAGE_CODE "PUB-c72m"


// =============================
// RFID标签
// =============================

// 测试标签编号
// 实际运行时使用R200读取到的EPC
#define TAG_ID "DEMO001"


#endif
