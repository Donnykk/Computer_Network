#include <iostream>
#include <WINSOCK2.h>
#include <time.h>
#include <fstream>
#pragma comment(lib, "ws2_32.lib")
using namespace std;

char data_buffer[20000000];

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

int tryConnect(SOCKET &socketClient, SOCKADDR_IN &server_addr, int &server_addr_len)
{
    HEADER header;
    char *buffer = new char[sizeof(header)];
    unsigned short sum;
    //��һ����������
    header.flag = SYN;
    header.sum = 0;
    unsigned short temp = check_sum((unsigned short *)&header, sizeof(header));
    header.sum = temp;
    memcpy(buffer, &header, sizeof(header));
    if (sendto(socketClient, buffer, sizeof(header), 0, (sockaddr *)&server_addr, server_addr_len) == SOCKET_ERROR)
    {
        return SOCKET_ERROR;
    }
    clock_t start = clock();
    unsigned long mode = 1;
    ioctlsocket(socketClient, FIONBIO, &mode);
    //���յڶ�����������
    while (recvfrom(socketClient, buffer, sizeof(header), 0, (sockaddr *)&server_addr, &server_addr_len) <= 0)
    {
        if (clock() - start > MAX_WAIT_TIME) //��ʱ�����´����һ������
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
            cout << "��һ�����ֳ�ʱ����ʼ�ش�..." << endl;
        }
    }
    memcpy(&header, buffer, sizeof(header));
    if (header.flag == ACK && check_sum((unsigned short *)&header, sizeof(header) == 0))
    {
        cout << "�յ��ڶ�����������" << endl;
    }
    else
    {
        return SOCKET_ERROR;
    }
    //��������������
    header.flag = ACK_SYN;
    header.sum = 0;
    header.sum = check_sum((unsigned short *)&header, sizeof(header));
    if (sendto(socketClient, (char *)&header, sizeof(header), 0, (sockaddr *)&server_addr, server_addr_len) == SOCKET_ERROR)
    {
        return SOCKET_ERROR;
    }
    cout << "�������ɹ����ӣ�" << endl;
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
    unsigned short check = check_sum((unsigned short *)buffer, sizeof(header) + len);
    header.sum = check;
    memcpy(buffer, &header, sizeof(header));
    sendto(socketClient, buffer, len + sizeof(header), 0, (sockaddr *)&server_addr, server_addr_len);
    cout << "�������� " << len << " bytes!"
         << " flag:" << int(header.flag) << " SEQ:" << int(header.SEQ) << " SUM:" << int(header.sum) << endl;
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
                header.SEQ = u_char(seq);
                header.flag = u_char(0x0);
                memcpy(buffer, &header, sizeof(header));
                memcpy(buffer + sizeof(header), message, sizeof(header) + len);
                unsigned short check = check_sum((unsigned short *)buffer, sizeof(header) + len); //����У���
                header.sum = check;
                memcpy(buffer, &header, sizeof(header));
                sendto(socketClient, buffer, len + sizeof(header), 0, (sockaddr *)&server_addr, server_addr_len);
                cout << "��ʱ�ش� " << len << " bytes! Flag:" << int(header.flag) << " SEQ:" << int(header.SEQ) << endl;
                clock_t start = clock();
            }
        }
        memcpy(&header, buffer, sizeof(header)); //���������յ���Ϣ����ȡ
        unsigned short check = check_sum((unsigned short *)&header, sizeof(header));
        if (header.SEQ == unsigned short(seq) && header.flag == ACK)
        {
            cout << "�����ѱ�ȷ��! Flag:" << int(header.flag) << " SEQ:" << int(header.SEQ) << endl;
            break;
        }
        else
        {
            continue;
        }
    }
    unsigned long mode = 0;
    ioctlsocket(socketClient, FIONBIO, &mode); //�Ļ�����ģʽ
}

void send(SOCKET &socketClient, SOCKADDR_IN &server_addr, int &server_addr_len, char *message, int len)
{
    int packagenum = len / BUFFER_SIZE + (len % BUFFER_SIZE != 0);
    int seq = 0;
    for (int i = 0; i < packagenum; i++)
    {
        send_package(socketClient, server_addr, server_addr_len, message + i * BUFFER_SIZE, i == packagenum - 1 ? len - (packagenum - 1) * BUFFER_SIZE : BUFFER_SIZE, seq);
        seq++;
        if (seq > 255)
        {
            seq = seq - 256;
        }
    }
    HEADER header;
    char *buffer = new char[sizeof(header)];
    header.flag = END;
    header.sum = 0;
    unsigned short temp = check_sum((unsigned short *)&header, sizeof(header));
    header.sum = temp;
    memcpy(buffer, &header, sizeof(header));
    sendto(socketClient, buffer, sizeof(header), 0, (sockaddr *)&server_addr, server_addr_len);
    cout << "�������!" << endl;
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
                unsigned short temp = check_sum((unsigned short *)&header, sizeof(header));
                header.sum = temp;
                memcpy(buffer, &header, sizeof(header));
                sendto(socketClient, buffer, sizeof(header), 0, (sockaddr *)&server_addr, server_addr_len);
                cout << "��ʱ�ش���" << endl;
                start = clock();
            }
        }
        memcpy(&header, buffer, sizeof(header));
        unsigned short check = check_sum((unsigned short *)&header, sizeof(header));
        if (header.flag == END)
        {
            cout << "�Է��ѳɹ������ļ�!" << endl;
            break;
        }
        else
        {
            continue;
        }
    }
    unsigned long mode = 0;
    ioctlsocket(socketClient, FIONBIO, &mode); //�Ļ�����ģʽ
}

int disConnect(SOCKET &socketClient, SOCKADDR_IN &server_addr, int &server_addr_len)
{
    HEADER header;
    char *buffer = new char[sizeof(header)];
    unsigned short sum;
    //��һ����������
    header.flag = FIN;
    header.sum = 0;
    unsigned short temp = check_sum((unsigned short *)&header, sizeof(header));
    header.sum = temp;
    memcpy(buffer, &header, sizeof(header));
    if (sendto(socketClient, buffer, sizeof(header), 0, (sockaddr *)&server_addr, server_addr_len) == SOCKET_ERROR)
    {
        return SOCKET_ERROR;
    }
    clock_t start = clock();
    unsigned long mode = 1;
    ioctlsocket(socketClient, FIONBIO, &mode);
    //���յڶ��λ���
    while (recvfrom(socketClient, buffer, sizeof(header), 0, (sockaddr *)&server_addr, &server_addr_len) <= 0)
    {
        if (clock() - start > MAX_WAIT_TIME) //��ʱ�����´����һ�λ���
        {
            header.flag = FIN;
            header.sum = 0;
            header.sum = check_sum((unsigned short *)&header, sizeof(header));
            memcpy(buffer, &header, sizeof(header));
            sendto(socketClient, buffer, sizeof(header), 0, (sockaddr *)&server_addr, server_addr_len);
            start = clock();
            cout << "��һ�λ��ֳ�ʱ����ʼ�ش�..." << endl;
        }
    }
    memcpy(&header, buffer, sizeof(header));
    if (header.flag == ACK && check_sum((unsigned short *)&header, sizeof(header) == 0))
    {
        cout << "�յ��ڶ��λ�������" << endl;
    }
    else
    {
        return SOCKET_ERROR;
    }
    //���е����λ���
    header.flag = FIN_ACK;
    header.sum = 0;
    header.sum = check_sum((unsigned short *)&header, sizeof(header));
    if (sendto(socketClient, (char *)&header, sizeof(header), 0, (sockaddr *)&server_addr, server_addr_len) == SOCKET_ERROR)
    {
        return SOCKET_ERROR;
    }
    start = clock();
    //���յ��Ĵλ�������
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
            cout << "���Ĵ����ֳ�ʱ����ʼ�ش�..." << endl;
        }
    }
    cout << "�Ĵλ��ֽ�����" << endl;
    return 1;
}

int main()
{
    WSADATA wsadata;
    WSAStartup(MAKEWORD(2, 2), &wsadata);
    SOCKADDR_IN server_addr;
    SOCKET server;
    server_addr.sin_family = AF_INET; //ʹ��IPV4
    server_addr.sin_port = htons(6666);
    server_addr.sin_addr.s_addr = htonl(2130706433);
    server = socket(AF_INET, SOCK_DGRAM, 0);
    int len = sizeof(server_addr);
    //��������
    if (tryConnect(server, server_addr, len) == SOCKET_ERROR)
    {
        return 0;
    }
    string filename;
    cout << "�������ļ�����" << endl;
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
    cout << "������ʱ��Ϊ:" << (end - start) / CLOCKS_PER_SEC << "s" << endl;
    cout << "������Ϊ:" << ((float)index) / ((end - start) / CLOCKS_PER_SEC) << "byte/s" << endl;
    disConnect(server, server_addr, len);
}