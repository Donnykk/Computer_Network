#include <iostream>
#include <WINSOCK2.h>
#include <time.h>
#include <fstream>
#pragma comment(lib, "ws2_32.lib")
using namespace std;

char data_buffer[100000000];

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
    unsigned char sum = 0;
    unsigned char datasize = 0;
    unsigned char flag = 0;
    unsigned char SEQ = 0;
};

unsigned char check_sum(unsigned char *data, int size)
{
    int count = (size + 1) / 2;
    unsigned char *buf = (unsigned char *)malloc(size + 1);
    memset(buf, 0, size + 1);
    memcpy(buf, data, size);
    unsigned long sum = 0;
    while (count--)
    {
        sum += *buf++;
        if (sum & 0xff00)
        {
            sum &= 0x00ff;
            sum++;
        }
    }
    return ~(sum & 0x00ff);
}

int tryConnect(SOCKET &socketClient, SOCKADDR_IN &server_addr, int &len)
{
    HEADER header;
    char *buffer = new char[sizeof(header)];
    //发送第一次握手请求
    unsigned char sum;
    header.flag = SYN;
    header.sum = 0;
    unsigned char temp = check_sum((unsigned char *)&header, sizeof(header));
    header.sum = temp;
    //将数据包头部放入缓冲区
    memcpy(buffer, &header, sizeof(header));
    if (sendto(socketClient, buffer, sizeof(header), 0, (sockaddr *)&server_addr, len) == SOCKET_ERROR)
    {
        return SOCKET_ERROR;
    }
    cout << "已发送第一次握手请求" << endl;
    clock_t start = clock();
    //接收第二次握手
    while (recvfrom(socketClient, buffer, sizeof(header), 0, (sockaddr *)&server_addr, &len) <= 0)
    {
        if (clock() - start > MAX_WAIT_TIME)
        {
            header.flag = SYN;
            header.sum = 0;
            header.sum = check_sum((unsigned char *)&header, sizeof(header));
            memcpy(buffer, &header, sizeof(header));
            if (sendto(socketClient, buffer, sizeof(header), 0, (sockaddr *)&server_addr, len) == SOCKET_ERROR)
            {
                return SOCKET_ERROR;
            }
            start = clock();
            cout << "第一次握手超时，开始重传..." << endl;
        }
    }
    memcpy(&header, buffer, sizeof(header));
    if (header.flag == ACK && check_sum((unsigned char *)&header, sizeof(header) == 0))
    {
        cout << "收到第二次握手请求" << endl;
    }
    else
    {
        return SOCKET_ERROR;
    }
    //发送第三次握手请求
    header.flag = ACK_SYN;
    header.sum = 0;
    header.sum = check_sum((unsigned char *)&header, sizeof(header));
    if (sendto(socketClient, (char *)&header, sizeof(header), 0, (sockaddr *)&server_addr, len) == SOCKET_ERROR)
    {
        return SOCKET_ERROR;
    }
    cout << "已发送第三次握手请求" << endl;
    cout << "握手成功，服务器已连接！" << endl;
    return 1;
}

void send_package(SOCKET &socketClient, SOCKADDR_IN &server_addr, int &server_addr_len, char *message, int len, int &seq)
{
    HEADER header;
    char *buffer = new char[BUFFER_SIZE + sizeof(header)];
    header.datasize = len;
    header.SEQ = unsigned char(seq);
    memcpy(buffer, &header, sizeof(header));
    memcpy(buffer + sizeof(header), message, sizeof(header) + len);
    unsigned char check = check_sum((unsigned char *)buffer, sizeof(header) + len);
    header.sum = check;
    memcpy(buffer, &header, sizeof(header));
    if (sendto(socketClient, buffer, len + sizeof(header), 0, (sockaddr *)&server_addr, server_addr_len) == SOCKET_ERROR)
    {
        return;
    }
    cout << "发送数据 " << len << " bytes!"
         << " flag:" << int(header.flag) << " SUM:" << int(header.sum) << " SEQ:" << int(header.SEQ) << endl;
    clock_t start = clock();
    while (true)
    {
        unsigned long mode = 1;
        ioctlsocket(socketClient, FIONBIO, &mode);
        while (recvfrom(socketClient, buffer, BUFFER_SIZE, 0, (sockaddr *)&server_addr, &server_addr_len) <= 0)
        {
            if (clock() - start > MAX_WAIT_TIME)
            {
                header.datasize = len;
                header.SEQ = unsigned char(seq);
                header.flag = unsigned char(0x0);
                memcpy(buffer, &header, sizeof(header));
                memcpy(buffer + sizeof(header), message, sizeof(header) + len);
                unsigned char check = check_sum((unsigned char *)buffer, sizeof(header) + len);
                header.sum = check;
                memcpy(buffer, &header, sizeof(header));
                if (sendto(socketClient, buffer, len + sizeof(header), 0, (sockaddr *)&server_addr, server_addr_len) == SOCKET_ERROR)
                {
                    return;
                }
                cout << "超时，重传数据 " << len << " bytes! Flag:" << int(header.flag) << " SEQ:" << int(header.SEQ) << endl;
                clock_t start = clock();
            }
        }
        memcpy(&header, buffer, sizeof(header));
        unsigned char check = check_sum((unsigned char *)&header, sizeof(header));
        if (header.SEQ == unsigned char(seq) && header.flag == ACK)
        {
            cout << "数据已被接收! Flag:" << int(header.flag) << " SEQ:" << int(header.SEQ) << endl;
            break;
        }
        else
        {
            continue;
        }
    }
    unsigned long mode = 0;
    ioctlsocket(socketClient, FIONBIO, &mode); //改回阻塞模式
}

void send(SOCKET &socketClient, SOCKADDR_IN &server_addr, int &server_addr_len, char *message, int len)
{
    int pack_num = len / BUFFER_SIZE + (len % BUFFER_SIZE != 0);
    int seq = 0;
    for (int i = 0; i < pack_num; i++)
    {
        send_package(socketClient, server_addr, server_addr_len, message + i * BUFFER_SIZE, i == pack_num - 1 ? len - (pack_num - 1) * BUFFER_SIZE : BUFFER_SIZE, seq);
        seq++;
        if (seq > 255)
        {
            seq = seq - 256;
        }
    }
    //发送结束信息
    HEADER header;
    char *buffer = new char[sizeof(header)];
    header.flag = END;
    header.sum = 0;
    unsigned char temp = check_sum((unsigned char *)&header, sizeof(header));
    header.sum = temp;
    memcpy(buffer, &header, sizeof(header));
    if (sendto(socketClient, buffer, sizeof(header), 0, (sockaddr *)&server_addr, server_addr_len) == SOCKET_ERROR)
    {
        return;
    }
    cout << "传输已完成" << endl;
    clock_t start = clock();
    while (true)
    {
        unsigned long mode = 1;
        ioctlsocket(socketClient, FIONBIO, &mode);
        while (recvfrom(socketClient, buffer, BUFFER_SIZE, 0, (sockaddr *)&server_addr, &server_addr_len) <= 0)
        {
            if (clock() - start > MAX_WAIT_TIME)
            {
                char *buffer = new char[sizeof(header)];
                header.flag = END;
                header.sum = 0;
                unsigned char temp = check_sum((unsigned char *)&header, sizeof(header));
                header.sum = temp;
                memcpy(buffer, &header, sizeof(header));
                if (sendto(socketClient, buffer, sizeof(header), 0, (sockaddr *)&server_addr, server_addr_len) == SOCKET_ERROR)
                {
                    return;
                }
                cout << "超时，重传传输完成信息..." << endl;
                start = clock();
            }
        }
        memcpy(&header, buffer, sizeof(header));
        unsigned char check = check_sum((unsigned char *)&header, sizeof(header));
        if (header.flag == END)
        {
            cout << "对方已成功接收文件!" << endl;
            break;
        }
        else
        {
            continue;
        }
    }
    unsigned long mode = 0;
    ioctlsocket(socketClient, FIONBIO, &mode);
}

int disConnect(SOCKET &socketClient, SOCKADDR_IN &server_addr, int &len)
{
    HEADER header;
    char *buffer = new char[sizeof(header)];
    unsigned char sum;
    //发送第一次挥手请求
    header.flag = FIN;
    header.sum = 0;
    unsigned char temp = check_sum((unsigned char *)&header, sizeof(header));
    header.sum = temp;
    memcpy(buffer, &header, sizeof(header));
    if (sendto(socketClient, buffer, sizeof(header), 0, (sockaddr *)&server_addr, len) == SOCKET_ERROR)
    {
        return SOCKET_ERROR;
    }
    cout << "发送第一次挥手请求" << endl;
    clock_t start = clock();
    unsigned long mode = 1;
    ioctlsocket(socketClient, FIONBIO, &mode);
    //接收第二次挥手请求
    while (recvfrom(socketClient, buffer, sizeof(header), 0, (sockaddr *)&server_addr, &len) <= 0)
    {
        if (clock() - start > MAX_WAIT_TIME)
        {
            header.flag = FIN;
            header.sum = 0;
            header.sum = check_sum((unsigned char *)&header, sizeof(header));
            memcpy(buffer, &header, sizeof(header));
            sendto(socketClient, buffer, sizeof(header), 0, (sockaddr *)&server_addr, len);
            start = clock();
            cout << "第一次挥手超时，开始重传..." << endl;
        }
    }
    memcpy(&header, buffer, sizeof(header));
    if (header.flag == ACK && check_sum((unsigned char *)&header, sizeof(header) == 0))
    {
        cout << "收到第二次挥手请求" << endl;
    }
    else
    {
        return SOCKET_ERROR;
    }
    //发送第三次挥手请求
    header.flag = FIN_ACK;
    header.sum = 0;
    header.sum = check_sum((unsigned char *)&header, sizeof(header));
    if (sendto(socketClient, (char *)&header, sizeof(header), 0, (sockaddr *)&server_addr, len) == SOCKET_ERROR)
    {
        return SOCKET_ERROR;
    }
    cout << "发送第三次挥手请求" << endl;
    start = clock();
    //接收第四次挥手请求
    while (recvfrom(socketClient, buffer, sizeof(header), 0, (sockaddr *)&server_addr, &len) <= 0)
    {
        if (clock() - start > MAX_WAIT_TIME)
        {
            header.flag = FIN;
            header.sum = 0;
            header.sum = check_sum((unsigned char *)&header, sizeof(header));
            memcpy(buffer, &header, sizeof(header));
            if (sendto(socketClient, buffer, sizeof(header), 0, (sockaddr *)&server_addr, len) == SOCKET_ERROR)
            {
                return SOCKET_ERROR;
            }
            start = clock();
            cout << "第四次握手超时，开始重传..." << endl;
        }
    }
    cout << "四次挥手成功！" << endl;
    return 1;
}

int main()
{
    WSADATA wsadata;
    WSAStartup(MAKEWORD(2, 2), &wsadata);
    SOCKADDR_IN server_addr;
    SOCKET server;
    server_addr.sin_family = AF_INET; //使用IPV4
    server_addr.sin_port = htons(6666);
    server_addr.sin_addr.s_addr = htonl(2130706433);
    server = socket(AF_INET, SOCK_DGRAM, 0);
    int len = sizeof(server_addr);
    if (tryConnect(server, server_addr, len) == SOCKET_ERROR)
    {
        return 0;
    }
    string filename;
    cout << "请输入传输文件：" << endl;
    cin >> filename;
    ifstream fin(filename.c_str(), ifstream::binary);
    int index = 0;
    unsigned char temp = fin.get();
    while (fin)
    {
        data_buffer[index++] = temp;
        temp = fin.get();
    }
    fin.close();
    send(server, server_addr, len, (char *)(filename.c_str()), filename.length());
    clock_t start = clock();
    send(server, server_addr, len, data_buffer, index);
    clock_t end = clock();
    cout << "传输总时间为:" << (end - start) / CLOCKS_PER_SEC << "s" << endl;
    cout << "吞吐率为:" << ((float)index) / ((end - start) / CLOCKS_PER_SEC) << "byte/s" << endl;
    disConnect(server, server_addr, len);
}