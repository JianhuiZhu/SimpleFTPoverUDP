
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <winsock.h>
#include <stdio.h>
#include <iostream>
#include <fstream>
#include <sys/types.h>
#include <sys/stat.h>
#include <string>
#include <vector>
#include <math.h>
#include "Protocol.h"

#pragma comment (lib, "Ws2_32.lib")
using namespace std;


#define FILECHUNK 100
#define FILESIZEBUFFER 20
#define HOSTNAME_LENGTH 20
#define RESP_LENGTH 80
#define FILENAME_LENGTH 20
#define RECVPORT 5000
#define ROUTERPORT 7000
#define BUFFER_LENGTH 1024 
#define TRACE 0
#define MSGHDRSIZE 8 //Message Header Size
///////////////////////////////////////
#define UDPDATABUFFER 128
#define SEQ0 0
#define SEQ1 1
#define HANDSHAKEBUFFER 4
#define DATAPACKETHEAD 8
#define TRACE 1

typedef enum{
	RESP = 1, POST, GET, LIST, DELETE_FILE, RENAME_FILE //Message type
} Type;

typedef struct{
	unsigned int sequence;
	Type type;
	unsigned int filesize;
	char filename[FILENAME_LENGTH];
}ControlPacket;

typedef struct{
	unsigned int sequence;
	unsigned int length;
	char data[UDPDATABUFFER];
}DataPacket;

typedef struct{
	unsigned int sequence;
}ACK;

typedef struct{
	unsigned int ack;
	unsigned int local_random;
}HandShakeACK;


typedef struct
{
	char hostname[HOSTNAME_LENGTH];
	char filename[FILENAME_LENGTH];
	unsigned int filesize;
} Req;  //request

typedef struct
{
	char response[RESP_LENGTH];
} Resp; //response


typedef struct
{
	Type type;
	int  length; //length of effective bytes in the buffer
	char buffer[BUFFER_LENGTH];
} Msg; //message format used for sending and receiving


class TcpClient
{
	int sock;                    /* Socket descriptor */
	struct sockaddr_in sa_in; /* server socket address */
	SOCKADDR_IN RouterAddr;
	SOCKADDR_IN from;
	unsigned short RecvPort;     /*  port to recv msg */
	Req req;               /* request */
	Resp * respp;          /* pointer to response*/
	Msg smsg, rmsg;               /* receive_message and send_message */
	WSADATA wsadata;
	int nextsendSeq;
	int nextrecvSeq;
	int RouterAddrLen;
	int fromlen;
	ofstream fout;					//log file
	int numberofpacket;
	int numberofbytesent;
public:
	TcpClient(){}
	void run(int argc, char * argv[]);
	~TcpClient();
	int msg_recv(int, Msg *);
	int msg_send(int, Msg *);
	unsigned long ResolveName(char name[]);
	void err_sys(char * fmt, ...);
	void handshake();
	void switchRecvSeq();
	void switchSendSeq();
	int TimeOutUDP(SOCKET socket, long sec, long usec);
	int send_pct(SOCKET, ControlPacket *, int);
	int send_pct(SOCKET, DataPacket*);
	int recv_pct(SOCKET, ControlPacket *, int);
	int recv_pct(SOCKET, DataPacket *, int);
	int RecvDuppacket(SOCKET, char*, int);

};