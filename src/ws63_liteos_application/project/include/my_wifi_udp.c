
#include "string.h"
#include "wifi_device.h"
#include "wifi_hotspot.h"
#include "wifi_hotspot_config.h"

#include "lwip/nettool/misc.h"
#include "lwip/ip4_addr.h"
#include "lwip/netif.h"
#include "lwip/netifapi.h"
#include "lwip/sockets.h"

#include "osal_debug.h"
#include "soc_osal.h"

static int socket_fd = -1;
static int server_socket_fd = -1;
static char recvData[512] = {0};

static struct sockaddr_in client_addr = {0};

//作为UDP客户端，向UDP服务器发送数据
int UDPClient_SendData(const char *server_ip, unsigned short server_port, const char *send_data){
  //创建socket
 if(socket_fd == -1){ //如果没有socket，就创建
    socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
  }

  //初始化预连接的服务端地址
  //服务器的地址信息
  struct sockaddr_in send_addr;
  send_addr.sin_family = AF_INET; // IPV4
  send_addr.sin_port = htons(server_port); // 远端服务器的端口号
  send_addr.sin_addr.s_addr = inet_addr(server_ip); // 远端服务器的IP地址
  socklen_t addr_length = sizeof(send_addr);

  //发送数据到服务远端
  return sendto(socket_fd, send_data, strlen(send_data), 0, (struct sockaddr *)&send_addr, addr_length);
}

int UDPClient_ReceiveData(const char *server_ip, unsigned short server_port, char *recvBuf){
  if(socket_fd == -1){ //如果没有socket，就创建
    socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
  }

  memset(recvBuf, 0, sizeof(recvBuf));
  //服务器的地址信息
  struct sockaddr_in from_addr;
  
  from_addr.sin_family = AF_INET; // IPV4
  from_addr.sin_port = htons(server_port); // 远端服务器的端口号
  from_addr.sin_addr.s_addr = inet_addr(server_ip); // 远端服务器的IP地址
  socklen_t addr_length = sizeof(from_addr);

  //接收服务端返回的字符串
  return recvfrom(socket_fd, recvBuf, sizeof(recvBuf), 0, (struct sockaddr *)&from_addr, &addr_length);
}

void CloseUDPClient(void){
  closesocket(socket_fd);
  socket_fd = -1;
}

int UDPServer_Start(unsigned short server_port){
  int retval = 0;
  // 调用 socket() 创建套接字
  server_socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
  
  // 初始化本地地址信息，为绑定做准备
  struct sockaddr_in server_addr = {0};
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(server_port);
  server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

  // 调用 bind() 将 socket 和本地地址信息绑定
  retval = bind(server_socket_fd, (struct sockaddr *)&server_addr, sizeof(server_addr));
  if(retval < 0){
    osal_printk("UDPServer Start Failed!\r\n");
  }else{
    osal_printk("UDPServer Start bind to port %hu success!\r\n", server_port);
  }
  return retval;
}

char* UDPServer_ReceiveData(void){
  ssize_t retval = 0;
  
  socklen_t clientLen = sizeof(client_addr);
  
  retval = recvfrom(server_socket_fd, recvData, sizeof(recvData), 0, (struct sockaddr*)&client_addr, &clientLen);
  osal_printk("retval: %d\r\n", retval);
  recvData[retval] = '\0'; 

  return recvData;
}

int UDPServer_SendData(char *message){
  return sendto(server_socket_fd, message, strlen(message), 0, (struct sockaddr *)&client_addr, sizeof(client_addr));
}