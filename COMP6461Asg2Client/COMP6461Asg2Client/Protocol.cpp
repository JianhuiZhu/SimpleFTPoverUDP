#include "Protocol.h"


#pragma comment (lib, "Ws2_32.lib")

void printwindow(deque<Packet> window)
{
	printf("   Window : ");
	deque<Packet>::iterator it= window.begin();
	for(;it!=window.end();it++)
		printf("%d",it->sequencenumber);
	printf("\n");
}

int gettimeofday(struct timeval2 *tv/*in*/, struct timezone2 *tz/*in*/)
{
	FILETIME ft;
	__int64 tmpres = 0;
	// TIME_ZONE_INFORMATION tz_winapi;
	int rez=0;

	ZeroMemory(&ft,sizeof(ft));
	//ZeroMemory(&tz_winapi,sizeof(tz_winapi));

	GetSystemTimeAsFileTime(&ft);

	tmpres = ft.dwHighDateTime;
	tmpres <<= 32;
	tmpres |= ft.dwLowDateTime;

	/*converting file time to unix epoch*/
	tmpres /= 10;  /*convert into microseconds*/
	tmpres -= DELTA_EPOCH_IN_MICROSECS; 
	tv->tv_sec = (__int32)(tmpres*0.000001);
	tv->tv_usec =(tmpres%1000000);


	//_tzset(),don't work properly, so we use GetTimeZoneInformation
	//rez=GetTimeZoneInformation(&tz_winapi);
	//tz->tz_dsttime=false;
	//tz->tz_minuteswest = (__int32)0;

	return 0;
}

int Protocol::get_timeout(struct timeval2 start, struct timeval *timeout)
{
	struct timeval2 current;
	gettimeofday(&current, NULL);

	long diff;


	diff = (current.tv_sec - start.tv_sec) * 1000000 + (current.tv_usec - start.tv_usec);

	long timeout_l = 300000 - diff;

	if( timeout_l < 0 ){
		timeout->tv_sec = 0;
		timeout->tv_usec = 0;
	}else{
		timeout->tv_sec = timeout_l / 1000000;
		timeout->tv_usec = timeout_l;
	}
	printf("Timeout: %d:%d\n", timeout->tv_sec, timeout->tv_usec);
	return 0;
}

int Protocol::initial()
{
	base = 1;
	nextsequence = 1;
	lastframesequence = 1;
	numberofsentpacket = 0;
	numberoftotalpacket = 0;
	windowsize = 1;
	sequenceMax = 2*windowsize + 1;
	filelocation = -1;
	Window.clear();
	return 0;
}

//put file into window when the protocol starts
int Protocol::initialWindow(FILE *fin,int total)
{
	//seq : 1 ~ w_size
	if(total < windowsize)
	{
		for(int i=0;i<total;i++)
			PutFileToWindow(fin);
	}
	else
	{

		for(int i = 0;i<windowsize;i++)
		{
			PutFileToWindow(fin);
		}
	}
	return 0;
}

void Protocol::PutFileToWindow(FILE *fin)
{
	Packet packet;
	ZeroMemory(&packet.data,sizeof(packet.data));
	/*
		function fread parameters list

		ptr:	packet.data 
				Pointer to a block of memory with a size of at least (size*count) bytes, converted to a void*

		size:	sizeof(char)
				Size, in bytes, of each element to be read.
				size_t is an unsigned integral type.

		count:	FILECHUNK
				Number of elements, each one with a size of size bytes.
				size_t is an unsigned integral type.

		stream:	fin
				Pointer to a FILE object that specifies an input stream.

		return	nbytes
				# of bytes readed in
	*/
	int nbytes = fread(packet.data, sizeof(char), FILECHUNK, fin);
	packet.length = nbytes * sizeof(char);
	packet.sequencenumber = nextsequence;
	Window.push_back(packet);
	IncreaseSequence();
	//filelocation is class member variable
	printf("read %d from file. File location: %d\n", packet.length, filelocation);
	filelocation++;
	printf("end reading file\n");
	//delete &packet;
	//return 0;
}

int Protocol::MoveWindowToNAK(deque<Packet> window,ACKNAK acknak)
{
	/*
		Find the location of packet which has not been acknowledged
	*/
	int count = 0;
	int sequence = acknak.sequencenumber;
	deque<Packet>::iterator it = window.begin();
	for(;it!=window.end();it++)
	{
		if(sequence==it->sequencenumber) //NAK is the next expected seq for receiver,before NAK all received
			break;
		count ++;
	}
	//pop all packet before NAK
	if(count==0||count==Window.size())//NAK out of bound ,before or after window
	{
		//access the last element's sequence number and +1, check if is larger than maximum sequence number
		int n = Window.back().sequencenumber + 1;
		if(n>sequenceMax)
			n=1;
		//说明window内已经全部获得确认，未被确认的为window以外的第一个包
		if(acknak.sequencenumber==n)//this case u need move your window!! [1234]5 or[6789]1
		{
			//The packets to be sent is less than the window size
			if(Window.size()>numberoftotalpacket-numberofsentpacket)
			{
				for(int i=0;i<Window.size();i++)
					Window.pop_front();
				for(int j=0;j<numberoftotalpacket-numberofsentpacket;j++)
					PutFileToWindow(&fin);
			}
			else
			{
				for(int i=0;i<Window.size();i++)
				{
					Window.pop_front();
					PutFileToWindow(&fin);
				}
			}
			count = Window.size();//move window size
		}
		else
			return 0;//this case NAK is before window, do not need to move window
	}
	//test if the packet rest is less than the step that the window can move most
	//count is the number that window can move
	if(count>numberoftotalpacket-numberofsentpacket)
	{
		//still pop all the packet before NAK
		for(int i=0;i<count;i++)
			Window.pop_front();
		//but here only put rest packets into window
		for(int j=0;j<numberoftotalpacket-numberofsentpacket;j++)
			PutFileToWindow(&fin);
	}
	else
	{
		//pop packets before NAK.sequence then push new packets from file into window
		for(int i =0;i<count;i++)
		{
			Window.pop_front();
			PutFileToWindow(&fin);
		}
	}
	MoveBaseToNAK(window,acknak);
	return count;//return number of step(packet) that window moves
}

//sequence number next to attach to packet
int Protocol::IncreaseSequence()
{
	nextsequence++;
	if(nextsequence>2*windowsize+1)
		nextsequence = 1;
	return nextsequence;
}
//To be Rewrited, sent the packet which is not acknowledged only instead of send the whole window
int Protocol::SendWindow(SOCKET socket,deque<Packet> window,SOCKADDR_IN dst)
{
	deque<Packet>::iterator it = window.begin();
	int count = 0;
	//send window
	for(;it!=window.end();it++)
	{
		Packet packet ;
		ZeroMemory(&packet,sizeof(packet));
		packet = *it;
		sendto(socket,(char*)&packet,sizeof(packet),0,(struct sockaddr*)&dst,sizeof(dst));
		count++;
	}
	return count;
}

int Protocol::SetTimeout(SOCKET socket,long sec,long usec)
{
	// Setup timeval variable
	struct timeval timeout;
	struct fd_set fds;

	timeout.tv_sec = sec;
	timeout.tv_usec = usec;
	// Setup fd_set structure
	FD_ZERO(&fds);
	FD_SET(socket, &fds);
	// Return value:
	// -1: error occurred
	// 0: timed out
	// > 0: data ready to be read
	return select(0, &fds, 0, 0, &timeout);
}

int Protocol::MoveBaseToNAK(deque<Packet> window,ACKNAK acknak)
{
	int sequence = acknak.sequencenumber;
	base = sequence;//set base to this NAK sequence
	return base;//return base
}

//sequence number for the last packet of file
int Protocol::GetLastFrameSeq(int sequenceMax,int total)
{
	int i = total%(sequenceMax)+1;//if last packet is 4, then base should move to 5 to say 4 has been received
	return i;
}

//if base equal last frame seq, then sender recv all ACK from receiver
bool Protocol::CheckIfSentOver(int base,int sequenceMax,int total)
{
	return base==GetLastFrameSeq(sequenceMax,total);
}

int Protocol::MoveWindowForNextPacket(deque<Packet> window,ACKNAK acknak)
{
	//pop the first packet in window and push_back new packet
	window.pop_front();
	PutFileToWindow(&fin);
	MoveBase(acknak);
	return 0;
}

int Protocol::MoveBase(ACKNAK acknak)
{
	base = acknak.sequencenumber+1;
	if(base > sequenceMax)
		base = 1;
	return base;
}

int Protocol::SendNewFrameOfWindow(SOCKET socket,deque<Packet> window,SOCKADDR_IN dst)
{
	Packet packet = window.back();
	int nbytes = sendto(socket,(char*)&packet,sizeof(packet),0,(struct sockaddr*)&dst,sizeof(dst));
	return nbytes;
}

int Protocol::GetPacketPositionInWindowByAck(deque<Packet> window,ACKNAK acknak)
{
	int sequence = acknak.sequencenumber;
	int count = 0;
	deque<Packet>::iterator it = window.begin();
	for(;it!=window.end();it++)
	{
		if(it->sequencenumber==sequence)
			break;
		count++;
	}
	return count;//if ack is not in the window count is windowsize
}

set<int> Protocol::GetPreviousACKNAKInWindow(deque<Packet> window,ACKNAK acknak)
{
	set<int> myset;
	int sequence = acknak.sequencenumber;
	deque<Packet>::iterator it = window.begin();
	for(;it!=window.end();it++)
	{
		if(it->sequencenumber!=sequence)
			myset.insert(it->sequencenumber);
		else
			break;
	}
	return myset;
}


int Protocol::Protocol_Implementation(FILE *Fin, int Total, SOCKET socket, SOCKADDR_IN dst, SOCKADDR_IN from, int w_size)
{
	int fromlen = sizeof(from);
	initial();
	windowsize = w_size;
	sequenceMax = 2 *windowsize +1;
	fin = *Fin;
	lastframesequence = GetLastFrameSeq(sequenceMax,Total);
	numberoftotalpacket = Total;
	initialWindow(&fin,Total);
	numberofsentpacket += SendWindow(socket,Window,dst);
	printf("send inital window!\n");
	//int count = -1; //temp variable for sequence number in window
	set<int> checkset;//check if ack is previous ack in window
	int NAKcheckset = 0;
	ACKNAK acknak;
	ZeroMemory(&acknak,sizeof(acknak));
	int ready; 
	struct timeval2 starttime_ACK;
	struct timeval2 starttime_NAK;
	ZeroMemory(&starttime_NAK,sizeof(starttime_NAK));
	ZeroMemory(&starttime_ACK,sizeof(starttime_ACK));
	ready = SetTimeout(socket,0,300000);//set time out for first packet in window
	while(1)
	{

		if(ready>0)
		{
			recvfrom(socket,(char*)&acknak,sizeof(acknak),0,(struct sockaddr*)&from,&fromlen);
			if(acknak.type ==PROTOCOL_NAK)
			{
				if(NAKcheckset!=acknak.sequencenumber)
				{
					printf("Sender receive NAK, number is %d\n",acknak.sequencenumber);
					NAKcheckset = acknak.sequencenumber;
					int move = MoveWindowToNAK(Window,acknak);
					if (numberofsentpacket != numberoftotalpacket)
					{
						numberofsentpacket += move;//calculate sent because new frame has been sent here.
					}
					printwindow(Window);
					//MoveBaseToNAK(Window,acknak);
					SendWindow(socket,Window,dst);
					ready = SetTimeout(socket,0,300000);
					//start recording current time
					//ZeroMemory(&starttime_NAK,sizeof(starttime_NAK));
					gettimeofday(&starttime_NAK,NULL);
				}
				else
				{
					struct timeval timeout;
					get_timeout(starttime_NAK,&timeout);
					ready = SetTimeout(socket,timeout.tv_sec,timeout.tv_usec);
					printf("Sender receive NAK , number is %d\n",acknak.sequencenumber);
					continue;
				}
			}
			if(acknak.type ==PROTOCOL_ACK)
			{
				int p = GetPacketPositionInWindowByAck(Window,acknak);
				if((checkset.find(acknak.sequencenumber)==checkset.end())&&p!=Window.size())//means cant find it and its post ACK
				{
					checkset = GetPreviousACKNAKInWindow(Window,acknak);
					if(p+1>numberoftotalpacket-numberofsentpacket)//p+1 is the number of move 
					{//still pop before and including that ACK
						printf("Sender receive ACK %d, and window is close to end, position is %d\n ",acknak.sequencenumber,p);
						for (int i = 0; i < p + 1; i++)
						{
							Window.pop_front();
						}
						for(int j=0;j<numberoftotalpacket-numberofsentpacket;j++)
						{
							printf("Sender move window and send packet %d\n",nextsequence);
							PutFileToWindow(&fin);
							SendNewFrameOfWindow(socket,Window,dst);
							printwindow(Window);
							numberofsentpacket++;
						}
						ready = SetTimeout(socket,0,300000);
						//ZeroMemory(&starttime_ACK,sizeof(starttime_ACK));
						gettimeofday(&starttime_ACK,NULL);
					}
					else
					{
						printf("Sender receive ACK %d , position is %d\n",acknak.sequencenumber,p);
						for(int i = 0;i<p+1;i++)
						{
							Window.pop_front();
							printf("Sender move window and send packet %d\n",nextsequence);
							PutFileToWindow(&fin);
							SendNewFrameOfWindow(socket,Window,dst);
							printwindow(Window);
							numberofsentpacket++;
						}
						ready = SetTimeout(socket,0,300000);
						//ZeroMemory(&starttime_ACK,sizeof(starttime_ACK));
						gettimeofday(&starttime_ACK,NULL);
					}
					int b = MoveBase(acknak);
					printf("base : %d last :%d total :%d sent :%d\n",b,lastframesequence,numberoftotalpacket,numberofsentpacket);
					if((numberoftotalpacket==numberofsentpacket)&&(b==lastframesequence))
						break;//sent over
				}
				else//if ack 1 received after ack 2 , ignore
				{
					struct timeval timeout;
					get_timeout(starttime_ACK,&timeout);
					ready = SetTimeout(socket,timeout.tv_sec,timeout.tv_usec);
					printf("Sender receive ACK %d but do nothing\n",acknak.sequencenumber);
					continue;
				}
			}

		}
		else
		{
			/*
			Here need to be modified, send only all the unack packet in window instead of sending whole window
			*/
			//timeout,resend the whole window
			SendWindow(socket,Window,dst);
			ready = SetTimeout(socket,0,300000);
			printf("timeout and resend window\n");
			printwindow(Window);
		}
	}
	printf("gbn sent over\n");
	return 0;
}

///////////////////////////////////////////////////////
//RECEIVE//////////////////////////////////////////

int Protocol::Receive(FILE *fin,SOCKET socket,SOCKADDR_IN from,int totalpacketnumber,int w_size)
{
	initial();
	windowsize = w_size;
	sequenceMax = 2 * windowsize + 1;
	numberoftotalpacket = totalpacketnumber;
	int recv_packet = 0;
	Packet packet;
	ACKNAK acknak;
	ZeroMemory(&packet,sizeof(packet));
	ZeroMemory(&acknak,sizeof(acknak));
	int fromlen = sizeof(from);
	while(1)
	{
		recvfrom(socket,(char*)&packet,sizeof(packet),0,(struct sockaddr*)&from,&fromlen);
		if(packet.sequencenumber==nextsequence)
		{
			acknak.sequencenumber = packet.sequencenumber;
			acknak.type = PROTOCOL_ACK;
			printf("receive correct packet %d\n",packet.sequencenumber);
			IncreaseSequence();//receive successfully then increase next expect sequence
			recv_packet++;
			fwrite(packet.data,sizeof(char),packet.length,fin);
			printf("Received packet : %d\n",recv_packet);
		}
		else
		{
			acknak.sequencenumber = nextsequence;
			acknak.type = PROTOCOL_NAK;
			printf("receive wrong packet , number is %d , expecting is %d\n",packet.sequencenumber,nextsequence);
			//dont need to increase nextsequence
		}
		sendto(socket,(char*)&acknak,sizeof(acknak),0,(struct sockaddr*)&from,fromlen);
		if(totalpacketnumber==recv_packet)
		{
			for(int i=0;i<10;i++)
			{
				int r = SetTimeout(socket,0,300000);
				if(r>0)
				{
					//recvfrom(socket,(char*)&packet,sizeof(packet),0,(struct sockaddr*)&from,&fromlen);
					sendto(socket,(char*)&acknak,sizeof(acknak),0,(struct sockaddr*)&from,fromlen);
				}
			}
			//sendto(socket,(char*)&acknak,sizeof(acknak),0,(struct sockaddr*)&from,fromlen);
			break;
		}
	}
	printf("receive over\n");
	return 0;
}
