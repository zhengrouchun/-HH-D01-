#ifndef MY_WIFI_TCP_H__
#define MY_WIFI_TCP_H__

/*
 * 功能：连接 Wi-Fi 并启动 TCP Server 后，在指定端口监听
 * 参数：server_port -- 端口号
 * 返回值：为创建的 server_socket_fd 的值 
 */
int TCPServer_StartListening(unsigned short server_port);

/*
 * 功能：接收来自客户端的连接
 * 参数：server_socket_fd -- 服务器端建立的 socket 描述符
 * 返回值：连接到服务器端的客户端的socket fd 
 */
int TCPServer_AcceptClient(int server_socket_fd);

/*
 * 功能：接收消息
 * 参数：
 *     client_socket_fd --客户端socket描述符
 *     *recvData -- 存放接收消息的字符指针
 *     recvSize -- 接收到消息的字符长度
 * 返回值：当前通信的客户端socket描述符
 */
int TCP_ReceiveData(int client_socket_fd, char *recvData, unsigned int recvSize);

/*
 * 功能：发送消息
 * 参数：
 *     client_socket_fd --客户端socket描述符
 *     *message -- 待发送字符串指针
 * 返回值：当前通信的客户端socket描述符
 */
int TCP_SendData(int client_socket_fd, char *message);

/*
 * 功能：TCP客户端连接到指定 IP 和端口的服务器端
 * 参数：
 *     *server_ip --服务器端 IP 地址
 *     server_port -- 服务器端监听的端口号
 * 返回值：连接到服务器的客户端socket描述符
 */
// 客户端连接指定 ip 和端口的服务器
int TCPClient_ConnectToServer(const char *server_ip, unsigned short server_port);
#endif