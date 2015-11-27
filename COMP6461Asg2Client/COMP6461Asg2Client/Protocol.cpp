#include "Protocol.h"


#pragma comment (lib, "Ws2_32.lib")


void printwindow(deque<Packet> window)
{
	printf("   Window : ");
	deque<Packet>::iterator it = window.begin();
	for (; it != window.end(); it++){
		if (it->isAck == PROTOCOL_ACK){
			printf(" %d is acked ", it->sequencenumber);
		}
		else{
			printf(" %d isn't acked ", it->sequencenumber);
		}
	}
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


//sequence number for the last packet of file
int Protocol::GetLastFrameSeq(int sequenceMax,int total)
{
	int i = total%(sequenceMax)+1;//if last packet is 4, then base should move to 5 to say 4 has been received
	return i;
}



int Protocol::SendNewFrameOfWindow(SOCKET socket,deque<Packet> window,SOCKADDR_IN dst)
{
	Packet packet = window.back();
	int nbytes = sendto(socket,(char*)&packet,sizeof(packet),0,(struct sockaddr*)&dst,sizeof(dst));
	return nbytes;
}



int Protocol::Send(FILE *Fin, int Total, SOCKET socket, SOCKADDR_IN dst, SOCKADDR_IN from, int w_size)
{
	int numofackpacket = 0;
	int fromlen = sizeof(from);
	initial();
	windowsize = w_size;
	sequenceMax = 2 * windowsize + 1;
	fin = *Fin;
	lastframesequence = GetLastFrameSeq(sequenceMax, Total);
	numberoftotalpacket = Total;
	initialWindow(&fin, Total);
	numberofsentpacket += SendWindow(socket, Window, dst);
	printf("send inital window!\n");
	int NAKcheckset = 0;
	ACKNAK acknak;
	ZeroMemory(&acknak, sizeof(acknak));
	int ready;
	struct timeval2 starttime_ACK;
	struct timeval2 starttime_NAK;
	ZeroMemory(&starttime_NAK, sizeof(starttime_NAK));
	ZeroMemory(&starttime_ACK, sizeof(starttime_ACK));
	ready = SetTimeout(socket, 0, 300000);//set time out for first packet in window
	while (1)
	{

		if (ready>0)
		{
			recvfrom(socket, (char*)&acknak, sizeof(acknak), 0, (struct sockaddr*)&from, &fromlen);
			if (Window.front().sequencenumber == acknak.sequencenumber){
				if (Window.front().isAck != PROTOCOL_ACK){
					Window.front().isAck = PROTOCOL_ACK;
					numofackpacket++;
				}

				int count = 0;
				for (int cur = 0; cur<Window.size(); count++){
					if (Window[cur].isAck == PROTOCOL_ACK){
						Window.pop_front();
					}
					else{
						break;
					}
				}
				for (int counter = 0; counter < count; counter++){
					if (numofackpacket < numberoftotalpacket){
						PutFileToWindow(&fin);
						SendNewFrameOfWindow(socket, Window, dst);
					}
				}
			}
			else{
				for (auto it = Window.begin(); it != Window.end(); it++){
					if (it->sequencenumber == acknak.sequencenumber&&it->isAck != PROTOCOL_ACK){
						it->isAck = PROTOCOL_ACK;
						numofackpacket++;
					}
				}
			}
			if (numofackpacket >= numberoftotalpacket&&Window.empty()){
				Window.clear();
				break;


			}
			ready = SetTimeout(socket, 0, 3000000);


		}
		else
		{
			SendWindow(socket, Window, dst);
			ready = SetTimeout(socket, 0, 3000000);
			printf("timeout and resend window\n");
			printwindow(Window);
		}
	}
	printf("file sent over\n");
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
	Window.clear();
	while(1)
	{
		recvfrom(socket,(char*)&packet,sizeof(packet),0,(struct sockaddr*)&from,&fromlen);
		//validate packet
		if ((nextsequence + windowsize - 1 <= sequenceMax&&packet.sequencenumber>=nextsequence&&packet.sequencenumber<nextsequence+windowsize)||(nextsequence+windowsize-1)>sequenceMax&&((packet.sequencenumber<(nextsequence+windowsize)%sequenceMax)||packet.sequencenumber>=nextsequence))
		{


			if (Window.size() == 0){
				Window.push_back(packet);
			}
			else{
				if (packet.sequencenumber == nextsequence){
					if (Window.front().sequencenumber != nextsequence){
						Window.push_front(packet);
					}
				}
				else{
					auto it = Window.begin();
					/*
						first condition: window in between the sequence, like:
						1 2 3 | 4 5 6 7 8| 9 or 1 2 3 4 | 5 6 7 8 9 |
						*/
					if ((nextsequence + windowsize - 1) <= sequenceMax){
						if (packet.sequencenumber > nextsequence&&packet.sequencenumber >= Window.front().sequencenumber&&packet.sequencenumber < (nextsequence + windowsize)){
							auto it = Window.begin();
							for (; it != Window.end(); it++){
								if (it->sequencenumber == packet.sequencenumber){
									//Already has the packet in window, do nothing
									break;
								}
								else if ((it + 1) != Window.end() && it->sequencenumber<packet.sequencenumber && (it + 1)->sequencenumber>packet.sequencenumber){
									Window.insert(it+1, packet);
									break;
								}
								else if ((it + 1) == Window.end()){
									if (it->sequencenumber < packet.sequencenumber){
										Window.push_back(packet);
										break;
									}
									else{
										Window.insert(it, packet);
										break;
									}
								}
							}
						}
						else if (packet.sequencenumber < Window.front().sequencenumber&&packet.sequencenumber>nextsequence){
							Window.push_front(packet);
						}
					}
					/*
						second condition window has two half , the end of sequence and the beginning of sequence
						1 2 | 3 4 5 6 |7 8 9 or 1 2 3 | 4 5 6 7 | 8 9
						*/
					else{
						/*
							Judge packet is the smaller half window or bigger half window
							*/
						//caculate the smaller half range and bigger half range
						int smallerBorder = (nextsequence + windowsize) % sequenceMax;
						if (packet.sequencenumber<smallerBorder || packet.sequencenumber>nextsequence){
							//packet is belong to smaller half range
							if (packet.sequencenumber < smallerBorder){
								for (auto it = Window.begin(); it != Window.end(); it++){
									if (it->sequencenumber == packet.sequencenumber){
										//already have this sequence, do nothing
										break;
									}
									if (it->sequencenumber < smallerBorder){
										if (it->sequencenumber > packet.sequencenumber){
											//Insert in front of the it packet
											// 9 1  3    2 will go between 1 and 3 as 9 larger than smaller border and 1 smaller than packet number 
											Window.insert(it, packet);
											break;
										}
										else if ((it + 1) != Window.end() && it->sequencenumber<packet.sequencenumber && (it + 1)->sequencenumber>packet.sequencenumber){
											//insert at next hop location 
											// here is a bit redundant, 9 1 3  2 will go between 1 and 3
											Window.insert(it+1, packet);
											break;
										}
										else if ((it + 1) == Window.end()){
											if (it->sequencenumber > packet.sequencenumber){
												//insert in front of cur it
												// 9 3   2 will go just before 3
												Window.insert(it, packet);
											}
											else{
												Window.push_back(packet);
												break;
											}
										}
									}
								}
							}
							//packet is belong to bigger half range
							else{
								for (auto it = Window.begin(); it != Window.end(); it++){
									if (it->sequencenumber == packet.sequencenumber){
										//already have this packet
										break;
									}
									else if (it->sequencenumber < smallerBorder){
										//insert in front of it 
										Window.insert(it, packet);
										break;
									}
									else if ((it + 1) != Window.end() && it->sequencenumber<packet.sequencenumber && ((it + 1)->sequencenumber>packet.sequencenumber || (it + 1)->sequencenumber < smallerBorder)){
										//insert just after it
										Window.insert(it+1, packet);
										break;
									}
									else if ((it + 1) == Window.end()){
										if (it->sequencenumber < smallerBorder){
											Window.insert(it, packet);
											break;
										}
										else if (it->sequencenumber > packet.sequencenumber){
											Window.insert(it, packet);
											break;
										}
										else{
											Window.push_back(packet);
											break;
										}
									}
								}
							}
						}
					}
				}
			}
		}
			if (Window.size()!=0&&Window.front().sequencenumber == nextsequence&&recv_packet<totalpacketnumber)
			{
				
				acknak.sequencenumber = Window.front().sequencenumber;
				acknak.type = PROTOCOL_ACK;
				printf("receive correct packet %d\n", Window.front().sequencenumber);
				IncreaseSequence();//receive successfully then increase next expect sequence
				recv_packet++;
				fwrite(Window.front().data, sizeof(char), Window.front().length, fin);
				Window.pop_front();
				printf("Received packet : %d\n", recv_packet);
				sendto(socket, (char*)&acknak, sizeof(acknak), 0, (struct sockaddr*)&from, fromlen);
				printwindow(Window);
			}
			else
			{
				int packetInWindowResult = sequenceInWindow(packet.sequencenumber);
				if (packetInWindowResult != -1){
					acknak.sequencenumber = Window[packetInWindowResult].sequencenumber;
					acknak.type = PROTOCOL_ACK;
					sendto(socket, (char*)&acknak, sizeof(acknak), 0, (struct sockaddr*)&from, fromlen);
				}
				else if (nextsequence > windowsize){
					if (packet.sequencenumber >= nextsequence - windowsize&&packet.sequencenumber <= nextsequence - 1){
						acknak.sequencenumber = packet.sequencenumber;
						acknak.type = PROTOCOL_ACK;
						sendto(socket, (char*)&acknak, sizeof(acknak), 0, (struct sockaddr*)&from, fromlen);
					}
				}
				else{
					if (nextsequence - 1> 0){
						if ((packet.sequencenumber >= sequenceMax - abs(nextsequence - windowsize)&&packet.sequencenumber<=sequenceMax) || (packet.sequencenumber>=1&&packet.sequencenumber <= nextsequence - 1)){
							acknak.sequencenumber = packet.sequencenumber;
							acknak.type = PROTOCOL_ACK;
							sendto(socket, (char*)&acknak, sizeof(acknak), 0, (struct sockaddr*)&from, fromlen);
						}
					}
					else{
						if (packet.sequencenumber > sequenceMax - windowsize&&packet.sequencenumber <= sequenceMax){
							acknak.sequencenumber = packet.sequencenumber;
							acknak.type = PROTOCOL_ACK;
							sendto(socket, (char*)&acknak, sizeof(acknak), 0, (struct sockaddr*)&from, fromlen);
						}
					}
				}
			}
			
		
		
		if(totalpacketnumber==recv_packet)
		{
			for(int i=0;i<10;i++)
			{
				int r = SetTimeout(socket,0,300000);
				if(r>0)
				{
					acknak.sequencenumber = i %sequenceMax+1;
					sendto(socket,(char*)&acknak,sizeof(acknak),0,(struct sockaddr*)&from,fromlen);
				}
			}
			break;
		}
	}
	printf("receive over\n");
	return 0;
}
int Protocol::sequenceInWindow(int sequencenumber){
	int index = 0;
	for (; index < Window.size(); index++){
		if (Window[index].sequencenumber == sequencenumber){
			return index;
		}
	}
	return -1;
}