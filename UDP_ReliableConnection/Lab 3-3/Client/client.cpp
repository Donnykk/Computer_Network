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
mutex LOCK_PRINT; // ��־��ӡ����ȷ����־˳�����

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
    // ��һ����������
    header.flag = SYN;
    header.sum = 0;
    header.sum = check_sum((char *)&header, sizeof(header));
    memcpy(buffer, &header, sizeof(header));
    if (sendto(socketClient, buffer, sizeof(header), 0, (sockaddr *)&server_addr, server_addr_len) == SOCKET_ERROR)
    {
        return SOCKET_ERROR;
    }
    cout << "���͵�һ����������" << endl;
    clock_t start = clock();
    unsigned long mode = 1;
    ioctlsocket(socketClient, FIONBIO, &mode);
    // ���յڶ�����������
    while (recvfrom(socketClient, buffer, sizeof(header), 0, (sockaddr *)&server_addr, &server_addr_len) <= 0)
    {
        if (clock() - start > MAX_WAIT_TIME) // ��ʱ�����´����һ������
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
            cout << "��һ�����ֳ�ʱ����ʼ�ش�..." << endl;
        }
    }
    memcpy(&header, buffer, sizeof(header));
    if (header.flag == ACK && check_sum((char *)&header, sizeof(header) == 0))
    {
        cout << "�յ��ڶ�����������" << endl;
    }
    else
    {
        return SOCKET_ERROR;
    }
    // ��������������
    header.flag = ACK_SYN;
    header.sum = 0;
    header.sum = check_sum((char *)&header, sizeof(header));
    if (sendto(socketClient, (char *)&header, sizeof(header), 0, (sockaddr *)&server_addr, server_addr_len) == SOCKET_ERROR)
    {
        return SOCKET_ERROR;
    }
    cout << "���͵�������������" << endl;
    cout << "�������ɹ����ӣ�" << endl;
    return 1;
}

void Receive_Message(SOCKET *socketClient, SOCKADDR_IN *server_addr)
{
    int server_addr_len = sizeof(SOCKADDR_IN);
    HEADER header;
    char *buffer = new char[sizeof(header)];
    int step = 0; // ӵ������ȷ����
    int dup = 0;  // �ظ�ACK��
    // ��Ϊ������ģʽ
    unsigned long mode = 1;
    ioctlsocket(*socketClient, FIONBIO, &mode);
    while (sending)
    {
        int num = 0;
        while (recvfrom(*socketClient, buffer, sizeof(header), 0, (sockaddr *)server_addr, &server_addr_len) < 0)
        {
            if (!sending)
            {
                // ���̷߳������
                mode = 0;
                ioctlsocket(*socketClient, FIONBIO, &mode);
                return;
            }
            if (timer.is_time_out())
            {
                // ��ʱ�ش�
                dup = 0;
                ssthresh = cwnd / 2;
                cwnd = 1;
                state = Slow_Start;
                for (auto package : GBN_BUFFER)
                {
                    sendto(*socketClient, (char *)package, package->header.datasize + sizeof(HEADER), 0,
                           (sockaddr *)server_addr, server_addr_len);
                    LOCK_PRINT.lock();
                    cout << "��ʱ�ش���SEQ:" << package->header.SEQ << endl;
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
                // �յ�����ACK
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
                    cout << "�յ�3���ظ�ACK����ʼ�ش���SEQ:" << p->header.SEQ << endl;
                    LOCK_PRINT.unlock();
                }
            }
            else if (int(header.SEQ) >= first_pos)
            {
                // �յ���ȷACK�����´��ڴ�С
                cout << "�յ�ACK! SEQ:" << int(header.SEQ) << endl;
                if (state == Slow_Start)
                {
                    cwnd++;
                    if (cwnd >= ssthresh)
                        // ����ӵ������׶�
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
                    // ���ٻָ�
                    cwnd = ssthresh;
                    state = Congestion_Avoid;
                    step = 0;
                }
                // ���´���λ��
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
                cout << "�����ѱ�ȷ��! SEQ:" << int(header.SEQ) << endl;
                cout << "���ڣ�" << first_pos << "~" << first_pos + cwnd - 1 << endl;
                LOCK_PRINT.unlock();
            }
        }
        else
        {
            LOCK_PRINT.lock();
            cout << "У��ͳ���" << endl;
            LOCK_PRINT.unlock();
            continue;
        }
        // ������ʱ��
        if (first_pos < last_pos)
        {
            timer.start_();
        }
    }
}

void Send_Message(SOCKET *socketClient, SOCKADDR_IN *server_addr, int &server_addr_len, char *message, int len)
{
    int package_num = len / BUFFER_SIZE + (len % BUFFER_SIZE != 0); // ���ݰ�����
    HEADER header;
    char *buffer = new char[sizeof(header)];
    sending = true;
    thread Receive_Thread(Receive_Message, socketClient, server_addr);
    for (int i = 0; i < len; i += BUFFER_SIZE)
    {
        while (last_pos - first_pos >= MAX_WINDOW || last_pos - first_pos >= cwnd)
            continue; // ����
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
        cout << "������Ϣ " << pack_len << " bytes! SEQ:" << int(package->header.SEQ) << endl;
        LOCK_PRINT.unlock();
        if (first_pos == last_pos)
            timer.start_();
        last_pos++;
    }
    // ���ͽ�����Ϣ
    sending = false;
    Receive_Thread.join();
    header.flag = END;
    header.sum = 0;
    header.sum = check_sum((char *)&header, sizeof(header));
    memcpy(buffer, &header, sizeof(header));
    sendto(*socketClient, buffer, sizeof(header), 0, (sockaddr *)server_addr, server_addr_len);
    cout << "�ѷ��ͽ�������!" << endl;
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
                cout << "���ͳ�ʱ! ��ʼ�ش�..." << endl;
                start_ = clock();
            }
        }
        memcpy(&header, buffer, sizeof(header));
        if (check_sum((char *)&header, sizeof(header)) == 0 && header.flag == END)
        {
            sending = false;
            cout << "�Է��ѳɹ������ļ�!" << endl;
            break;
        }
        else if (check_sum((char *)&header, sizeof(header)) != 0)
        {
            // У��ʧ�ܣ��ش�
            header.flag = END;
            header.sum = 0;
            header.sum = check_sum((char *)&header, sizeof(header));
            memcpy(buffer, &header, sizeof(header));
            sendto(*socketClient, buffer, sizeof(header), 0, (sockaddr *)server_addr, server_addr_len);
            cout << "У��ʧ�ܣ������·��ͽ�������!" << endl;
            start_ = clock();
        }
    }
}

int Wave_hand(SOCKET &socketClient, SOCKADDR_IN &server_addr, int &server_addr_len)
{
    HEADER header;
    char *buffer = new char[sizeof(header)];
    // ��һ����������
    header.flag = FIN;
    header.sum = 0;
    header.sum = check_sum((char *)&header, sizeof(header));
    memcpy(buffer, &header, sizeof(header));
    if (sendto(socketClient, buffer, sizeof(header), 0, (sockaddr *)&server_addr, server_addr_len) == SOCKET_ERROR)
    {
        return SOCKET_ERROR;
    }
    cout << "���͵�һ�λ�������" << endl;
    clock_t start = clock();
    unsigned long mode = 1;
    ioctlsocket(socketClient, FIONBIO, &mode);
    // ���յڶ��λ���
    while (recvfrom(socketClient, buffer, sizeof(header), 0, (sockaddr *)&server_addr, &server_addr_len) <= 0)
    {
        if (clock() - start > MAX_WAIT_TIME) // ��ʱ�����´����һ�λ���
        {
            header.flag = FIN;
            header.sum = 0;
            header.sum = check_sum((char *)&header, sizeof(header));
            memcpy(buffer, &header, sizeof(header));
            sendto(socketClient, buffer, sizeof(header), 0, (sockaddr *)&server_addr, server_addr_len);
            start = clock();
            cout << "��һ�λ��ֳ�ʱ����ʼ�ش�..." << endl;
        }
    }
    memcpy(&header, buffer, sizeof(header));
    if (header.flag == ACK && check_sum((char *)&header, sizeof(header) == 0))
    {
        cout << "�յ��ڶ��λ�������" << endl;
    }
    else
    {
        return SOCKET_ERROR;
    }
    // ���е����λ���
    header.flag = FIN_ACK;
    header.sum = 0;
    header.sum = check_sum((char *)&header, sizeof(header));
    memcpy(buffer, &header, sizeof(header));
    if (sendto(socketClient, buffer, sizeof(header), 0, (sockaddr *)&server_addr, server_addr_len) == SOCKET_ERROR)
    {
        return SOCKET_ERROR;
    }
    cout << "���͵����λ�������" << endl;
    start = clock();
    // ���յ��Ĵλ�������
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
            cout << "�����λ��ֳ�ʱ����ʼ�ش�..." << endl;
        }
    }
    cout << "�Ĵλ��ֽ�����" << endl;
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
    // ��������
    if (Shake_hand(server, server_addr, len) == SOCKET_ERROR)
    {
        return 0;
    }
    string filename;
    cout << "�������ļ�����" << endl;
    cin >> filename;
    memcpy(data_buffer, filename.c_str(), filename.length());
    data_buffer[filename.length()] = 0; // ���ļ������ļ����ݷָ���
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
    cout << "������ʱ��Ϊ:" << (end - start) / CLOCKS_PER_SEC << "s" << endl;
    cout << "������Ϊ:" << ((float)index) / ((end - start) / CLOCKS_PER_SEC) << "byte/s" << endl;
    Wave_hand(server, server_addr, len);
}