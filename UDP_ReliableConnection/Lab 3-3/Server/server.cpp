#include <iostream>
#include <WINSOCK2.h>
#include <time.h>
#include <fstream>
#pragma comment(lib, "ws2_32.lib")
using namespace std;

#define SYN 0x1
#define ACK 0x2
#define ACK_SYN 0x3
#define FIN 0x4
#define FIN_ACK 0x5
#define END 0x7

SOCKADDR_IN server_addr;
SOCKET server;

char data_buffer[200000000];
const int BUFFER_SIZE = 4096;
double MAX_WAIT_TIME = 500;

struct HEADER
{
    unsigned short sum = 0;
    unsigned short datasize = 0;
    unsigned short flag = 0;
    unsigned short SEQ = 0;
};

unsigned short check_sum(unsigned short *message, int size)
{
    int count = (size + 1) / 2;
    unsigned short *buf = new unsigned short[size + 1];
    memset(buf, 0, size + 1);
    memcpy(buf, message, size);
    unsigned long sum = 0;
    while (count--)
    {
        sum += *buf++;
        if (sum & 0xffff0000)
        {
            sum &= 0xffff;
            sum++;
        }
    }
    return ~(sum & 0xffff);
}

int Shake_hand(SOCKET &socketServer, SOCKADDR_IN &client_addr, int &len)
{
    HEADER header;
    char *buffer = new char[sizeof(header)];
    // 接收第一次握手请求
    while (true)
    {
        if (recvfrom(socketServer, buffer, sizeof(header), 0, (sockaddr *)&client_addr, &len) == SOCKET_ERROR)
        {
            return SOCKET_ERROR;
        }
        memcpy(&header, buffer, sizeof(header));
        if (header.flag == SYN && check_sum((unsigned short *)&header, sizeof(header)) == 0)
        {
            cout << "收到第一次握手请求" << endl;
            break;
        }
    }
    // 发送第二次握手请求
    header.flag = ACK;
    header.sum = 0;
    header.sum = check_sum((unsigned short *)&header, sizeof(header));
    memcpy(buffer, &header, sizeof(header));
    if (sendto(socketServer, buffer, sizeof(header), 0, (sockaddr *)&client_addr, len) == SOCKET_ERROR)
    {
        return SOCKET_ERROR;
    }
    cout << "已发送第二次握手请求" << endl;
    clock_t start = clock();
    // 接收第三次握手
    while (recvfrom(socketServer, buffer, sizeof(header), 0, (sockaddr *)&client_addr, &len) <= 0)
    {
        if (clock() - start > MAX_WAIT_TIME)
        {
            header.flag = ACK;
            header.sum = 0;
            header.sum = check_sum((unsigned short *)&header, sizeof(header));
            memcpy(buffer, &header, sizeof(header));
            if (sendto(socketServer, buffer, sizeof(header), 0, (sockaddr *)&client_addr, len) == SOCKET_ERROR)
            {
                return SOCKET_ERROR;
            }
            cout << "第二次握手超时，开始重传..." << endl;
        }
    }
    HEADER temp_header;
    memcpy(&temp_header, buffer, sizeof(header));
    if (temp_header.flag == ACK_SYN && check_sum((unsigned short *)&temp_header, sizeof(temp_header) == 0))
    {
        cout << "握手成功，客户端已连接" << endl;
    }
    else
    {
        return SOCKET_ERROR;
    }
    return 1;
}

int Receive_message(SOCKET &socketServer, SOCKADDR_IN &client_addr, int &client_addr_len, char *message)
{
    HEADER header;
    char *buffer = new char[BUFFER_SIZE + sizeof(header)];
    int seq = 0; // 序列号
    int len = 0; // 已读取长度
    while (true)
    {
        int mess_len = 0;
        if (mess_len = recvfrom(socketServer, buffer, sizeof(header) + BUFFER_SIZE, 0, (sockaddr *)&client_addr, &client_addr_len))
        {
            memcpy(&header, buffer, sizeof(header));
            if (header.flag == 0)
            {
                if (header.SEQ == seq)
                {
                    char *temp_buffer = new char[mess_len - sizeof(header)];
                    memcpy(temp_buffer, buffer + sizeof(header), mess_len - sizeof(header));
                    memcpy(message + len, temp_buffer, mess_len - sizeof(header));
                    len = len + int(header.datasize);
                    cout << "已接收数据 " << mess_len - sizeof(header) << " bytes! SEQ: " << int(header.SEQ) << endl;
                    // 发送ACK
                    header.flag = ACK;
                    header.datasize = 0;
                    header.SEQ = (unsigned short)seq;
                    header.sum = 0;
                    header.sum = check_sum((unsigned short *)&header, sizeof(header));
                    memcpy(buffer, &header, sizeof(header));
                    if (sendto(socketServer, buffer, sizeof(header), 0, (sockaddr *)&client_addr, client_addr_len) == SOCKET_ERROR)
                    {
                        return SOCKET_ERROR;
                    }
                    cout << "已发送ACK! SEQ:" << (int)header.SEQ << endl;
                    // 增加当前序列号
                    seq++;
                }
                else
                {
                    // 序列号不匹配
                    cout << "已接收数据 " << mess_len - sizeof(header) << " bytes! SEQ: " << int(header.SEQ) << endl;
                    header.flag = ACK;
                    header.datasize = 0;
                    header.SEQ = (unsigned short)(seq - 1);
                    header.sum = 0;
                    header.sum = check_sum((unsigned short *)&header, sizeof(header));
                    memcpy(buffer, &header, sizeof(header));
                    // 重发ACK
                    if (sendto(socketServer, buffer, sizeof(header), 0, (sockaddr *)&client_addr, client_addr_len) == SOCKET_ERROR)
                    {
                        return SOCKET_ERROR;
                    }
                    cout << "待接收序列号: " << seq << " 序列号无效，已重发ACK! SEQ:" << (int)header.SEQ << endl;
                    continue;
                }
            }
            else if (header.flag == END && check_sum((unsigned short *)&header, sizeof(header)) == 0)
            {
                cout << "文件已接收完毕！" << endl;
                break;
            }
        }
    }
    header.flag = END;
    header.sum = 0;
    header.sum = check_sum((unsigned short *)&header, sizeof(header));
    memcpy(buffer, &header, sizeof(header));
    if (sendto(socketServer, buffer, sizeof(header), 0, (sockaddr *)&client_addr, client_addr_len) == SOCKET_ERROR)
    {
        return SOCKET_ERROR;
    }
    return len;
}

int Wave_hand(SOCKET &socketServer, SOCKADDR_IN &client_addr, int &len)
{
    HEADER header;
    char *buffer = new char[sizeof(header)];
    while (true)
    {
        if (recvfrom(socketServer, buffer, sizeof(header) + BUFFER_SIZE, 0, (sockaddr *)&client_addr, &len) == SOCKET_ERROR)
        {
            return SOCKET_ERROR;
        }
        memcpy(&header, buffer, sizeof(header));
        if (header.flag == FIN && check_sum((unsigned short *)&header, sizeof(header)) == 0)
        {
            cout << "收到第一次挥手请求" << endl;
            break;
        }
    }
    // 发送第二次挥手信息
    header.flag = ACK;
    header.sum = 0;
    header.sum = check_sum((unsigned short *)&header, sizeof(header));
    memcpy(buffer, &header, sizeof(header));
    if (sendto(socketServer, buffer, sizeof(header), 0, (sockaddr *)&client_addr, len) == SOCKET_ERROR)
    {
        return SOCKET_ERROR;
    }
    cout << "发送第二次挥手请求" << endl;
    clock_t start = clock();
    while (recvfrom(socketServer, buffer, sizeof(header), 0, (sockaddr *)&client_addr, &len) <= 0)
    {
        if (clock() - start > MAX_WAIT_TIME)
        {
            header.flag = ACK;
            header.sum = 0;
            header.sum = check_sum((unsigned short *)&header, sizeof(header));
            memcpy(buffer, &header, sizeof(header));
            if (sendto(socketServer, buffer, sizeof(header), 0, (sockaddr *)&client_addr, len) == -1)
            {
                return -1;
            }
            cout << "第二次挥手超时，开始重传..." << endl;
        }
    }
    memcpy(&header, buffer, sizeof(header));
    if (header.flag == FIN_ACK && check_sum((unsigned short *)&header, sizeof(header) == 0))
    {
        cout << "收到第三次挥手请求" << endl;
    }
    else
    {
        return SOCKET_ERROR;
    }
    // 发送第四次挥手请求
    header.flag = FIN_ACK;
    header.sum = 0;
    header.sum = check_sum((unsigned short *)&header, sizeof(header));
    memcpy(buffer, &header, sizeof(header));
    if (sendto(socketServer, buffer, sizeof(header), 0, (sockaddr *)&client_addr, len) == SOCKET_ERROR)
    {
        return SOCKET_ERROR;
    }
    cout << "发送第四次挥手请求" << endl;
    cout << "四次挥手结束，连接断开！" << endl;
    return 1;
}

int main()
{
    WSADATA wsadata;
    WSAStartup(MAKEWORD(2, 2), &wsadata);
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(6667);
    server_addr.sin_addr.s_addr = htonl(2130706433);
    server = socket(AF_INET, SOCK_DGRAM, 0);
    bind(server, (SOCKADDR *)&server_addr, sizeof(server_addr));
    cout << "开始监听......" << endl;
    int len = sizeof(server_addr);
    if (Shake_hand(server, server_addr, len) == SOCKET_ERROR)
    {
        return 0;
    }
    int datalen = Receive_message(server, server_addr, len, data_buffer);
    string file_name = "";
    int i;
    for (i = 0; i < datalen; i++)
    {
        if (data_buffer[i] == 0)
        {
            i++;
            break;
        }
        file_name += data_buffer[i]; // 获得文件名
    }
    ofstream fout(file_name.c_str(), ofstream::binary);
    for (; i < datalen; i++)
    {
        fout << data_buffer[i];
    }
    fout.close();
    cout << "文件已成功下载到本地" << endl;
    Wave_hand(server, server_addr, len);
}