#include <time.h>
#include <windows.h>
#include <winsock.h>
#include <stdio.h>
#include <iostream>
#include <fstream>
#include <sys/types.h>
#include <sys/stat.h>
#include <string>
#include <vector>
#include <queue>
#include <set>



using namespace std;

#define FILECHUNK 100

const __int64 DELTA_EPOCH_IN_MICROSECS= 11644473600000000;
struct timezone2 
{
	__int32  tz_minuteswest; /* minutes W of Greenwich */
	bool  tz_dsttime;     /* type of dst correction */
};

struct timeval2 {
	__int32    tv_sec;         /* seconds */
	__int32    tv_usec;        /* microseconds */
};
typedef enum{
	PROTOCOL_ACK=1,PROTOCOL_NAK
}TYPE;

typedef struct
{
	int sequencenumber;
	int length;
	char data[FILECHUNK];
}Packet;

typedef struct  
{
	TYPE type;
	int sequencenumber;
}ACKNAK;

class Protocol
{
	int base ;
	int nextsequence ; //sequence number next to attach
	int lastframesequence; //sequence of last packet in window
	int numberoftotalpacket; //total packet of file
	int numberofsentpacket; //number of packet that has been sent
	int windowsize;
	int sequenceMax; //windowsize + 1
	int filelocation;
	deque<Packet> Window;
	FILE fin;

public:
	Protocol(){}
	//sending protocol
	int initial();
	int initialWindow(FILE *fin,int total); //put w_size packets into window
	void PutFileToWindow(FILE *fin);
	int MoveWindowToNAK(deque<Packet>,ACKNAK);
	int SendWindow(SOCKET,deque<Packet>,SOCKADDR_IN);
	int IncreaseSequence();
	int SetTimeout(SOCKET,long,long);
	int MoveBaseToNAK(deque<Packet>,ACKNAK);
	int GetLastFrameSeq(int sequenceMax,int total);
	bool CheckIfSentOver(int base,int sequenceMax,int total);// at the last packet for the file 
	int MoveWindowForNextPacket(deque<Packet>,ACKNAK);
	int MoveBase(ACKNAK);
	//int MoveBaseToAck(deque<Packet>,ACKNAK);
	int SendNewFrameOfWindow(SOCKET,deque<Packet>,SOCKADDR_IN);
	int GetPacketPositionInWindowByAck(deque<Packet>,ACKNAK);
	int Protocol_Implementation(FILE *,int,SOCKET,SOCKADDR_IN,SOCKADDR_IN,int);
	set<int> GetPreviousACKNAKInWindow(deque<Packet>,ACKNAK);
	int get_timeout(struct timeval2, struct timeval *);

	//receiving protocol
	int Receive(FILE *,SOCKET,SOCKADDR_IN,int,int);
};
