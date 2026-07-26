# ClearChain WS63 硬件端与后端联调说明

负责人：郑柔纯  
日期：2026-07-26

## 1. 当前硬件端状态

WS63 硬件端已经完成以下能力：

1. WS63 可连接 WiFi。
2. WS63 UART1 可向 R200 UHF RFID 模块发送盘点命令。
3. R200 可返回 EPC 数据。
4. WS63 已成功解析 EPC。
5. WS63 已按约定字段组装 JSON。
6. WS63 已能向本地后端地址发送 `POST /scan` 请求。

## 2. 当前发送接口

请求方法：

```text
POST /scan
```

请求头：

```text
Content-Type: application/json
```

请求体：

```json
{
  "tag_id": "E28011704000021D35AFADD9",
  "location": "Checkpoint-01",
  "stage": 4,
  "scan_type": 1,
  "stage_code": "PUB-c72m"
}
```

## 3. 请后端确认的内容

请后端负责人确认以下事项：

1. `/scan` 是否仍然只需要这五个字段：

```text
tag_id, location, stage, scan_type, stage_code
```

2. `stage` 和 `scan_type` 是否保持数字类型，而不是字符串类型。
3. `stage_code` 是否继续使用 `PUB-c72m`。
4. mock/ngrok 地址是否支持普通 HTTP 请求。

## 4. 关于 ngrok 地址的重要说明

当前 WS63 代码使用的是原始 TCP 方式手动拼接 HTTP 请求，并不是 HTTPS 客户端。

因此，如果后端给的是：

```text
https://xxxx.ngrok-free.app
```

硬件端可能无法直接请求，因为 HTTPS 需要 TLS 支持。

推荐后端提供以下形式之一：

```text
http://xxxx.ngrok-free.app/scan
```

或：

```text
公网 IP + HTTP 端口 + /scan
```

如果只能提供 HTTPS 地址，则硬件端需要额外增加 TLS/HTTPS 客户端支持，不能只修改 IP 地址。

## 5. 硬件端需要修改的位置

配置文件：

```text
D:\ws\fbb\src\application\ws63\ws63_liteos_application\project\clearchain_config.h
```

当前配置：

```c
#define SERVER_IP "192.168.110.142"
#define SERVER_PORT 5000
#define SERVER_PATH "/scan"
```

收到后端地址后，需要根据地址修改 `SERVER_IP`、`SERVER_PORT` 和 `SERVER_PATH`。

## 6. 给后端队友的联调消息模板

可以直接发送：

```text
我这边 WS63 + R200 硬件端已经可以读到 UHF EPC，并且已经能组装 JSON 发送 POST /scan。

当前发送字段是：
{
  "tag_id": "E28011704000021D35AFADD9",
  "location": "Checkpoint-01",
  "stage": 4,
  "scan_type": 1,
  "stage_code": "PUB-c72m"
}

请给我一个 mock/ngrok 公网测试地址。注意我现在硬件端是原始 TCP HTTP 请求，最好给 http:// 地址；如果只有 https:// 地址，我这边可能需要额外做 TLS/HTTPS 适配。

你收到请求后请帮我确认 Flask 控制台是否打印了 POST /scan，以及是否成功接收到 tag_id。
```

