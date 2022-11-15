#include <iostream>
#include <WINSOCK2.h>
#include <time.h>
#include <fstream>
#pragma comment(lib, "ws2_32.lib")
using namespace std;

char data_buffer[100000000];

const int BUFFER_SIZE = 1024;
const unsigned char SYN = 0x1;
const unsigned char ACK = 0x2;
const unsigned char ACK_SYN = 0x3;
const unsigned char FIN = 0x4;
const unsigned char FIN_ACK = 0x5;
const unsigned char END = 0x7;
double MAX_TIME = 1000;

/*
1.把伪首部添加到UDP上；
2.计算初始时是需要将检验和字段添零的；
3.把所有位划分为16位（2字节）的字
4.把所有16位的字相加，如果遇到进位，则将高于16字节的进位部分的值加到最低位上，举例，0xBB5E+0xFCED=0x1 B84B，则将1放到最低位，得到结果是0xB84C
5.将所有字相加得到的结果应该为一个16位的数，将该数取反则可以得到检验和checksum。 */
u_short check_sum(u_short *message, int size)
{
    int count = (size + 1) / 2;
    u_short *buf = (u_short *)malloc(size + 1);
    memset(buf, 0, size + 1);
    memcpy(buf, message, size);
    u_long sum = 0;
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

struct HEADER
{
    u_short sum = 0;      //校验和 16位
    u_short datasize = 0; //所包含数据长度 16位
    unsigned char flag = 0;
    //八位，使用后四位，排列是FIN ACK SYN
    unsigned char SEQ = 0;
    //八位，传输的序列号，0~255，超过后mod
    HEADER()
    {
        sum = 0;      //校验和 16位
        datasize = 0; //所包含数据长度 16位
        flag = 0;
        //八位，使用后三位，排列是FIN ACK SYN
        SEQ = 0;
    }
};

int tryConnect(SOCKET &socketClient, SOCKADDR_IN &server_addr, int &len) //三次握手建立连接
{
    HEADER header;
    char *Buffer = new char[sizeof(header)];

    u_short sum;

    //进行第一次握手
    header.flag = SYN;
    header.sum = 0; //校验和置0
    u_short temp = check_sum((u_short *)&header, sizeof(header));
    header.sum = temp;                       //计算校验和
    memcpy(Buffer, &header, sizeof(header)); //将首部放入缓冲区
    if (sendto(socketClient, Buffer, sizeof(header), 0, (sockaddr *)&server_addr, len) == -1)
    {
        return -1;
    }
    clock_t start = clock(); //记录发送第一次握手时间

    u_long mode = 1;
    ioctlsocket(socketClient, FIONBIO, &mode);

    //接收第二次握手
    while (recvfrom(socketClient, Buffer, sizeof(header), 0, (sockaddr *)&server_addr, &len) <= 0)
    {
        if (clock() - start > MAX_TIME) //超时，重新传输第一次握手
        {
            header.flag = SYN;
            header.sum = 0;                                             //校验和置0
            header.sum = check_sum((u_short *)&header, sizeof(header)); //计算校验和
            memcpy(Buffer, &header, sizeof(header));                    //将首部放入缓冲区
            sendto(socketClient, Buffer, sizeof(header), 0, (sockaddr *)&server_addr, len);
            start = clock();
            cout << "第一次握手超时，正在进行重传" << endl;
        }
    }

    //进行校验和检验
    memcpy(&header, Buffer, sizeof(header));
    if (header.flag == ACK && check_sum((u_short *)&header, sizeof(header) == 0))
    {
        cout << "收到第二次握手信息" << endl;
    }
    else
    {
        cout << "连接发生错误，请重启客户端！" << endl;
        return -1;
    }

    //进行第三次握手
    header.flag = ACK_SYN;
    header.sum = 0;
    header.sum = check_sum((u_short *)&header, sizeof(header)); //计算校验和
    if (sendto(socketClient, (char *)&header, sizeof(header), 0, (sockaddr *)&server_addr, len) == -1)
    {
        return -1; //判断客户端是否打开，-1为未开启发送失败
    }
    cout << "服务器成功连接！可以发送数据" << endl;
    return 1;
}

void send_package(SOCKET &socketClient, SOCKADDR_IN &server_addr, int &server_addr_len, char *message, int len, int &order)
{
    HEADER header;
    char *buffer = new char[BUFFER_SIZE + sizeof(header)];
    header.datasize = len;
    header.SEQ = unsigned char(order); //序列号
    memcpy(buffer, &header, sizeof(header));
    memcpy(buffer + sizeof(header), message, sizeof(header) + len);
    u_short check = check_sum((u_short *)buffer, sizeof(header) + len); //计算校验和
    header.sum = check;
    memcpy(buffer, &header, sizeof(header));
    sendto(socketClient, buffer, len + sizeof(header), 0, (sockaddr *)&server_addr, server_addr_len); //发送
    cout << "Send message " << len << " bytes!"
         << " flag:" << int(header.flag) << " SEQ:" << int(header.SEQ) << " SUM:" << int(header.sum) << endl;
    clock_t start = clock(); //记录发送时间
    //接收ack等信息
    while (1 == 1)
    {
        u_long mode = 1;
        ioctlsocket(socketClient, FIONBIO, &mode);
        while (recvfrom(socketClient, buffer, BUFFER_SIZE, 0, (sockaddr *)&server_addr, &server_addr_len) <= 0)
        {
            if (clock() - start > MAX_TIME)
            {
                header.datasize = len;
                header.SEQ = u_char(order); //序列号
                header.flag = u_char(0x0);
                memcpy(buffer, &header, sizeof(header));
                memcpy(buffer + sizeof(header), message, sizeof(header) + len);
                u_short check = check_sum((u_short *)buffer, sizeof(header) + len); //计算校验和
                header.sum = check;
                memcpy(buffer, &header, sizeof(header));
                sendto(socketClient, buffer, len + sizeof(header), 0, (sockaddr *)&server_addr, server_addr_len); //发送
                cout << "TIME OUT! ReSend message " << len << " bytes! Flag:" << int(header.flag) << " SEQ:" << int(header.SEQ) << endl;
                clock_t start = clock(); //记录发送时间
            }
        }
        memcpy(&header, buffer, sizeof(header)); //缓冲区接收到信息，读取
        u_short check = check_sum((u_short *)&header, sizeof(header));
        if (header.SEQ == u_short(order) && header.flag == ACK)
        {
            cout << "Send has been confirmed! Flag:" << int(header.flag) << " SEQ:" << int(header.SEQ) << endl;
            break;
        }
        else
        {
            continue;
        }
    }
    u_long mode = 0;
    ioctlsocket(socketClient, FIONBIO, &mode); //改回阻塞模式
}

void send(SOCKET &socketClient, SOCKADDR_IN &server_addr, int &server_addr_len, char *message, int len)
{
    int packagenum = len / BUFFER_SIZE + (len % BUFFER_SIZE != 0);
    int seqnum = 0;
    for (int i = 0; i < packagenum; i++)
    {
        send_package(socketClient, server_addr, server_addr_len, message + i * BUFFER_SIZE, i == packagenum - 1 ? len - (packagenum - 1) * BUFFER_SIZE : BUFFER_SIZE, seqnum);
        seqnum++;
        if (seqnum > 255)
        {
            seqnum = seqnum - 256;
        }
    }
    //发送结束信息
end:
    HEADER header;
    char *Buffer = new char[sizeof(header)];
    header.flag = END;
    header.sum = 0;
    u_short temp = check_sum((u_short *)&header, sizeof(header));
    header.sum = temp;
    memcpy(Buffer, &header, sizeof(header));
    sendto(socketClient, Buffer, sizeof(header), 0, (sockaddr *)&server_addr, server_addr_len);
    cout << "Send End!" << endl;
    clock_t start = clock();
    while (1 == 1)
    {
        u_long mode = 1;
        ioctlsocket(socketClient, FIONBIO, &mode);
        while (recvfrom(socketClient, Buffer, BUFFER_SIZE, 0, (sockaddr *)&server_addr, &server_addr_len) <= 0)
        {
            if (clock() - start > MAX_TIME)
            {
                char *Buffer = new char[sizeof(header)];
                header.flag = END;
                header.sum = 0;
                u_short temp = check_sum((u_short *)&header, sizeof(header));
                header.sum = temp;
                memcpy(Buffer, &header, sizeof(header));
                sendto(socketClient, Buffer, sizeof(header), 0, (sockaddr *)&server_addr, server_addr_len);
                cout << "Time Out! ReSend End!" << endl;
                start = clock();
            }
        }
        memcpy(&header, Buffer, sizeof(header)); //缓冲区接收到信息，读取
        u_short check = check_sum((u_short *)&header, sizeof(header));
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
    u_long mode = 0;
    ioctlsocket(socketClient, FIONBIO, &mode); //改回阻塞模式
}

int disConnect(SOCKET &socketClient, SOCKADDR_IN &server_addr, int &len)
{
    HEADER header;
    char *Buffer = new char[sizeof(header)];

    u_short sum;

    //进行第一次握手
    header.flag = FIN;
    header.sum = 0; //校验和置0
    u_short temp = check_sum((u_short *)&header, sizeof(header));
    header.sum = temp;                       //计算校验和
    memcpy(Buffer, &header, sizeof(header)); //将首部放入缓冲区
    if (sendto(socketClient, Buffer, sizeof(header), 0, (sockaddr *)&server_addr, len) == -1)
    {
        return -1;
    }
    clock_t start = clock(); //记录发送第一次挥手时间

    u_long mode = 1;
    ioctlsocket(socketClient, FIONBIO, &mode);

    //接收第二次挥手
    while (recvfrom(socketClient, Buffer, sizeof(header), 0, (sockaddr *)&server_addr, &len) <= 0)
    {
        if (clock() - start > MAX_TIME) //超时，重新传输第一次挥手
        {
            header.flag = FIN;
            header.sum = 0;                                             //校验和置0
            header.sum = check_sum((u_short *)&header, sizeof(header)); //计算校验和
            memcpy(Buffer, &header, sizeof(header));                    //将首部放入缓冲区
            sendto(socketClient, Buffer, sizeof(header), 0, (sockaddr *)&server_addr, len);
            start = clock();
            cout << "第一次挥手超时，正在进行重传" << endl;
        }
    }

    //进行校验和检验
    memcpy(&header, Buffer, sizeof(header));
    if (header.flag == ACK && check_sum((u_short *)&header, sizeof(header) == 0))
    {
        cout << "收到第二次挥手信息" << endl;
    }
    else
    {
        cout << "连接发生错误，程序直接退出！" << endl;
        return -1;
    }

    //进行第三次挥手
    header.flag = FIN_ACK;
    header.sum = 0;
    header.sum = check_sum((u_short *)&header, sizeof(header)); //计算校验和
    if (sendto(socketClient, (char *)&header, sizeof(header), 0, (sockaddr *)&server_addr, len) == -1)
    {
        return -1;
    }

    start = clock();
    //接收第四次挥手
    while (recvfrom(socketClient, Buffer, sizeof(header), 0, (sockaddr *)&server_addr, &len) <= 0)
    {
        if (clock() - start > MAX_TIME) //超时，重新传输第三次挥手
        {
            header.flag = FIN;
            header.sum = 0;                                             //校验和置0
            header.sum = check_sum((u_short *)&header, sizeof(header)); //计算校验和
            memcpy(Buffer, &header, sizeof(header));                    //将首部放入缓冲区
            sendto(socketClient, Buffer, sizeof(header), 0, (sockaddr *)&server_addr, len);
            start = clock();
            cout << "第四次握手超时，正在进行重传" << endl;
        }
    }
    cout << "四次挥手结束，连接断开！" << endl;
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
    if (tryConnect(server, server_addr, len) == -1)
    {
        return 0;
    }
    string filename;
    cout << "请输入文件名称" << endl;
    cin >> filename;
    ifstream fin(filename.c_str(), ifstream::binary); //以二进制方式打开文件
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