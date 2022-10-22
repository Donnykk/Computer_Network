#include<iostream>
#include<Winsock2.h>
#include<cstring>
#include<string>
#include<ctime>
#include "process.h"
#include "windows.h" 
#pragma comment(lib,"ws2_32.lib")

using namespace std;
const int BUFFER_SIZE = 1024;
char CLIENT_NAME[9];
const char ip[1024] = "10.136.16.198";

void Receive_message(void* param)
{
	while (1)
	{
		SOCKET server_socket = *(SOCKET*)(param);
		char recvbuf[BUFFER_SIZE] = {};
		recv(server_socket, recvbuf, BUFFER_SIZE, 0);
		if (strcmp(recvbuf, "") == 0)
		{
			continue;
		}
		char* sendby = strtok(recvbuf, "#");
		char* sendto = strtok(NULL, "#");
		char* recv_mess = strtok(NULL, "#"); 
		//获得系统时间
		time_t now = time(0);
		string dt = (string)ctime(&now);
		dt = dt.substr(0, dt.length() - 1);
		if (strcmp(sendby, "server") == 0 && strcmp(recv_mess, "name") == 0)
		{
			sprintf(CLIENT_NAME, "%s", sendto);
			cout << "你的用户名为："  << CLIENT_NAME << endl;
			continue;
		}
		else if (strcmp(sendby, "server") == 0 && strcmp(recv_mess, "connected") == 0)
		{
			cout << "[" << dt << "]" << "对方已连接" << endl;
			continue;
		}
		//对方断开连接
		else if (strcmp(recv_mess, "exit") == 0)
		{
			cout << "[" << dt << "]" << sendby << "已断开连接！" << endl;
		}
		else
			cout << "[" << dt << "]" << sendby << "说： " << recv_mess << endl;
	}
}

void Send_message(void* param)
{
	while (1)
	{
		SOCKET server_socket = *(SOCKET*)(param);
		char message[BUFFER_SIZE] = { 0 };
		cin.getline(message, BUFFER_SIZE); //输入聊天信息
		if (strcmp(message, "") == 0)
			continue;
		char sendby[BUFFER_SIZE] = { 0 };
		sprintf(sendby, "%s", CLIENT_NAME);
		char* sendto;
		if (strcmp(message, "exit") == 0)
			sendto = new char[7] {"server"};
		else if (strcmp(CLIENT_NAME, "client_A") == 0)
			sendto = new char[9] {"client_B"};
		else if (strcmp(CLIENT_NAME, "client_B") == 0)
			sendto = new char[9] {"client_A"};
		//重构传输数据
		sprintf(sendby, "%s#%s#%s", sendby, sendto, message);
		send(server_socket, sendby, strlen(sendby), 0);
		//获得系统时间
		time_t now = time(0);
		string dt = (string)ctime(&now);
		dt = dt.substr(0, dt.length() - 1);
		//退出当前会话
		if (strcmp(message, "exit") == 0) 
		{
			cout << "[" << dt << "]  已退出会话" << endl;
			delete[]sendto;
			return;
		}
		cout << "[" << dt << "]你说： " << message << endl;
	}
}

int main()
{
	WSADATA wsaData;
	WSAStartup(MAKEWORD(2, 2), &wsaData);
	//创建Socket
	SOCKET client_socket = socket(AF_INET, SOCK_STREAM, 0);
	SOCKADDR_IN addrCli;
	addrCli.sin_family = AF_INET;
	addrCli.sin_addr.s_addr = inet_addr(ip);
	addrCli.sin_port = htons(6666);
	SOCKADDR_IN addrSrv;
	addrSrv.sin_family = AF_INET;
	addrSrv.sin_addr.S_un.S_addr = inet_addr(ip);
	addrSrv.sin_port = htons(6666);

	//尝试连接服务器
	while (1)
	{
		if (connect(client_socket, (SOCKADDR*)&addrSrv, sizeof(SOCKADDR)) == SOCKET_ERROR)
		{
			cout << "尝试连接服务器！" << endl;
			//等待一段时间
			int i = 0;
			while (i < 1000) { i++; }
			continue;
		}
		else
		{
			cout << "连接服务器成功！" << endl;
			break;
		}
	}
	_beginthread(Receive_message, 0, &client_socket);
	//主线程发送信息
	Send_message(&client_socket);
	//关闭Socket
	closesocket(client_socket);
	client_socket = INVALID_SOCKET;
	WSACleanup();
	return 0;
}