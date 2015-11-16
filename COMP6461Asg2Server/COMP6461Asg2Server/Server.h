
#ifndef SER_TCP_H
#define SER_TCP_H
#define _CRT_SECURE_NO_WARNINGS
#include <winsock.h>
#include <iostream>
#include <windows.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <process.h>
#include <fstream>
#include <vector>
#include <string>
#include "server.h"
#include "Protocol.h"

#pragma comment (lib, "Ws2_32.lib")
using namespace std;


#define FILECHUNK 100
#define HOSTNAME_LENGTH 20
#define RESP_LENGTH 80
#define FILENAME_LENGTH 20
#define REQUEST_PORT 5001
#define BUFFER_LENGTH 1024 
#define MAXPENDING 10
#define MSGHDRSIZE 8 //Message Header Size
///////////////////////////////////////////////
#define UDPDATABUFFER 128
#define SEQ0 0
#define SEQ1 1
#define HANDSHAKEBUFFER 4
#define DATAPACKETHEAD 8
#define TRACE 1


typedef enum{
	RESP=1,POST,GET,LIST,DELETE_FILE,RENAME_FILE //Message type
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


class TcpServer
{
	std::ofstream fout;			//log file
	sockaddr_in ClientAddr; /* Client address */
	sockaddr_in ServerAddr; /* Server address */
	unsigned short ServerPort;     /* Server port */
	int clientLen;            /* Length of Server address data structure */
	char servername[HOSTNAME_LENGTH];
	int nextsendSeq;
	int nextrecvSeq;
	int numberofpacket;
	int numberofbytesent;
	char * fn;


public:
	int serverSock,clientSock;     /* Socket descriptor for server and client*/
	TcpServer();
	~TcpServer();
	void TcpServer::start();
	void handshake();
	int msg_recv(int ,Msg * );
	int msg_send(int ,Msg * );
	unsigned long ResolveName(char name[]);
	static void err_sys(char * fmt,...);
	int TimeOutUDP(SOCKET socket,long sec, long usec);
	int SendACK(SOCKET,char*);
	int recv_pct(SOCKET,ControlPacket *,int);
	int recv_pct(SOCKET,DataPacket *,int);
	int send_pct(SOCKET,ControlPacket *,int);
	int send_pct(SOCKET,DataPacket*);
	void switchRecvSeq();
	void switchSendSeq();
	void RecvDupCp(SOCKET,ControlPacket*,int);
	void RecvDuppacket(SOCKET,char*,int);
};


#endif
