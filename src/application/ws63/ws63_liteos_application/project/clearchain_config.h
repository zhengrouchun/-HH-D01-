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
#define SERVER_IP "192.168.110.142"


// Flask端口
#define SERVER_PORT 5000


// HTTP接口
#define SERVER_PATH "/scan"


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