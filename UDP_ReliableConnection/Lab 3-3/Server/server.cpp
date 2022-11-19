#include <iostream>
#include <WINSOCK2.h>
#include <time.h>
#include <fstream>
#pragma comment(lib, "ws2_32.lib")
using namespace std;

char name_buffer[20];
char data_buffer[200000000];

const int BUFFER_SIZE = 4096;
const unsigned char SYN = 0x1;
const unsigned char ACK = 0x2;
const unsigned char ACK_SYN = 0x3;
const unsigned char FIN = 0x4;
const unsigned char FIN_ACK = 0x5;
const unsigned char END = 0x7;
double MAX_WAIT_TIME = 500;

struct HEADER
{
    unsigned short sum = 0;
    unsigned short datasize = 0;
    unsigned char flag = 0;
    unsigned char SEQ = 0;
};

unsigned short check_sum(unsigned short *message, int size)
{
    int count = (size + 1) / 2;
    unsigned short *buf = (unsigned short *)malloc(size + 1);
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

int tryConnect(SOCKET &socketServer, SOCKADDR_IN &client_addr, int &len)
{
    HEADER header;
    char *buffer = new char[sizeof(header)];
    //接收第一次握手请求
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
    //发送第二次握手请求
    header.flag = ACK;
    header.sum = 0;
    unsigned short temp = check_sum((unsigned short *)&header, sizeof(header));
    header.sum = temp;
    memcpy(buffer, &header, sizeof(header));
    if (sendto(socketServer, buffer, sizeof(header), 0, (sockaddr *)&client_addr, len) == SOCKET_ERROR)
    {
        return SOCKET_ERROR;
    }
    cout << "已发送第二次握手请求" << endl;
    clock_t start = clock();
    //接收第三次握手
    while (recvfrom(socketServer, buffer, sizeof(header), 0, (sockaddr *)&client_addr, &len) <= 0)
    {
        if (clock() - start > MAX_WAIT_TIME)
        {
            header.flag = ACK;
            header.sum = 0;
            unsigned short temp = check_sum((unsigned short *)&header, sizeof(header));
            header.flag = temp;
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

int recv_message(SOCKET &socketServer, SOCKADDR_IN &client_addr, int &len, char *message)
{
    long int all = 0;
    HEADER header;
    char *buffer = new char[BUFFER_SIZE + sizeof(header)];
    int seq = 0;
    int index = 0;
    while (true)
    {
        int length = recvfrom(socketServer, buffer, sizeof(header) + BUFFER_SIZE, 0, (sockaddr *)&client_addr, &len);
        if (length == SOCKET_ERROR)
        {
            return SOCKET_ERROR;
        }
        memcpy(&header, buffer, sizeof(header));
        if (header.flag == END && check_sum((unsigned short *)&header, sizeof(header)) == 0)
        {
            cout << "文件已接收" << endl;
            break;
        }
        if (header.flag == unsigned char(0) && check_sum((unsigned short *)buffer, length - sizeof(header)))
        {
            //判断序列号是否符合
            if (seq != int(header.SEQ))
            {
                header.flag = ACK;
                header.datasize = 0;
                header.SEQ = (unsigned char)seq;
                header.sum = 0;
                unsigned short temp = check_sum((unsigned short *)&header, sizeof(header));
                header.sum = temp;
                memcpy(buffer, &header, sizeof(header));
                //重发该包的ACK
                if (sendto(socketServer, buffer, sizeof(header), 0, (sockaddr *)&client_addr, len) == SOCKET_ERROR)
                {
                    return SOCKET_ERROR;
                }
                cout << "向客户端重传ACK，SEQ:" << (int)header.SEQ << endl;
                continue; //丢弃该数据包
            }
            seq = int(header.SEQ);
            if (seq > 255)
            {
                seq = seq - 256;
            }
            cout << "收到数据 " << length - sizeof(header) << " bytes! Flag:" << int(header.flag) << " SUM:" << int(header.sum) << " SEQ : " << int(header.SEQ) << endl;
            char *temp = new char[length - sizeof(header)];
            memcpy(temp, buffer + sizeof(header), length - sizeof(header));
            memcpy(message + all, temp, length - sizeof(header));
            all += int(header.datasize);
            //返回ACK
            header.flag = ACK;
            header.datasize = 0;
            header.SEQ = (unsigned char)seq;
            header.sum = 0;
            unsigned short temp_ = check_sum((unsigned short *)&header, sizeof(header));
            header.sum = temp_;
            memcpy(buffer, &header, sizeof(header));
            if (sendto(socketServer, buffer, sizeof(header), 0, (sockaddr *)&client_addr, len) == SOCKET_ERROR)
            {
                return SOCKET_ERROR;
            }
            cout << "已发送ACK:" << (int)header.SEQ << " SEQ:" << (int)header.SEQ << endl;
            seq++;
            if (seq > 255)
            {
                seq = seq - 256;
            }
        }
    }
    header.flag = END;
    header.sum = 0;
    unsigned short temp = check_sum((unsigned short *)&header, sizeof(header));
    header.sum = temp;
    memcpy(buffer, &header, sizeof(header));
    if (sendto(socketServer, buffer, sizeof(header), 0, (sockaddr *)&client_addr, len) == SOCKET_ERROR)
    {
        return SOCKET_ERROR;
    }
    return all;
}

int disConnect(SOCKET &socketServer, SOCKADDR_IN &client_addr, int &len)
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
    //发送第二次挥手信息
    header.flag = ACK;
    header.sum = 0;
    unsigned short temp = check_sum((unsigned short *)&header, sizeof(header));
    header.sum = temp;
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
            unsigned short temp = check_sum((unsigned short *)&header, sizeof(header));
            header.flag = temp;
            memcpy(buffer, &header, sizeof(header));
            if (sendto(socketServer, buffer, sizeof(header), 0, (sockaddr *)&client_addr, len) == -1)
            {
                return -1;
            }
            cout << "第二次挥手超时，开始重传..." << endl;
        }
    }
    HEADER temp_header;
    memcpy(&temp_header, buffer, sizeof(header));
    if (temp_header.flag == FIN_ACK && check_sum((unsigned short *)&temp_header, sizeof(temp_header) == 0))
    {
        cout << "收到第三次挥手请求" << endl;
    }
    else
    {
        return SOCKET_ERROR;
    }
    //发送第四次挥手请求
    header.flag = FIN_ACK;
    header.sum = 0;
    temp = check_sum((unsigned short *)&header, sizeof(header));
    header.sum = temp;
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
    SOCKADDR_IN server_addr;
    SOCKET server;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(6666);
    server_addr.sin_addr.s_addr = htonl(2130706433);
    server = socket(AF_INET, SOCK_DGRAM, 0);
    bind(server, (SOCKADDR *)&server_addr, sizeof(server_addr));
    cout << "开始监听......" << endl;
    int len = sizeof(server_addr);
    if (tryConnect(server, server_addr, len) == SOCKET_ERROR)
    {
        return 0;
    }
    int namelen = recv_message(server, server_addr, len, name_buffer);
    int datalen = recv_message(server, server_addr, len, data_buffer);
    string a;
    for (int i = 0; i < namelen; i++)
    {
        a = a + name_buffer[i];
    }
    ofstream fout(a.c_str(), ofstream::binary);
    for (int i = 0; i < datalen; i++)
    {
        fout << data_buffer[i];
    }
    fout.close();
    cout << "文件已成功下载到本地" << endl;
    disConnect(server, server_addr, len);
}