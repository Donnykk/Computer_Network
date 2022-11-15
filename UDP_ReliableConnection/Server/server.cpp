#include <iostream>
#include <WINSOCK2.h>
#include <time.h>
#include <fstream>
#pragma comment(lib, "ws2_32.lib")
using namespace std;

char name_buffer[20];
char data_buffer[200000000];

const int BUFFER_SIZE = 1024;
const unsigned char SYN = 0x1;
const unsigned char ACK = 0x2;
const unsigned char ACK_SYN = 0x3;
const unsigned char FIN = 0x4;
const unsigned char FIN_ACK = 0x5;
const unsigned char END = 0x7;
double MAX_TIME = 1000;

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
    u_short sum = 0;
    u_short datasize = 0;
    unsigned char flag = 0;
    unsigned char SEQ = 0;
};

int recv_message(SOCKET &sockServ, SOCKADDR_IN &client_addr, int &len, char *message)
{
    long int all = 0; //�ļ�����
    HEADER header;
    char *Buffer = new char[BUFFER_SIZE + sizeof(header)];
    int seq = 0;
    int index = 0;

    while (1 == 1)
    {
        int length = recvfrom(sockServ, Buffer, sizeof(header) + BUFFER_SIZE, 0, (sockaddr *)&client_addr, &len); //���ձ��ĳ���
        // cout << length << endl;
        memcpy(&header, Buffer, sizeof(header));
        //�ж��Ƿ��ǽ���
        if (header.flag == END && check_sum((u_short *)&header, sizeof(header)) == 0)
        {
            cout << "�ļ��������" << endl;
            break;
        }
        if (header.flag == unsigned char(0) && check_sum((u_short *)Buffer, length - sizeof(header)))
        {
            //�ж��Ƿ���ܵ��Ǳ�İ�
            if (seq != int(header.SEQ))
            {
                //˵���������⣬����ACK
                header.flag = ACK;
                header.datasize = 0;
                header.SEQ = (unsigned char)seq;
                header.sum = 0;
                u_short temp = check_sum((u_short *)&header, sizeof(header));
                header.sum = temp;
                memcpy(Buffer, &header, sizeof(header));
                //�ط��ð���ACK
                sendto(sockServ, Buffer, sizeof(header), 0, (sockaddr *)&client_addr, len);
                cout << "Send to Clinet ACK:" << (int)header.SEQ << " SEQ:" << (int)header.SEQ << endl;
                continue; //���������ݰ�
            }
            seq = int(header.SEQ);
            if (seq > 255)
            {
                seq = seq - 256;
            }
            //ȡ��buffer�е�����
            cout << "Send message " << length - sizeof(header) << " bytes!Flag:" << int(header.flag) << " SEQ : " << int(header.SEQ) << " SUM:" << int(header.sum) << endl;
            char *temp = new char[length - sizeof(header)];
            memcpy(temp, Buffer + sizeof(header), length - sizeof(header));
            // cout << "size" << sizeof(message) << endl;
            memcpy(message + all, temp, length - sizeof(header));
            all = all + int(header.datasize);

            //����ACK
            header.flag = ACK;
            header.datasize = 0;
            header.SEQ = (unsigned char)seq;
            header.sum = 0;
            u_short temp1 = check_sum((u_short *)&header, sizeof(header));
            header.sum = temp1;
            memcpy(Buffer, &header, sizeof(header));
            //�ط��ð���ACK
            sendto(sockServ, Buffer, sizeof(header), 0, (sockaddr *)&client_addr, len);
            cout << "Send to Clinet ACK:" << (int)header.SEQ << " SEQ:" << (int)header.SEQ << endl;
            seq++;
            if (seq > 255)
            {
                seq = seq - 256;
            }
        }
    }
    //����OVER��Ϣ
    header.flag = END;
    header.sum = 0;
    u_short temp = check_sum((u_short *)&header, sizeof(header));
    header.sum = temp;
    memcpy(Buffer, &header, sizeof(header));
    if (sendto(sockServ, Buffer, sizeof(header), 0, (sockaddr *)&client_addr, len) == -1)
    {
        return -1;
    }
    return all;
}

int tryConnect(SOCKET &sockServ, SOCKADDR_IN &client_addr, int &len)
{

    HEADER header;
    char *Buffer = new char[sizeof(header)];

    //���յ�һ��������Ϣ
    while (1 == 1)
    {
        if (recvfrom(sockServ, Buffer, sizeof(header), 0, (sockaddr *)&client_addr, &len) == -1)
        {
            return -1;
        }
        memcpy(&header, Buffer, sizeof(header));
        if (header.flag == SYN && check_sum((u_short *)&header, sizeof(header)) == 0)
        {
            cout << "�ɹ����յ�һ��������Ϣ" << endl;
            break;
        }
    }

    //���͵ڶ���������Ϣ
    header.flag = ACK;
    header.sum = 0;
    u_short temp = check_sum((u_short *)&header, sizeof(header));
    header.sum = temp;
    memcpy(Buffer, &header, sizeof(header));
    if (sendto(sockServ, Buffer, sizeof(header), 0, (sockaddr *)&client_addr, len) == -1)
    {
        return -1;
    }
    clock_t start = clock(); //��¼�ڶ������ַ���ʱ��

    //���յ���������
    while (recvfrom(sockServ, Buffer, sizeof(header), 0, (sockaddr *)&client_addr, &len) <= 0)
    {
        if (clock() - start > MAX_TIME)
        {
            header.flag = ACK;
            header.sum = 0;
            u_short temp = check_sum((u_short *)&header, sizeof(header));
            header.flag = temp;
            memcpy(Buffer, &header, sizeof(header));
            if (sendto(sockServ, Buffer, sizeof(header), 0, (sockaddr *)&client_addr, len) == -1)
            {
                return -1;
            }
            cout << "�ڶ������ֳ�ʱ�����ڽ����ش�" << endl;
        }
    }

    HEADER temp1;
    memcpy(&temp1, Buffer, sizeof(header));
    if (temp1.flag == ACK_SYN && check_sum((u_short *)&temp1, sizeof(temp1) == 0))
    {
        cout << "�ɹ�����ͨ�ţ����Խ�������" << endl;
    }
    else
    {
        cout << "serve���ӷ��������������ͻ��ˣ�" << endl;
        return -1;
    }
    return 1;
}

int disConnect(SOCKET &sockServ, SOCKADDR_IN &client_addr, int &len)
{
    HEADER header;
    char *Buffer = new char[sizeof(header)];
    while (1 == 1)
    {
        int length = recvfrom(sockServ, Buffer, sizeof(header) + BUFFER_SIZE, 0, (sockaddr *)&client_addr, &len); //���ձ��ĳ���
        memcpy(&header, Buffer, sizeof(header));
        if (header.flag == FIN && check_sum((u_short *)&header, sizeof(header)) == 0)
        {
            cout << "�ɹ����յ�һ�λ�����Ϣ" << endl;
            break;
        }
    }
    //���͵ڶ��λ�����Ϣ
    header.flag = ACK;
    header.sum = 0;
    u_short temp = check_sum((u_short *)&header, sizeof(header));
    header.sum = temp;
    memcpy(Buffer, &header, sizeof(header));
    if (sendto(sockServ, Buffer, sizeof(header), 0, (sockaddr *)&client_addr, len) == -1)
    {
        return -1;
    }
    clock_t start = clock(); //��¼�ڶ��λ��ַ���ʱ��

    //���յ����λ���
    while (recvfrom(sockServ, Buffer, sizeof(header), 0, (sockaddr *)&client_addr, &len) <= 0)
    {
        if (clock() - start > MAX_TIME)
        {
            header.flag = ACK;
            header.sum = 0;
            u_short temp = check_sum((u_short *)&header, sizeof(header));
            header.flag = temp;
            memcpy(Buffer, &header, sizeof(header));
            if (sendto(sockServ, Buffer, sizeof(header), 0, (sockaddr *)&client_addr, len) == -1)
            {
                return -1;
            }
            cout << "�ڶ��λ��ֳ�ʱ�����ڽ����ش�" << endl;
        }
    }

    HEADER temp1;
    memcpy(&temp1, Buffer, sizeof(header));
    if (temp1.flag == FIN_ACK && check_sum((u_short *)&temp1, sizeof(temp1) == 0))
    {
        cout << "�ɹ����յ����λ���" << endl;
    }
    else
    {
        cout << "��������,�ͻ��˹رգ�" << endl;
        return -1;
    }

    //���͵��Ĵλ�����Ϣ
    header.flag = FIN_ACK;
    header.sum = 0;
    temp = check_sum((u_short *)&header, sizeof(header));
    header.sum = temp;
    memcpy(Buffer, &header, sizeof(header));
    if (sendto(sockServ, Buffer, sizeof(header), 0, (sockaddr *)&client_addr, len) == -1)
    {
        cout << "��������,�ͻ��˹رգ�" << endl;
        return -1;
    }
    cout << "�Ĵλ��ֽ��������ӶϿ���" << endl;
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
    bind(server, (SOCKADDR *)&server_addr, sizeof(server_addr)); //���׽��֣��������״̬
    cout << "�������״̬���ȴ��ͻ�������" << endl;
    int len = sizeof(server_addr);
    //��������
    tryConnect(server, server_addr, len);
    int namelen = recv_message(server, server_addr, len, name_buffer);
    int datalen = recv_message(server, server_addr, len, data_buffer);
    string a;
    for (int i = 0; i < namelen; i++)
    {
        a = a + name_buffer[i];
    }
    disConnect(server, server_addr, len);
    ofstream fout(a.c_str(), ofstream::binary);
    for (int i = 0; i < datalen; i++)
    {
        fout << data_buffer[i];
    }
    fout.close();
    cout << "�ļ��ѳɹ����ص�����" << endl;
}
