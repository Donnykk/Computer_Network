#include<iostream>
#include<Winsock2.h>
#include<cstring>
#include<ctime>
#include<string>
#include "process.h"
#include "windows.h" 
#pragma comment(lib,"ws2_32.lib")  

using namespace std;
const int BUFFER_SIZE = 1024;
const char ip[1024] = "10.136.16.198";
bool client_A = false;
bool client_B = false;
SOCKET client_A_socket = INVALID_SOCKET;
SOCKET client_B_socket = INVALID_SOCKET;

void Send_message(void* param, char* sendby, char* sendto, char* message)
{
	SOCKET client_socket = *(SOCKET*)(param);
	if (strcmp(sendby, "server") != 0 && strcmp(message, "exit") != 0)
		cout << sendby << "说：" << message << endl;
	char* message_ = new char[1024];
	sprintf(message_, "%s#%s#%s", sendby, sendto, message);
	send(client_socket, message_, strlen(message_), 0);
	delete[]message_;
}

void Receive_message(void* param)
{
	while (1)
	{
		SOCKET client_socket = *(SOCKET*)(param);
		char recvbuf[BUFFER_SIZE] = {};
		recv(client_socket, recvbuf, BUFFER_SIZE, 0);
		if (strcmp(recvbuf, "") == 0)
		{
			continue;
		}
		//获取系统时间
		time_t now = time(0);
		string dt = (string)ctime(&now);
		dt = dt.substr(0, dt.length() - 1);
		//分割字符串
		char* sendby = strtok(recvbuf, "#");
		char* sendto = strtok(NULL, "#");
		char* recv_mess = strtok(NULL, "#");
		
		if (strcmp(sendto, "server") == 0 && strcmp(recv_mess, "exit") == 0)
		{
			cout << "[" << dt << "]" << sendby << "已断开连接" << endl << endl;
			if (strcmp(sendby, "client_A") == 0)
			{
				char message[BUFFER_SIZE] = "exit";
				char sendto[9] = "client_B";
				Send_message(&client_B_socket, sendby, sendto, message);
				client_A = false;
			}
			else if (strcmp(sendby, "client_B") == 0)
			{
				char message[BUFFER_SIZE] = "exit";
				char sendto[9] = "client_A";
				Send_message(&client_A_socket, sendby, sendto, message);
				client_B = false;
			}
		}
		else
		{
			if (strcmp(sendby, "client_A") == 0)
			{
				char sendto[9] = "client_B";
				Send_message(&client_B_socket, sendby, sendto, recv_mess);
			}
			else if (strcmp(sendby, "client_B") == 0)
			{
				char sendto[9] = "client_A";
				Send_message(&client_A_socket, sendby, sendto, recv_mess);
			}
		}
	}
}

int main()
{
	WSADATA wsaData;
	WSAStartup(MAKEWORD(2, 2), &wsaData);

	SOCKET server_socket;
	if ((server_socket = socket(AF_INET, SOCK_STREAM, 0)) == SOCKET_ERROR)
	{
		cout << "套接字创建失败！" << endl;
		return 0;
	}

	SOCKADDR_IN addrSrv;
	addrSrv.sin_family = AF_INET;
	addrSrv.sin_addr.S_un.S_addr = inet_addr(ip);
	addrSrv.sin_port = htons(6666);

	//绑定套接字
	if (bind(server_socket, (SOCKADDR*)&addrSrv, sizeof(addrSrv)) == SOCKET_ERROR)
	{
		cout << "套接字绑定失败！" << endl;
		return 0;
	}

	//服务器监听	
	if (listen(server_socket, SOMAXCONN) == SOCKET_ERROR)
	{
		cout << "监听失败！" << endl;
		return 0;
	}
	else
		cout << "服务器正在监听：" << endl;

	SOCKADDR_IN addrCli;
	int len = sizeof(addrCli);
	//监听服务器连接
	while (1)
	{
		if (client_A == false && (client_A_socket = accept(server_socket, (sockaddr*)&addrCli, &len)) == INVALID_SOCKET)
		{
			cout << "等待用户A连接！" << endl;
			Sleep(1000);
			continue;
		}
		else if (client_A == false)
		{
			cout << "用户A连接成功！" << endl;
			client_A = true;
			char sendby[7] = "server";
			char sendto[9] = "client_A";
			char message[5] = "name";
			Send_message(&client_A_socket, sendby, sendto, message);
			if (client_B_socket != INVALID_SOCKET) 
			{
				char sendby_[7] = "server";
				char sendto_[9] = "client_B";
				char message_[10] = "connected";
				Sleep(100);
				Send_message(&client_A_socket, sendby_, sendto_, message_);
			}
			_beginthread(Receive_message, 0, &client_A_socket);
		}
		else if (client_A == true && client_B == false && (client_B_socket = accept(server_socket, (sockaddr*)&addrCli, &len)) == INVALID_SOCKET)
		{
			cout << "等待用户B连接！" << endl;
			Sleep(1000);
			continue;
		}
		else if (client_A == true && client_B == false)
		{
			cout << "用户B连接成功！" << endl;
			client_B = true;
			char sendby[7] = "server";
			char sendto[9] = "client_B";
			char message[5] = "name";
			char sendby_[7] = "server";
			char sendto_[9] = "client_A";
			char message_[10] = "connected";
			Send_message(&client_B_socket, sendby, sendto, message);
			Sleep(100);
			Send_message(&client_B_socket, sendby, sendto, message_);
			Sleep(100);
			Send_message(&client_A_socket, sendby_, sendto_, message_);
			_beginthread(Receive_message, 0, &client_B_socket);
		}
		else 
		{
			Sleep(10000);
		}
	}
	//关闭socket
	closesocket(client_A_socket);
	closesocket(client_B_socket);
	closesocket(server_socket);
	WSACleanup();
	return 0;
}
