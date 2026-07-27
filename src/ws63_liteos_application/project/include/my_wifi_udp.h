#ifndef MY_WIFI_UDP_H__
#define MY_WIFI_UDP_H__

int UDPClient_SendData(const char *server_ip, unsigned short server_port, const char *send_data);
int UDPClient_ReceiveData(const char *server_ip, unsigned short server_port, char *recvBuf);
void CloseUDPClient(void);
int UDPServer_Start(unsigned short server_port);
char* UDPServer_ReceiveData(void);
int UDPServer_SendData(char *message);

#endif