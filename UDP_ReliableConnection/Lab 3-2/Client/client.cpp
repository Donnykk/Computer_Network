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

char data_buffer[20000000];
const int BUFFER_SIZE = 4096;
const int MAX_WINDOW = 8;
const double MAX_WAIT_TIME = 500;

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

int Shake_hand(SOCKET &socketClient, SOCKADDR_IN &server_addr, int &server_addr_len)
{
    HEADER header;
    char *buffer = new char[sizeof(header)];
    unsigned short sum;
    //第一次握手请求
    header.flag = SYN;
    header.sum = 0;
    header.sum = check_sum((unsigned short *)&header, sizeof(header));
    memcpy(buffer, &header, sizeof(header));
    if (sendto(socketClient, buffer, sizeof(header), 0, (sockaddr *)&server_addr, server_addr_len) == SOCKET_ERROR)
    {
        return SOCKET_ERROR;
    }
    cout << "发送第一次握手请求" << endl;
    clock_t start = clock();
    unsigned long mode = 1;
    ioctlsocket(socketClient, FIONBIO, &mode);
    //接收第二次握手请求
    while (recvfrom(socketClient, buffer, sizeof(header), 0, (sockaddr *)&server_addr, &server_addr_len) <= 0)
    {
        if (clock() - start > MAX_WAIT_TIME) //超时，重新传输第一次握手
        {
            header.flag = SYN;
            header.sum = 0;
            header.sum = check_sum((unsigned short *)&header, sizeof(header));
            memcpy(buffer, &header, sizeof(header));
            if (sendto(socketClient, buffer, sizeof(header), 0, (sockaddr *)&server_addr, server_addr_len) == SOCKET_ERROR)
            {
                return SOCKET_ERROR;
            }
            start = clock();
            cout << "第一次握手超时，开始重传..." << endl;
        }
    }
    memcpy(&header, buffer, sizeof(header));
    if (header.flag == ACK && check_sum((unsigned short *)&header, sizeof(header) == 0))
    {
        cout << "收到第二次握手请求" << endl;
    }
    else
    {
        return SOCKET_ERROR;
    }
    //第三次握手请求
    header.flag = ACK_SYN;
    header.sum = 0;
    header.sum = check_sum((unsigned short *)&header, sizeof(header));
    if (sendto(socketClient, (char *)&header, sizeof(header), 0, (sockaddr *)&server_addr, server_addr_len) == SOCKET_ERROR)
    {
        return SOCKET_ERROR;
    }
    cout << "发送第三次握手请求" << endl;
    cout << "服务器成功连接！" << endl;
    return 1;
}

void Send_Message(SOCKET &socketClient, SOCKADDR_IN &server_addr, int &server_addr_len, char *message, int len)
{
    int package_num = len / BUFFER_SIZE + (len % BUFFER_SIZE != 0); //数据包数量
    HEADER header;
    char *buffer = new char[sizeof(header)];
    int first_pos = -1, last_pos = 0;
    clock_t start;
    while (first_pos < package_num - 1)
    {
        int pack_len = 0;
        if (last_pos != package_num && last_pos - first_pos < MAX_WINDOW)
        {
            //若窗口未满，直接发送数据包即可
            HEADER header;
            char *buffer = new char[BUFFER_SIZE + sizeof(header)];
            if (last_pos == package_num - 1)
                pack_len = len - (package_num - 1) * BUFFER_SIZE;
            else
                pack_len = BUFFER_SIZE;
            header.SEQ = unsigned char(last_pos % 256);
            header.datasize = pack_len;
            memcpy(buffer, &header, sizeof(header));
            memcpy(buffer + sizeof(header), message + last_pos * BUFFER_SIZE, pack_len);
            header.sum = check_sum((unsigned short *)buffer, sizeof(header) + pack_len);
            memcpy(buffer, &header, sizeof(header));
            sendto(socketClient, buffer, pack_len + sizeof(header), 0, (sockaddr *)&server_addr, server_addr_len);
            cout << "发送信息 " << pack_len << " bytes! SEQ:" << int(header.SEQ) << endl;
            start = clock();
            last_pos++;
        }
        //变为非阻塞模式
        unsigned long mode = 1;
        ioctlsocket(socketClient, FIONBIO, &mode);
        if (recvfrom(socketClient, buffer, sizeof(header), 0, (sockaddr *)&server_addr, &server_addr_len) > 0)
        {
            memcpy(&header, buffer, sizeof(header));
            if (check_sum((unsigned short *)&header, sizeof(header)) == 0)
            {
                unsigned char temp = header.SEQ - first_pos % 256;
                if (temp > 0)
                {
                    // ACK对应序列号有效
                    first_pos += temp;
                }
                else if (header.SEQ < MAX_WINDOW && first_pos % 256 + MAX_WINDOW >= 256)
                {
                    //窗口横跨两个256序列号组
                    temp += temp + 256;
                    first_pos += temp;
                }
                else
                    continue; //忽略重复ACK
                cout << "发送已被确认! SEQ:" << int(header.SEQ) << endl;
                cout << "窗口：" << first_pos << "~" << first_pos + MAX_WINDOW << endl;
                continue;
            }
            else
            {
                //校验和出错，丢弃未确认数据包
                last_pos = first_pos + 1;
                cout << "ERROR！已丢弃未确认数据包" << endl;
                continue;
            }
        }
        else
        {
            if (clock() - start > MAX_WAIT_TIME)
            {
                last_pos = first_pos + 1;
                cout << "确认超时，开始重传...";
            }
        }
        mode = 0;
        ioctlsocket(socketClient, FIONBIO, &mode);
    }
    //发送结束信息
    header.flag = END;
    header.sum = 0;
    header.sum = check_sum((unsigned short *)&header, sizeof(header));
    memcpy(buffer, &header, sizeof(header));
    sendto(socketClient, buffer, sizeof(header), 0, (sockaddr *)&server_addr, server_addr_len);
    cout << "已发送结束请求!" << endl;
    clock_t start_ = clock();
    while (true)
    {
        unsigned long mode = 1;
        ioctlsocket(socketClient, FIONBIO, &mode);
        while (recvfrom(socketClient, buffer, BUFFER_SIZE, 0, (sockaddr *)&server_addr, &server_addr_len) <= 0)
        {
            clock_t present = clock();
            if (present - start_ > MAX_WAIT_TIME)
            {
                char *buffer = new char[sizeof(header)];
                header.flag = END;
                header.sum = 0;
                header.sum = check_sum((unsigned short *)&header, sizeof(header));
                memcpy(buffer, &header, sizeof(header));
                sendto(socketClient, buffer, sizeof(header), 0, (sockaddr *)&server_addr, server_addr_len);
                cout << "发送超时! 开始重传..." << endl;
                start_ = clock();
            }
        }
        memcpy(&header, buffer, sizeof(header));
        if (check_sum((unsigned short *)&header, sizeof(header)) == 0)
        {
            //校验和正确
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
        else
        {
            //校验失败，重传
            header.flag = END;
            header.sum = 0;
            header.sum = check_sum((unsigned short *)&header, sizeof(header));
            memcpy(buffer, &header, sizeof(header));
            sendto(socketClient, buffer, sizeof(header), 0, (sockaddr *)&server_addr, server_addr_len);
            cout << "校验失败，已重新发送结束请求!" << endl;
            start_ = clock();
        }
    }
    unsigned long mode = 0;
    ioctlsocket(socketClient, FIONBIO, &mode);
}

/*
// GBN
//发送单个数据包
void SendPacket(SOCKET &socketClient, SOCKADDR_IN &servAddr, int &servAddrlen, char *message, int len, int order)
{
    HEADER header;
    char *buffer = new char[BUFFER_SIZE + sizeof(header)];
    header.datasize = len;
    header.SEQ = unsigned char(order); //序列号
    header.flag = unsigned char(0x0);
    memcpy(buffer, &header, sizeof(header));
    memcpy(buffer + sizeof(header), message, sizeof(header) + len);
    header.sum = check_sum((u_short *)buffer, sizeof(header) + len);
    memcpy(buffer, &header, sizeof(header));
    sendto(socketClient, buffer, len + sizeof(header), 0, (sockaddr *)&servAddr, servAddrlen);
    cout << "Send message:" << len << "bytes"
         << "\tFlag:" << int(header.flag) << "\tSEQ:" << int(header.SEQ) << "\tSum:" << int(header.sum) << endl;
}

//发送整个文件
void Send_Message(SOCKET &socketClient, SOCKADDR_IN &servAddr, int &servAddrlen, char *message, int len)
{
    int GBN_Head = -1; //滑动窗口头部
    int GBN_Tail = 0;  //滑动窗口尾部
    int PacketNum;
    if (len % BUFFER_SIZE == 0)
    {
        PacketNum = len / BUFFER_SIZE;
    }
    else
    {
        PacketNum = len / BUFFER_SIZE + 1;
    }
    HEADER header;
    char *buffer = new char[sizeof(header)];
    clock_t start;
    while (GBN_Head < PacketNum - 1) //没有发送结束
    {
        if (GBN_Tail - GBN_Head < MAX_WINDOW && GBN_Tail != PacketNum)
        {
            int templen;
            if (PacketNum == GBN_Tail + 1)
            {
                templen = len - GBN_Tail * BUFFER_SIZE;
            }
            else
            {
                templen = BUFFER_SIZE;
            }
            SendPacket(socketClient, servAddr, servAddrlen, message + GBN_Tail * BUFFER_SIZE, templen, (GBN_Tail % 256));
            GBN_Tail++;
            start = clock();
        }
        u_long mode = 1;
        ioctlsocket(socketClient, FIONBIO, &mode);
        if (recvfrom(socketClient, buffer, sizeof(header), 0, (sockaddr *)&servAddr, &servAddrlen) > 0) //缓冲区接收到信息
        {
            memcpy(&header, buffer, sizeof(header)); //读取缓冲区
            if ((int(check_sum((u_short *)&header, sizeof(header))) != 0) || header.flag != ACK)
            {
                GBN_Tail = GBN_Head + 1;
                cout << "GBN重传" << endl;
                continue;
            }
            else
            {
                if (int(header.SEQ) >= GBN_Head % 256)
                {
                    GBN_Head = GBN_Head + int(header.SEQ) - GBN_Head % 256;
                    cout << "Send message(GBN) Confirmed:"
                         << "\tFlag:" << int(header.flag) << "\tSEQ:" << int(header.SEQ) << endl;
                }
                else if ((GBN_Head % 256 > 256 - MAX_WINDOW - 1) && (int(header.SEQ) < MAX_WINDOW))
                {
                    GBN_Head = GBN_Head + 256 - GBN_Head % 256 + int(header.SEQ);
                    cout << "Send message(GBN) Confirmed:"
                         << "\tFlag:" << int(header.flag) << "\tSEQ:" << int(header.SEQ) << endl;
                }
            }
        }
        else
        {
            if (clock() - start > MAX_WAIT_TIME)
            {
                GBN_Tail = GBN_Head + 1;
                cout << "超时，GBN重传" << endl;
            }
        }
        mode = 0;
        ioctlsocket(socketClient, FIONBIO, &mode);
    }

    //发送结束信息
    header.flag = END;
    header.sum = 0;
    header.sum = check_sum((u_short *)&header, sizeof(header));
    memcpy(buffer, &header, sizeof(header));
    sendto(socketClient, buffer, sizeof(header), 0, (sockaddr *)&servAddr, servAddrlen);
    cout << "发送结束" << endl;
    start = clock();
    while (1)
    {
        u_long mode = 1;
        ioctlsocket(socketClient, FIONBIO, &mode);
        while (recvfrom(socketClient, buffer, BUFFER_SIZE, 0, (sockaddr *)&servAddr, &servAddrlen) <= 0)
        {
            if (clock() - start > MAX_WAIT_TIME)
            {
                char *buffer = new char[sizeof(header)];
                header.flag = END;
                header.sum = 0;
                header.sum = check_sum((u_short *)&header, sizeof(header));
                memcpy(buffer, &header, sizeof(header));
                sendto(socketClient, buffer, sizeof(header), 0, (sockaddr *)&servAddr, servAddrlen);
                cout << "结束超时，正在重传" << endl;
                start = clock();
            }
        }
        memcpy(&header, buffer, sizeof(header));
        if (header.flag == END)
        {
            cout << "发送成功，对方已成功接收文件" << endl;
            break;
        }
    }
    u_long mode = 0;
    ioctlsocket(socketClient, FIONBIO, &mode);
}
*/

int Wave_hand(SOCKET &socketClient, SOCKADDR_IN &server_addr, int &server_addr_len)
{
    HEADER header;
    char *buffer = new char[sizeof(header)];
    unsigned short sum;
    //第一次握手请求
    header.flag = FIN;
    header.sum = 0;
    header.sum = check_sum((unsigned short *)&header, sizeof(header));
    memcpy(buffer, &header, sizeof(header));
    if (sendto(socketClient, buffer, sizeof(header), 0, (sockaddr *)&server_addr, server_addr_len) == SOCKET_ERROR)
    {
        return SOCKET_ERROR;
    }
    cout << "发送第一次挥手请求" << endl;
    clock_t start = clock();
    unsigned long mode = 1;
    ioctlsocket(socketClient, FIONBIO, &mode);
    //接收第二次挥手
    while (recvfrom(socketClient, buffer, sizeof(header), 0, (sockaddr *)&server_addr, &server_addr_len) <= 0)
    {
        if (clock() - start > MAX_WAIT_TIME) //超时，重新传输第一次挥手
        {
            header.flag = FIN;
            header.sum = 0;
            header.sum = check_sum((unsigned short *)&header, sizeof(header));
            memcpy(buffer, &header, sizeof(header));
            sendto(socketClient, buffer, sizeof(header), 0, (sockaddr *)&server_addr, server_addr_len);
            start = clock();
            cout << "第一次挥手超时，开始重传..." << endl;
        }
    }
    memcpy(&header, buffer, sizeof(header));
    if (header.flag == ACK && check_sum((unsigned short *)&header, sizeof(header) == 0))
    {
        cout << "收到第二次挥手请求" << endl;
    }
    else
    {
        return SOCKET_ERROR;
    }
    //进行第三次挥手
    header.flag = FIN_ACK;
    header.sum = 0;
    header.sum = check_sum((unsigned short *)&header, sizeof(header));
    if (sendto(socketClient, (char *)&header, sizeof(header), 0, (sockaddr *)&server_addr, server_addr_len) == SOCKET_ERROR)
    {
        return SOCKET_ERROR;
    }
    cout << "发送第三次挥手请求" << endl;
    start = clock();
    //接收第四次挥手请求
    while (recvfrom(socketClient, buffer, sizeof(header), 0, (sockaddr *)&server_addr, &server_addr_len) <= 0)
    {
        if (clock() - start > MAX_WAIT_TIME)
        {
            header.flag = FIN;
            header.sum = 0;
            header.sum = check_sum((unsigned short *)&header, sizeof(header));
            memcpy(buffer, &header, sizeof(header));
            if (sendto(socketClient, buffer, sizeof(header), 0, (sockaddr *)&server_addr, server_addr_len) == SOCKET_ERROR)
            {
                return SOCKET_ERROR;
            }
            start = clock();
            cout << "第三次挥手超时，开始重传..." << endl;
        }
    }
    cout << "四次挥手结束！" << endl;
    return 1;
}

int main()
{
    WSADATA wsadata;
    WSAStartup(MAKEWORD(2, 2), &wsadata);
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(6666);
    server_addr.sin_addr.s_addr = htonl(2130706433);
    server = socket(AF_INET, SOCK_DGRAM, 0);
    int len = sizeof(server_addr);
    //建立连接
    if (Shake_hand(server, server_addr, len) == SOCKET_ERROR)
    {
        return 0;
    }
    string filename;
    cout << "请输入文件名称" << endl;
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
    Send_Message(server, server_addr, len, (char *)(filename.c_str()), filename.length());
    clock_t start = clock();
    Send_Message(server, server_addr, len, data_buffer, index);
    clock_t end = clock();
    cout << "传输总时间为:" << (end - start) / CLOCKS_PER_SEC << "s" << endl;
    cout << "吞吐率为:" << ((float)index) / ((end - start) / CLOCKS_PER_SEC) << "byte/s" << endl;
    Wave_hand(server, server_addr, len);
}