#include <iostream>
#include <WINSOCK2.h>
#include <mutex>
#include <thread>
#include <vector>
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
#define Slow_Start 0
#define Congestion_Avoid 1
#define Quick_Recover 2

SOCKADDR_IN server_addr;
SOCKET server;

char data_buffer[20000000];
const int BUFFER_SIZE = 4096;
const int MAX_WINDOW = 10;
const double MAX_WAIT_TIME = CLOCKS_PER_SEC * 5;
int cwnd = 1;
int ssthresh = 16;
int first_pos = 0;
int last_pos = 0;
int state = Slow_Start;
bool sending = false;

struct HEADER
{
    unsigned short sum = 0;
    unsigned short datasize = 0;
    unsigned short flag = 0;
    unsigned short SEQ = 0;
};

struct Package
{
    HEADER header;
    char data[BUFFER_SIZE];
};

struct Timer
{
    clock_t start;
    mutex mtx;
    void start_()
    {
        mtx.lock();
        start = clock();
        mtx.unlock();
    }
    bool is_time_out()
    {
        if (clock() - start >= MAX_WAIT_TIME)
            return true;
        else
            return false;
    }
} timer;

vector<Package *> GBN_BUFFER;
mutex LOCK_BUFFER;
mutex LOCK_PRINT; // 日志打印锁，确保日志顺序输出

unsigned short check_sum(char *message, int size)
{
    int count = (size + 1) / 2;
    unsigned short* buf = new unsigned short[size + 1];
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
    // 第一次握手请求
    header.flag = SYN;
    header.sum = 0;
    header.sum = check_sum((char *)&header, sizeof(header));
    memcpy(buffer, &header, sizeof(header));
    if (sendto(socketClient, buffer, sizeof(header), 0, (sockaddr *)&server_addr, server_addr_len) == SOCKET_ERROR)
    {
        return SOCKET_ERROR;
    }
    cout << "发送第一次握手请求" << endl;
    clock_t start = clock();
    unsigned long mode = 1;
    ioctlsocket(socketClient, FIONBIO, &mode);
    // 接收第二次握手请求
    while (recvfrom(socketClient, buffer, sizeof(header), 0, (sockaddr *)&server_addr, &server_addr_len) <= 0)
    {
        if (clock() - start > MAX_WAIT_TIME) // 超时，重新传输第一次握手
        {
            header.flag = SYN;
            header.sum = 0;
            header.sum = check_sum((char *)&header, sizeof(header));
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
    if (header.flag == ACK && check_sum((char *)&header, sizeof(header) == 0))
    {
        cout << "收到第二次握手请求" << endl;
    }
    else
    {
        return SOCKET_ERROR;
    }
    // 第三次握手请求
    header.flag = ACK_SYN;
    header.sum = 0;
    header.sum = check_sum((char *)&header, sizeof(header));
    if (sendto(socketClient, (char *)&header, sizeof(header), 0, (sockaddr *)&server_addr, server_addr_len) == SOCKET_ERROR)
    {
        return SOCKET_ERROR;
    }
    cout << "发送第三次握手请求" << endl;
    cout << "服务器成功连接！" << endl;
    return 1;
}

void Receive_Message(SOCKET *socketClient, SOCKADDR_IN *server_addr)
{
    int server_addr_len = sizeof(SOCKADDR_IN);
    HEADER header;
    char *buffer = new char[sizeof(header)];
    int step = 0; // 拥塞避免确认数
    int dup = 0;  // 重复ACK数
    // 变为非阻塞模式
    unsigned long mode = 1;
    ioctlsocket(*socketClient, FIONBIO, &mode);
    while (sending)
    {
        int num = 0;
        while (recvfrom(*socketClient, buffer, sizeof(header), 0, (sockaddr *)server_addr, &server_addr_len) < 0)
        {
            if (!sending)
            {
                // 主线程发送完毕
                mode = 0;
                ioctlsocket(*socketClient, FIONBIO, &mode);
                return;
            }
            if (timer.is_time_out())
            {
                // 超时重传
                dup = 0;
                ssthresh = cwnd / 2;
                cwnd = 1;
                state = Slow_Start;
                for (auto package : GBN_BUFFER)
                {
                    sendto(*socketClient, (char *)package, package->header.datasize + sizeof(HEADER), 0,
                           (sockaddr *)server_addr, server_addr_len);
                    LOCK_PRINT.lock();
                    cout << "超时重传，SEQ:" << package->header.SEQ << endl;
                    LOCK_PRINT.unlock();
                }
                timer.start_();
            }
        }
        memcpy(&header, buffer, sizeof(header));
        if (check_sum((char *)&header, sizeof(header)) == 0)
        {
            if (int(header.SEQ) < first_pos)
            {
                // 收到冗余ACK
                if (state == Quick_Recover)
                    cwnd++;
                dup++;
                if (dup == 3)
                {
                    if (state == Slow_Start || state == Congestion_Avoid)
                    {
                        state = Quick_Recover;
                        ssthresh = cwnd / 2;
                        cwnd = ssthresh + 3;
                    }
                    Package *p = GBN_BUFFER[0];
                    sendto(*socketClient, (char *)p, p->header.datasize + sizeof(HEADER), 0, (sockaddr *)server_addr, server_addr_len);
                    LOCK_PRINT.lock();
                    cout << "收到3个重复ACK，开始重传，SEQ:" << p->header.SEQ << endl;
                    LOCK_PRINT.unlock();
                }
            }
            else if (int(header.SEQ) >= first_pos)
            {
                // 收到正确ACK，更新窗口大小
                cout << "收到ACK! SEQ:" << int(header.SEQ) << endl;
                if (state == Slow_Start)
                {
                    cwnd++;
                    if (cwnd >= ssthresh)
                        // 进入拥塞避免阶段
                        state = Congestion_Avoid;
                }
                else if (state == Congestion_Avoid)
                {
                    step++;
                    if (step >= cwnd)
                    {
                        step = 0;
                        cwnd++;
                    }
                }
                else
                {
                    // 快速恢复
                    cwnd = ssthresh;
                    state = Congestion_Avoid;
                    step = 0;
                }
                // 更新窗口位置
                int count = int(header.SEQ) - first_pos + 1;
                for (int i = 0; i < count; i++)
                {
                    LOCK_BUFFER.lock();
                    if (GBN_BUFFER.size() <= 0)
                        break;
                    delete GBN_BUFFER[0];
                    GBN_BUFFER.erase(GBN_BUFFER.begin());
                    LOCK_BUFFER.unlock();
                }
                first_pos = header.SEQ + 1;
                LOCK_PRINT.lock();
                cout << "发送已被确认! SEQ:" << int(header.SEQ) << endl;
                cout << "窗口：" << first_pos << "~" << first_pos + cwnd - 1 << endl;
                LOCK_PRINT.unlock();
            }
        }
        else
        {
            LOCK_PRINT.lock();
            cout << "校验和出错！" << endl;
            LOCK_PRINT.unlock();
            continue;
        }
        // 重启计时器
        if (first_pos < last_pos)
        {
            timer.start_();
        }
    }
}

void Send_Message(SOCKET *socketClient, SOCKADDR_IN *server_addr, int &server_addr_len, char *message, int len)
{
    int package_num = len / BUFFER_SIZE + (len % BUFFER_SIZE != 0); // 数据包数量
    HEADER header;
    char *buffer = new char[sizeof(header)];
    sending = true;
    thread Receive_Thread(Receive_Message, socketClient, server_addr);
    for (int i = 0; i < len; i += BUFFER_SIZE)
    {
        while (last_pos - first_pos >= MAX_WINDOW || last_pos - first_pos >= cwnd)
            continue; // 阻塞
        Package *package = new Package;
        int pack_len = BUFFER_SIZE;
        if (i + BUFFER_SIZE > len)
            pack_len = len - i;
        package->header.datasize = pack_len;
        package->header.sum = 0;
        package->header.SEQ = last_pos;
        memcpy(package->data, message + i, pack_len);
        package->header.sum = check_sum((char *)package, sizeof(header) + pack_len);
        LOCK_BUFFER.lock();
        GBN_BUFFER.push_back(package);
        LOCK_BUFFER.unlock();
        sendto(*socketClient, (char *)package, pack_len + sizeof(header), 0, (sockaddr *)server_addr, server_addr_len);
        LOCK_PRINT.lock();
        cout << "发送信息 " << pack_len << " bytes! SEQ:" << int(package->header.SEQ) << endl;
        LOCK_PRINT.unlock();
        if (first_pos == last_pos)
            timer.start_();
        last_pos++;
    }
    // 发送结束信息
    sending = false;
    Receive_Thread.join();
    header.flag = END;
    header.sum = 0;
    header.sum = check_sum((char *)&header, sizeof(header));
    memcpy(buffer, &header, sizeof(header));
    sendto(*socketClient, buffer, sizeof(header), 0, (sockaddr *)server_addr, server_addr_len);
    cout << "已发送结束请求!" << endl;
    clock_t start_ = clock();
    while (true)
    {
        unsigned long mode = 1;
        ioctlsocket(*socketClient, FIONBIO, &mode);
        while (recvfrom(*socketClient, buffer, BUFFER_SIZE, 0, (sockaddr *)server_addr, &server_addr_len) <= 0)
        {
            clock_t present = clock();
            if (present - start_ > MAX_WAIT_TIME)
            {
                char *buffer = new char[sizeof(header)];
                header.flag = END;
                header.sum = 0;
                header.sum = check_sum((char *)&header, sizeof(header));
                memcpy(buffer, &header, sizeof(header));
                sendto(*socketClient, buffer, sizeof(header), 0, (sockaddr *)server_addr, server_addr_len);
                cout << "发送超时! 开始重传..." << endl;
                start_ = clock();
            }
        }
        memcpy(&header, buffer, sizeof(header));
        if (check_sum((char *)&header, sizeof(header)) == 0 && header.flag == END)
        {
            sending = false;
            cout << "对方已成功接收文件!" << endl;
            break;
        }
        else if (check_sum((char *)&header, sizeof(header)) != 0)
        {
            // 校验失败，重传
            header.flag = END;
            header.sum = 0;
            header.sum = check_sum((char *)&header, sizeof(header));
            memcpy(buffer, &header, sizeof(header));
            sendto(*socketClient, buffer, sizeof(header), 0, (sockaddr *)server_addr, server_addr_len);
            cout << "校验失败，已重新发送结束请求!" << endl;
            start_ = clock();
        }
    }
}

int Wave_hand(SOCKET &socketClient, SOCKADDR_IN &server_addr, int &server_addr_len)
{
    HEADER header;
    char *buffer = new char[sizeof(header)];
    // 第一次握手请求
    header.flag = FIN;
    header.sum = 0;
    header.sum = check_sum((char *)&header, sizeof(header));
    memcpy(buffer, &header, sizeof(header));
    if (sendto(socketClient, buffer, sizeof(header), 0, (sockaddr *)&server_addr, server_addr_len) == SOCKET_ERROR)
    {
        return SOCKET_ERROR;
    }
    cout << "发送第一次挥手请求" << endl;
    clock_t start = clock();
    unsigned long mode = 1;
    ioctlsocket(socketClient, FIONBIO, &mode);
    // 接收第二次挥手
    while (recvfrom(socketClient, buffer, sizeof(header), 0, (sockaddr *)&server_addr, &server_addr_len) <= 0)
    {
        if (clock() - start > MAX_WAIT_TIME) // 超时，重新传输第一次挥手
        {
            header.flag = FIN;
            header.sum = 0;
            header.sum = check_sum((char *)&header, sizeof(header));
            memcpy(buffer, &header, sizeof(header));
            sendto(socketClient, buffer, sizeof(header), 0, (sockaddr *)&server_addr, server_addr_len);
            start = clock();
            cout << "第一次挥手超时，开始重传..." << endl;
        }
    }
    memcpy(&header, buffer, sizeof(header));
    if (header.flag == ACK && check_sum((char *)&header, sizeof(header) == 0))
    {
        cout << "收到第二次挥手请求" << endl;
    }
    else
    {
        return SOCKET_ERROR;
    }
    // 进行第三次挥手
    header.flag = FIN_ACK;
    header.sum = 0;
    header.sum = check_sum((char *)&header, sizeof(header));
    memcpy(buffer, &header, sizeof(header));
    if (sendto(socketClient, buffer, sizeof(header), 0, (sockaddr *)&server_addr, server_addr_len) == SOCKET_ERROR)
    {
        return SOCKET_ERROR;
    }
    cout << "发送第三次挥手请求" << endl;
    start = clock();
    // 接收第四次挥手请求
    while (recvfrom(socketClient, buffer, sizeof(header), 0, (sockaddr *)&server_addr, &server_addr_len) <= 0)
    {
        if (clock() - start > MAX_WAIT_TIME)
        {
            header.flag = FIN_ACK;
            header.sum = 0;
            header.sum = check_sum((char *)&header, sizeof(header));
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
    // 建立连接
    if (Shake_hand(server, server_addr, len) == SOCKET_ERROR)
    {
        return 0;
    }
    string filename;
    cout << "请输入文件名称" << endl;
    cin >> filename;
    memcpy(data_buffer, filename.c_str(), filename.length());
    data_buffer[filename.length()] = 0; // 把文件名和文件内容分隔开
    ifstream fin(filename.c_str(), ifstream::binary);
    int index = filename.length() + 1;
    unsigned char temp = fin.get();
    while (fin)
    {
        data_buffer[index++] = temp;
        temp = fin.get();
    }
    fin.close();
    clock_t start = clock();
    Send_Message(&server, &server_addr, len, data_buffer, index);
    clock_t end = clock();
    cout << "传输总时间为:" << (end - start) / CLOCKS_PER_SEC << "s" << endl;
    cout << "吞吐率为:" << ((float)index) / ((end - start) / CLOCKS_PER_SEC) << "byte/s" << endl;
    Wave_hand(server, server_addr, len);
}