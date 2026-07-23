
#include "lwip/netifapi.h"
#include "wifi_hotspot.h"
#include "wifi_hotspot_config.h"
#include "wifi_device.h"
#include "stdlib.h"
#include "lwip/nettool/misc.h"
#include "osal_debug.h"
#include "soc_osal.h"

#define WIFI_IFNAME_MAX_SIZE 16
#define WIFI_SCAN_AP_LIMIT 64
#define WIFI_CONN_STATUS_MAX_GET_TIMES 5 /* 启动连接之后，判断是否连接成功的最大尝试次数 */
#define DHCP_BOUND_STATUS_MAX_GET_TIMES 20 /* 启动DHCP Client端功能之后，判断是否绑定成功的最大尝试次数 */
#define WIFI_STA_IP_MAX_GET_TIMES 5 /* 判断是否获取到IP的最大尝试次数 */

static volatile int g_connected = 0;

static volatile int g_softAPStarted = 0;

static volatile int g_joinedStations = 0;

// 打印 station 信息
static void PrintStationInfo(wifi_sta_info_stru *info) 
{
  if (!info) return;
  static char macAddress[32] = {0};
  unsigned char* mac = info->mac_addr;
  sprintf(macAddress,"%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  osal_printk(" PrintStationInfo: mac=%s.\r\n", macAddress);
}

// wifi 连接状态变化回调
static void OnWifiConnectionChanged(int state, wifi_linked_info_stru* info)
{
  if (!info) return;
  osal_printk("OnWifiConnectionChanged info: %p\r\n", info);
  osal_printk("%s %d, state = %d, info = \r\n", __FUNCTION__, __LINE__, state);
  // PrintLinkedInfo(info);

  if (state == WIFI_STATE_AVALIABLE) {
      g_connected = 1;
  } else {
      g_connected = 0;
  }
}

// wifi 扫描状态变化回调
static void OnWifiScanStateChanged(int state, int size)
{
  osal_printk("%s %d, state = %X, size = %d\r\n", __FUNCTION__, __LINE__, state, size);
}

// SoftAP 状态变化回调
static void OnSoftAPStateChanged(int state)
{
  osal_printk("OnSoftAPStateChanged: %d.\r\n", state);
  if (state == WIFI_STATE_AVALIABLE) {
      g_softAPStarted = 1;
  } else {
      g_softAPStarted = 0;
  }
}

// station 连接到 SoftAP 时的回调
static void OnSoftAPStaJoin(wifi_sta_info_stru *info)
{
  osal_printk("OnWiFiStationConnect info: %p\r\n", info);
  g_joinedStations++;
  PrintStationInfo(info);
  printf("+OnSofAPStaJoin: active stations = %d.\r\n", g_joinedStations);
}

// station 断开 SoftAP 时的回调
static void OnSoftAPStaLeave(wifi_sta_info_stru *info)
{
  g_joinedStations--;
  PrintStationInfo(info);
  printf("-OnSoftAPStaLeave: active stations = %d.\r\n", g_joinedStations);
}

// 为 wifi 各个变化状态指定回调函数
static wifi_event_stru g_defaultWifiEventListener = {
  .wifi_event_connection_changed = OnWifiConnectionChanged,
  .wifi_event_scan_state_changed = OnWifiScanStateChanged,
  .wifi_event_softap_sta_join = OnSoftAPStaJoin,
  .wifi_event_softap_sta_leave = OnSoftAPStaLeave,
  .wifi_event_softap_state_changed = OnSoftAPStateChanged,
};


// STA 扫描
static errcode_t example_get_match_network(const char *expected_ssid,
                                          const char *key,
                                          wifi_sta_config_stru *expected_bss)
{
    uint32_t num = WIFI_SCAN_AP_LIMIT; /* 64:扫描到的Wi-Fi网络数量 */
    uint32_t bss_index = 0;

    /* 获取扫描结果 */
    uint32_t scan_len = sizeof(wifi_scan_info_stru) * WIFI_SCAN_AP_LIMIT;
    wifi_scan_info_stru *result = osal_kmalloc(scan_len, OSAL_GFP_ATOMIC);
    if (result == NULL) {
        return ERRCODE_MALLOC;
    }

    memset_s(result, scan_len, 0, scan_len);
    if (wifi_sta_get_scan_info(result, &num) != ERRCODE_SUCC) {
        osal_kfree(result);
        return ERRCODE_FAIL;
    }

    /* 筛选扫描到的Wi-Fi网络，选择待连接的网络 */
    for (bss_index = 0; bss_index < num; bss_index++) {
        if (strlen(expected_ssid) == strlen(result[bss_index].ssid)) {
            if (memcmp(expected_ssid, result[bss_index].ssid, strlen(expected_ssid)) == 0) {
                break;
            }
        }
    }

    /* 未找到待连接AP,可以继续尝试扫描或者退出 */
    if (bss_index >= num) {
        osal_kfree(result);
        return ERRCODE_FAIL;
    }
    /* 找到网络后复制网络信息和接入密码 */
    if (memcpy_s(expected_bss->ssid, WIFI_MAX_SSID_LEN, result[bss_index].ssid, WIFI_MAX_SSID_LEN) != EOK) {
        osal_kfree(result);
        return ERRCODE_MEMCPY;
    }
    if (memcpy_s(expected_bss->bssid, WIFI_MAC_LEN, result[bss_index].bssid, WIFI_MAC_LEN) != EOK) {
        osal_kfree(result);
        return ERRCODE_MEMCPY;
    }
    expected_bss->security_type = result[bss_index].security_type;
    if (memcpy_s(expected_bss->pre_shared_key, WIFI_MAX_KEY_LEN, key, strlen(key)) != EOK) {
        osal_kfree(result);
        return ERRCODE_MEMCPY;
    }
    expected_bss->ip_type = DHCP; /* IP类型为动态DHCP获取 */
    osal_kfree(result);
    return ERRCODE_SUCC;
}

struct netif *netif_p = NULL;

// 连接到热点
errcode_t wifi_connectTo_AP(const char *expected_ssid, const char *key)
{
    char ifname[WIFI_IFNAME_MAX_SIZE + 1] = "wlan0"; /* WiFi STA 网络设备名，SDK默认是wlan0, 以实际名称为准 */
    wifi_sta_config_stru expected_bss = {0};         /* 连接请求信息 */
  
    wifi_linked_info_stru wifi_status;
    uint8_t index = 0;
    /* 注册 Wi-Fi 事件 */
    if(wifi_register_event_cb(&g_defaultWifiEventListener) != ERRCODE_SUCC){
      osal_printk("wifi_register_event_cb error\r\n");
    }
    
    (void)osal_msleep(5000); /* 延时5s，等待wifi初始化完毕 */
    osal_printk("STA try enable.\r\n");
    /* 创建STA */
    if (wifi_sta_enable() != ERRCODE_SUCC) {
      osal_printk("sta enbale fail !\r\n");
        return ERRCODE_FAIL;
    }

    do {
      osal_printk("Start Scan !\r\n");
        (void)osal_msleep(1000); /* 每次触发扫描至少间隔1s */
        /* 启动STA扫描 */
        if (wifi_sta_scan() != ERRCODE_SUCC) {
          osal_printk("STA scan fail, try again !\r\n");
            continue;
        }

        (void)osal_msleep(3000); /* 延时3s, 等待扫描完成 */

        /* 获取待连接的网络 */
        if (example_get_match_network(expected_ssid, key, &expected_bss) != ERRCODE_SUCC) {
          osal_printk("Can not find AP, try again !\r\n");
            continue;
        }

        osal_printk("STA try connect.\r\n");
        /* 启动连接 */
        if (wifi_sta_connect(&expected_bss) != ERRCODE_SUCC) {
            continue;
        }

        /* 检查网络是否连接成功 */
        for (index = 0; index < WIFI_CONN_STATUS_MAX_GET_TIMES; index++) {
            (void)osal_msleep(500); /* 延时500ms */
            memset_s(&wifi_status, sizeof(wifi_linked_info_stru), 0, sizeof(wifi_linked_info_stru));
            if (wifi_sta_get_ap_info(&wifi_status) != ERRCODE_SUCC) {
                continue;
            }
            if (wifi_status.conn_state == WIFI_CONNECTED) {
                break;
            }
        }

        if (wifi_status.conn_state == WIFI_CONNECTED) {
            break; /* 连接成功退出循环 */
        }
    } while (1);

    osal_printk("STA DHCP start.\r\n");

    netif_p = NULL;
    /* DHCP获取IP地址 */
    netif_p = netifapi_netif_find(ifname);
    if (netif_p == NULL) {
        return ERRCODE_FAIL;
    }

    if (netifapi_dhcp_start(netif_p) != ERR_OK) {
      osal_printk("STA DHCP Fail.\r\n");
        return ERRCODE_FAIL;
    }

    for (uint8_t i = 0; i < DHCP_BOUND_STATUS_MAX_GET_TIMES; i++) {
        (void)osal_msleep(500); /* 延时500ms */
        if (netifapi_dhcp_is_bound(netif_p) == ERR_OK) {
          osal_printk("STA DHCP bound success.\r\n");
            break;
        }
    }

    for (uint8_t i = 0; i < WIFI_STA_IP_MAX_GET_TIMES; i++) {
        (void)osal_msleep(10); /* 延时10ms */
        if (netif_p->ip_addr.u_addr.ip4.addr != 0) {
          osal_printk("STA IP %u.%u.%u.%u\r\n", (netif_p->ip_addr.u_addr.ip4.addr & 0x000000ff),
                  (netif_p->ip_addr.u_addr.ip4.addr & 0x0000ff00) >> 8,
                  (netif_p->ip_addr.u_addr.ip4.addr & 0x00ff0000) >> 16,
                  (netif_p->ip_addr.u_addr.ip4.addr & 0xff000000) >> 24);
            netifapi_netif_common(netif_p, dhcp_clients_info_show, NULL);
            if (netifapi_dhcp_start(netif_p) != 0) {
              osal_printk("STA DHCP Fail.\r\n");
                return ERRCODE_FAIL;
            }

            /* 连接成功 */
            osal_printk("STA connect success.\r\n");
            return ERRCODE_SUCC;
        }
    }

    osal_printk("STA connect fail.\r\n");
    return ERRCODE_FAIL;
}

// 启动 wifi softAP
errcode_t softap_starter(void)
{
  /* SoftAp SSID */
  char ssid[WIFI_MAX_SSID_LEN] = "ZXB-WS63";

  char pre_shared_key[WIFI_MAX_KEY_LEN] = "12345678";
  softap_config_stru hapd_conf = {0};

  char ifname[WIFI_IFNAME_MAX_SIZE] = "ap0"; /* WiFi SoftAP 网络设备名，SDK默认是ap0, 以实际名称为准 */
  
  ip4_addr_t st_gw = {0};
  ip4_addr_t st_ipaddr = {0};
  ip4_addr_t st_netmask = {0};
  IP4_ADDR(&st_ipaddr, 192, 168, 63, 1);   /* IP地址设置：192.168.63.1 */
  IP4_ADDR(&st_netmask, 255, 255, 255, 0); /* 子网掩码设置：255.255.255.0 */
  IP4_ADDR(&st_gw, 192, 168, 63, 2);       /* 网关地址设置：192.168.63.2 */

  errcode_t errCode = wifi_register_event_cb(&g_defaultWifiEventListener);
  osal_printk("wifi_register_event_cb: %d\r\n", errCode);

  osal_printk("SoftAP try enable.\r\n");

  (void)memcpy_s(hapd_conf.ssid, sizeof(hapd_conf.ssid), ssid, sizeof(ssid));
  (void)memcpy_s(hapd_conf.pre_shared_key, WIFI_MAX_KEY_LEN, pre_shared_key, WIFI_MAX_KEY_LEN);

  hapd_conf.security_type = WIFI_SEC_TYPE_WPA2_WPA_PSK_MIX; /* 个人级的WPA和WPA2混合 */
  hapd_conf.channel_num = 6;                                /* 6：工作信道设置为6信道 */

  /* 启动SoftAp接口 */
  if (wifi_softap_enable(&hapd_conf) != ERRCODE_SUCC) {
    osal_printk("softap enable fail.\r\n");
    return ERRCODE_FAIL;
  }
  
  netif_p = NULL;
  /* 配置DHCP服务器 */
  netif_p = netif_find(ifname);
  if (netif_p == NULL) {
    osal_printk("find netif fail.\r\n", ifname);
    (void)wifi_softap_disable();
    return ERRCODE_FAIL;
  }
  if (netifapi_netif_set_addr(netif_p, &st_ipaddr, &st_netmask, &st_gw) != ERR_OK) {
    osal_printk("set addr() fail.\r\n");
    (void)wifi_softap_disable();
    return ERRCODE_FAIL;
  }
  if (netifapi_dhcps_start(netif_p, NULL, 0) != ERR_OK) {
    osal_printk("dhcps start() fail.\r\n");
    (void)wifi_softap_disable();
    return ERRCODE_FAIL;
  }
  osal_printk("SoftAp start success.\r\n");
  return ERRCODE_SUCC;
}

// 关闭 wifi SoftAP
void softap_stop(void)
{
  if (netif_p) {
      errcode_t ret = netifapi_dhcps_stop(netif_p);
      osal_printk("netifapi_dhcps_stop: %d\r\n", ret);
  }

  errcode_t errCode = wifi_unregister_event_cb(&g_defaultWifiEventListener);
  osal_printk("UnRegisterWifiEvent: %d\r\n", errCode);

  errCode = wifi_softap_disable();
  osal_printk("DisableHotspot: %d\r\n", errCode);
}