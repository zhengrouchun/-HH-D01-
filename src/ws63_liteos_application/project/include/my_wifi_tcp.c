#include <string.h>

#include "lwip/nettool/misc.h"
#include "lwip/netifapi.h"
#include "lwip/sockets.h"
#include "osal_debug.h"


#define TCP_BACKLOG 10



/*
 * TCP服务器启动
 */
int TCPServer_StartListening(unsigned short server_port)
{

    int server_socket_fd = -1;


    struct sockaddr_in server_addr = {0};



    server_socket_fd = socket(AF_INET, SOCK_STREAM, 0);


    if(server_socket_fd == -1)
    {
        osal_printk("socket create error\r\n");
        return -1;
    }



    server_addr.sin_family = AF_INET;

    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    server_addr.sin_port = htons(server_port);



    if(bind(server_socket_fd,
            (struct sockaddr *)&server_addr,
            sizeof(struct sockaddr)) == -1)
    {
        osal_printk("socket bind error\r\n");

        return -2;
    }



    if(listen(server_socket_fd,TCP_BACKLOG)==-1)
    {
        osal_printk("listen error\r\n");

        return -3;
    }



    osal_printk("start accept...\r\n");


    return server_socket_fd;
}





/*
 * 等待客户端连接
 */
int TCPServer_AcceptClient(int server_socket_fd)
{

    struct sockaddr_in client_addr={0};


    int sin_size;

    int client_socket_fd=-1;



    sin_size=sizeof(struct sockaddr_in);



    client_socket_fd=
        accept(server_socket_fd,
              (struct sockaddr *)&client_addr,
              (socklen_t *)&sin_size);



    if(client_socket_fd==-1)
    {
        osal_printk("accept error\r\n");

        return -1;
    }



    osal_printk(
        "accept client %d addr:%s\r\n",
        client_socket_fd,
        inet_ntoa(client_addr.sin_addr));


    return client_socket_fd;

}






/*
 * 接收数据
 */
int TCP_ReceiveData(int client_socket_fd,
                    char *recvData,
                    unsigned int recvSize)
{


    if(client_socket_fd==-1)
    {
        osal_printk("no client socket\r\n");

        return -1;
    }



    int retval =
        recv(client_socket_fd,
             recvData,
             recvSize,
             0);



    if(retval<=0)
    {

        osal_printk("client disconnected\r\n");


        closesocket(client_socket_fd);


        return -1;

    }



    osal_printk(
        "receive:%s\r\n",
        recvData);



    return client_socket_fd;

}






/*
 * 发送数据
 */
int TCP_SendData(int client_socket_fd,
                 char *message)
{


    if(client_socket_fd==-1)
    {
        osal_printk("no client socket\r\n");

        return -1;
    }



    int retval;



    retval =
        send(client_socket_fd,
             message,
             strlen(message),
             0);



    if(retval==-1)
    {

        osal_printk("send error\r\n");


        closesocket(client_socket_fd);


        return -1;

    }



    osal_printk(
        "send:%s\r\n",
        message);



    return client_socket_fd;

}






/*
 * 关闭客户端
 */
void TCP_CloseClient(int client_socket_fd)
{

    if(client_socket_fd>=0)
    {
        closesocket(client_socket_fd);
    }

}






/*
 * TCP客户端连接服务器
 */
int TCPClient_ConnectToServer(
        const char *server_ip,
        unsigned short server_port)
{


    struct sockaddr_in server_addr={0};


    int client_socket_fd=-1;



    client_socket_fd=
        socket(AF_INET,
               SOCK_STREAM,
               0);



    if(client_socket_fd==-1)
    {

        osal_printk(
            "client socket error\r\n");

        return -1;

    }




    server_addr.sin_family=AF_INET;


    server_addr.sin_addr.s_addr=
        inet_addr(server_ip);


    server_addr.sin_port=
        htons(server_port);




    if(connect(client_socket_fd,
              (struct sockaddr *)&server_addr,
              sizeof(struct sockaddr))==-1)
    {

        osal_printk(
            "connect failed\r\n");


        closesocket(client_socket_fd);


        return -2;

    }



    osal_printk(
        "connect success\r\n");



    return client_socket_fd;

}