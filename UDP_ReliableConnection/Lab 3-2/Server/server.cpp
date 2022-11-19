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
    //���յ�һ����������
    while (true)
    {
        if (recvfrom(socketServer, buffer, sizeof(header), 0, (sockaddr *)&client_addr, &len) == SOCKET_ERROR)
        {
            return SOCKET_ERROR;
        }
        memcpy(&header, buffer, sizeof(header));
        if (header.flag == SYN && check_sum((unsigned short *)&header, sizeof(header)) == 0)
        {
            cout << "�յ���һ����������" << endl;
            break;
        }
    }
    //���͵ڶ�����������
    header.flag = ACK;
    header.sum = 0;
    unsigned short temp = check_sum((unsigned short *)&header, sizeof(header));
    header.sum = temp;
    memcpy(buffer, &header, sizeof(header));
    if (sendto(socketServer, buffer, sizeof(header), 0, (sockaddr *)&client_addr, len) == SOCKET_ERROR)
    {
        return SOCKET_ERROR;
    }
    cout << "�ѷ��͵ڶ�����������" << endl;
    clock_t start = clock();
    //���յ���������
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
            cout << "�ڶ������ֳ�ʱ����ʼ�ش�..." << endl;
        }
    }
    HEADER temp_header;
    memcpy(&temp_header, buffer, sizeof(header));
    if (temp_header.flag == ACK_SYN && check_sum((unsigned short *)&temp_header, sizeof(temp_header) == 0))
    {
        cout << "���ֳɹ����ͻ���������" << endl;
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
            cout << "�ļ��ѽ���" << endl;
            break;
        }
        if (header.flag == unsigned char(0) && check_sum((unsigned short *)buffer, length - sizeof(header)))
        {
            //�ж����к��Ƿ����
            if (seq != int(header.SEQ))
            {
                header.flag = ACK;
                header.datasize = 0;
                header.SEQ = (unsigned char)seq;
                header.sum = 0;
                unsigned short temp = check_sum((unsigned short *)&header, sizeof(header));
                header.sum = temp;
                memcpy(buffer, &header, sizeof(header));
                //�ط��ð���ACK
                if (sendto(socketServer, buffer, sizeof(header), 0, (sockaddr *)&client_addr, len) == SOCKET_ERROR)
                {
                    return SOCKET_ERROR;
                }
                cout << "��ͻ����ش�ACK��SEQ:" << (int)header.SEQ << endl;
                continue; //���������ݰ�
            }
            seq = int(header.SEQ);
            if (seq > 255)
            {
                seq = seq - 256;
            }
            cout << "�յ����� " << length - sizeof(header) << " bytes! Flag:" << int(header.flag) << " SUM:" << int(header.sum) << " SEQ : " << int(header.SEQ) << endl;
            char *temp = new char[length - sizeof(header)];
            memcpy(temp, buffer + sizeof(header), length - sizeof(header));
            memcpy(message + all, temp, length - sizeof(header));
            all += int(header.datasize);
            //����ACK
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
            cout << "�ѷ���ACK:" << (int)header.SEQ << " SEQ:" << (int)header.SEQ << endl;
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
            cout << "�յ���һ�λ�������" << endl;
            break;
        }
    }
    //���͵ڶ��λ�����Ϣ
    header.flag = ACK;
    header.sum = 0;
    unsigned short temp = check_sum((unsigned short *)&header, sizeof(header));
    header.sum = temp;
    memcpy(buffer, &header, sizeof(header));
    if (sendto(socketServer, buffer, sizeof(header), 0, (sockaddr *)&client_addr, len) == SOCKET_ERROR)
    {
        return SOCKET_ERROR;
    }
    cout << "���͵ڶ��λ�������" << endl;
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
            cout << "�ڶ��λ��ֳ�ʱ����ʼ�ش�..." << endl;
        }
    }
    HEADER temp_header;
    memcpy(&temp_header, buffer, sizeof(header));
    if (temp_header.flag == FIN_ACK && check_sum((unsigned short *)&temp_header, sizeof(temp_header) == 0))
    {
        cout << "�յ������λ�������" << endl;
    }
    else
    {
        return SOCKET_ERROR;
    }
    //���͵��Ĵλ�������
    header.flag = FIN_ACK;
    header.sum = 0;
    temp = check_sum((unsigned short *)&header, sizeof(header));
    header.sum = temp;
    memcpy(buffer, &header, sizeof(header));
    if (sendto(socketServer, buffer, sizeof(header), 0, (sockaddr *)&client_addr, len) == SOCKET_ERROR)
    {
        return SOCKET_ERROR;
    }
    cout << "���͵��Ĵλ�������" << endl;
    cout << "�Ĵλ��ֽ��������ӶϿ���" << endl;
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
    cout << "��ʼ����......" << endl;
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
    cout << "�ļ��ѳɹ����ص�����" << endl;
    disConnect(server, server_addr, len);
}