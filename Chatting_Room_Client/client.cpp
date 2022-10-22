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
		//���ϵͳʱ��
		time_t now = time(0);
		string dt = (string)ctime(&now);
		dt = dt.substr(0, dt.length() - 1);
		if (strcmp(sendby, "server") == 0 && strcmp(recv_mess, "name") == 0)
		{
			sprintf(CLIENT_NAME, "%s", sendto);
			cout << "����û���Ϊ��"  << CLIENT_NAME << endl;
			continue;
		}
		else if (strcmp(sendby, "server") == 0 && strcmp(recv_mess, "connected") == 0)
		{
			cout << "[" << dt << "]" << "�Է�������" << endl;
			continue;
		}
		//�Է��Ͽ�����
		else if (strcmp(recv_mess, "exit") == 0)
		{
			cout << "[" << dt << "]" << sendby << "�ѶϿ����ӣ�" << endl;
		}
		else
			cout << "[" << dt << "]" << sendby << "˵�� " << recv_mess << endl;
	}
}

void Send_message(void* param)
{
	while (1)
	{
		SOCKET server_socket = *(SOCKET*)(param);
		char message[BUFFER_SIZE] = { 0 };
		cin.getline(message, BUFFER_SIZE); //����������Ϣ
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
		//�ع���������
		sprintf(sendby, "%s#%s#%s", sendby, sendto, message);
		send(server_socket, sendby, strlen(sendby), 0);
		//���ϵͳʱ��
		time_t now = time(0);
		string dt = (string)ctime(&now);
		dt = dt.substr(0, dt.length() - 1);
		//�˳���ǰ�Ự
		if (strcmp(message, "exit") == 0) 
		{
			cout << "[" << dt << "]  ���˳��Ự" << endl;
			delete[]sendto;
			return;
		}
		cout << "[" << dt << "]��˵�� " << message << endl;
	}
}

int main()
{
	WSADATA wsaData;
	WSAStartup(MAKEWORD(2, 2), &wsaData);
	//����Socket
	SOCKET client_socket = socket(AF_INET, SOCK_STREAM, 0);
	SOCKADDR_IN addrCli;
	addrCli.sin_family = AF_INET;
	addrCli.sin_addr.s_addr = inet_addr(ip);
	addrCli.sin_port = htons(6666);
	SOCKADDR_IN addrSrv;
	addrSrv.sin_family = AF_INET;
	addrSrv.sin_addr.S_un.S_addr = inet_addr(ip);
	addrSrv.sin_port = htons(6666);

	//�������ӷ�����
	while (1)
	{
		if (connect(client_socket, (SOCKADDR*)&addrSrv, sizeof(SOCKADDR)) == SOCKET_ERROR)
		{
			cout << "�������ӷ�������" << endl;
			//�ȴ�һ��ʱ��
			int i = 0;
			while (i < 1000) { i++; }
			continue;
		}
		else
		{
			cout << "���ӷ������ɹ���" << endl;
			break;
		}
	}
	_beginthread(Receive_message, 0, &client_socket);
	//���̷߳�����Ϣ
	Send_message(&client_socket);
	//�ر�Socket
	closesocket(client_socket);
	client_socket = INVALID_SOCKET;
	WSACleanup();
	return 0;
}