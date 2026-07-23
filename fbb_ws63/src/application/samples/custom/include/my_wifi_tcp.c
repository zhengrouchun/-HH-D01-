#include "lwip/nettool/misc.h"
#include "lwip/netifapi.h"
#include "lwip/sockets.h"
#include "osal_debug.h"

#define TCP_BACKLOG 10

// static int server_socket_fd = -1;
// int client_socket_fd = -1;

int TCPServer_StartListening(unsigned short server_port){
  int server_socket_fd = -1;

  int retval = 0;
  //服务端地址信息
	struct sockaddr_in server_addr = {0};

  //创建socket
	if ((server_socket_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1){
		osal_printk("socket create is error.\r\n");
		retval = -1;
    return retval;
	}

	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	server_addr.sin_port = htons(server_port);

	// 调用bind函数绑定socket和地址
	if (bind(server_socket_fd, (struct sockaddr *)&server_addr, sizeof(struct sockaddr)) == -1){
		osal_printk("socket bind is error.\r\n");
		retval = -2;
    return retval;
	}

	// 调用listen函数监听(指定port监听)
	if (listen(server_socket_fd, TCP_BACKLOG) == -1){
		osal_printk("listen is error\r\n");
		retval = -3;
    return retval;
	}

	osal_printk("start accept...\n");
  return server_socket_fd;
}

// 返回 client_socket_fd
int TCPServer_AcceptClient(int server_socket_fd){
  
  int sin_size;
  
  struct sockaddr_in client_addr = {0};
  int client_socket_fd = -1;
  
  sin_size = sizeof(struct sockaddr_in);
  while((client_socket_fd = accept(server_socket_fd, (struct sockaddr *)&client_addr, (socklen_t *)&sin_size)) == -1);
  osal_printk("accept client %d addr: %s\r\n", client_socket_fd, inet_ntoa(client_addr.sin_addr));
  return client_socket_fd;
}

int TCP_ReceiveData(int client_socket_fd, char *recvData, unsigned int recvSize){
  if(client_socket_fd == -1){
    osal_printk("no client socket\r\n");
    return -1;
  }
  int retval = recv(client_socket_fd, recvData, recvSize, 0);
  if(retval == -1){
    osal_printk("recvdata error.\r\n");
  }else if(retval == 0){ // 客户端断开连接
    osal_printk("Connection closed by client %d, will close it.\r\n", client_socket_fd);
    closesocket(client_socket_fd);  // 必须显式关闭
    client_socket_fd = -1;
    osal_printk("client socket has been closed.\r\n");
  }else{
    osal_printk("receive from client %d: %s\r\n",client_socket_fd, recvData);
  }
  return client_socket_fd;
}

// 向客户端发送数据
int TCP_SendData(int client_socket_fd, char *message){
  if(client_socket_fd == -1){
    osal_printk("no client socket.\r\n");
    return -1;
  }

  int retval = 0;
  for(int i = 0; i<3; i++){
    retval = send(client_socket_fd, message, strlen(message) 
    
    , 0);
    if (retval == -1){
      osal_printk("send data to client %d error, try again.\r\n", client_socket_fd);
    }else{
      osal_printk("send to client %d is: %s \r\n", client_socket_fd, message);
      break;
    }
  }
  
  if (retval == -1){
    osal_printk("can't send data to client %d, will close it.\r\n", client_socket_fd);
    closesocket(client_socket_fd);  // 关闭客户端
    client_socket_fd = -1;
    osal_printk("client socket has been closed.\r\n");
  }
  return client_socket_fd;
}

// 客户端连接指定 ip 和端口的服务器
// 返回连接的 client_socket_fd
int TCPClient_ConnectToServer(const char *server_ip, unsigned short server_port){
  struct sockaddr_in server_addr = {0};
  int client_socket_fd = -1;
  //创建socket
  if ((client_socket_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1){
    osal_printk("client_socket is error.\r\n");
    return -1;
  }

  bzero(&server_addr, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = inet_addr(server_ip); // 指定服务器地址
	server_addr.sin_port = htons(server_port); // 指定服务器的访问端口

  int re = 0;
	//调用 connect 函数请求与服务器连接
  for(int i = 0; i < TCP_BACKLOG; i++){
    re = connect(client_socket_fd, (struct sockaddr *)&server_addr, sizeof(struct sockaddr));
    if(re == -1){
      osal_printk("connect remain %d.\r\n", i);
    }
    if(re == 0) break;
  }

  if (re == -1)
  {
    osal_printk("Failed to connect to the server\r\n");
    return -2;
  }
  osal_printk("Connection to server successful\r\n");
  return client_socket_fd;
}