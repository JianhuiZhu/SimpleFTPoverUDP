
#include "Client.h"

int TcpClient::RecvDuppacket(SOCKET socket,char* p,int len)
{
	for(int i=0;i<10;i++)
	{
		int r = TimeOutUDP(socket,0,300000);
		if(r>0)
			recvfrom(socket,p,len,0,(struct sockaddr*)&from,&fromlen);
		else
			continue;
	}
	return 0;
}
char* substr(char* arr, int begin, int len)
{
	char* res = new char[len];
	for (int i = 0; i < len; i++)
		res[i] = *(arr + begin + i);
	res[len] = 0;
	return res;
}
void TcpClient::run(int argc,char * argv[])
{
	numberofpacket = 0;
	numberofbytesent = 0;
	char *fn="clientlog.txt";//log file


	HOSTENT* hp;
	string RouterName,FileName,method;
	char hostname[HOSTNAME_LENGTH];
	printf("Type name of router:");
	getline (cin,RouterName);
	//initialize win socket
	if (WSAStartup(0x0202,&wsadata)!=0)
	{  
		WSACleanup();  
	    err_sys("Error in starting WSAStartup()\n");
	}
	
	//Display name of local host and copy it to the req
	if(gethostname(hostname,HOSTNAME_LENGTH)!=0) //get the hostname
		err_sys("can not get the host name,program exit");
	printf("%s%s\n","Client starting at host:",hostname);
		//Open the log file
	fout.open(fn);
	if(TRACE)
	{
		fout<<"start on host : "<<hostname<<endl;
	}
	//Create the client socket
	if ((sock = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0)
		TcpClient::err_sys("Create socket error,exit");

	//initial receive sock then bind
	RecvPort=RECVPORT;
	memset(&sa_in, 0, sizeof(sa_in));     /* Zero out structure */
	sa_in.sin_family      = AF_INET;             /* Internet address family */
	sa_in.sin_addr.s_addr = htonl(INADDR_ANY);  /* Server IP address */
	sa_in.sin_port        = htons(RecvPort); /* Server port */

	//Bind the UDP port
	if (bind(sock,(LPSOCKADDR)&sa_in,sizeof(sa_in)) == SOCKET_ERROR)
		throw "can't bind the socket1";
	
	//set up router as a destination
	if((hp=gethostbyname(RouterName.c_str())) == NULL) 
		throw "get server name failed\n";
	memset(&RouterAddr,0,sizeof(RouterAddr));
	memcpy(&RouterAddr.sin_addr,hp->h_addr,hp->h_length);
	RouterAddr.sin_family = hp->h_addrtype;   
	RouterAddr.sin_port = htons(ROUTERPORT);//router port


	handshake();
	std::cout << "Type method of transfer:" << std::endl;
	std::cout << "1 --> post :" << endl;
	std::cout << "2 --> get :" << endl;
	std::cout << "3 --> list :" << endl;
	std::cout << "4 --> delete :" << endl;
	getline(cin,method);
	if(strcmp(method.c_str(),"1")==0)
	{
		printf("Type name of file:");
		getline(cin, FileName);
		struct _stat stat_buf;
		int result;
		if((result = _stat(FileName.c_str(),&stat_buf))!=0)
			printf("No such file,exits!\n");
		else
		{
			ifstream file(FileName,ios::binary);
			file.seekg(0,ios::end);
			unsigned int fileSize = file.tellg();
			file.close();
			ControlPacket controlpacket;
			strcpy_s(controlpacket.filename,FileName.c_str());
			controlpacket.type = POST;
			controlpacket.filesize = fileSize;
			controlpacket.sequence = nextsendSeq;
			printf("control packet ready\n");
			send_pct(sock,&controlpacket,sizeof(controlpacket));
			printf("control packet done\n");
			//recv duplicate ack
			char dup[100];
			RecvDuppacket(sock,dup,sizeof(dup));
			//Sleep(1500);//wait for 1.5seconds for receive to finish handling duplicate(synchronize)
		    //////////GBN sending/////////////////////////
			FILE * pfile;
			pfile = fopen(controlpacket.filename,"rb");
			int total = (fileSize/FILECHUNK + (fileSize%FILECHUNK !=0?1:0));
			int w_size = 4;
			Protocol *gbn = new Protocol();
			gbn->Send(pfile,total,sock,RouterAddr,from,w_size);
			fclose(pfile);
		}
	}

	if(strcmp(method.c_str(),"2")==0)
	{
		printf("Type name of file:");
		getline(cin, FileName);
		ControlPacket controlpacket;
		strcpy(controlpacket.filename,FileName.c_str());
		controlpacket.type = GET;
		//controlpacket.filesize = fileSize;
		controlpacket.sequence = nextsendSeq;
		printf("control packet ready\n");
		send_pct(sock,&controlpacket,sizeof(controlpacket));
		printf("control packet done\n");
		//handle duplicate ack 
		char dup[100];
		RecvDuppacket(sock,dup,sizeof(dup));//like clear the socket buffer
		//wait for the msg from server
		ControlPacket cp;
		recv_pct(sock,&cp,sizeof(cp));//overwrite previous cp struct is ok.
		//avoid last ack drop
		for(int i=0;i<10;i++)
		{
			int t = TimeOutUDP(sock,0,300000);
			if(t<0)
			{printf("select function error.");}
			else if(t==1)
			{
				recvfrom(sock,(char *)&controlpacket ,sizeof(controlpacket),0,(struct sockaddr*)&from,&fromlen);
				if(controlpacket.sequence != nextrecvSeq)
				{
					ACK ack;
					ack.sequence = controlpacket.sequence;//if expect 1 but recv 0 ,then send ack0;
					sendto(sock,(char*)&ack,sizeof(ack),0,(struct sockaddr*)&RouterAddr,RouterAddrLen);
				}
			}
			else
				continue;
		}

		if(strcmp(cp.filename,"null")==0)
		{
			printf("The file you require does not exist on server side!.\n");
		}
		else//prepare to recv data 
		{
			FILE *pfile;
			pfile = fopen(cp.filename,"wb");
			Protocol *gbn = new Protocol();
			int total = (cp.filesize/FILECHUNK + (cp.filesize%FILECHUNK !=0?1:0));
			int w_size = 4;
			gbn->Receive(pfile,sock,from,total,w_size);
			fclose(pfile);
			printf("receiving completes...");
			system("pause");
		}

	}
	
	if (strcmp(method.c_str(), "3") == 0)
	{
		ControlPacket controlpacket;
		strcpy(controlpacket.filename, FileName.c_str());
		controlpacket.type = LIST;
		//controlpacket.filesize = fileSize;
		controlpacket.sequence = nextsendSeq;
		printf("control packet ready\n");
		send_pct(sock, &controlpacket, sizeof(controlpacket));
		//handle duplicate ack 
		char dup[100];
		RecvDuppacket(sock,dup,sizeof(dup));//like clear the socket buffer

		ControlPacket cp;
		recv_pct(sock, &cp, sizeof(cp));
		printf("control packet done\n");
		DataPacket listfilebuffer;
		ZeroMemory(listfilebuffer.data, UDPDATABUFFER);
		//memset(listfilebuffer.buffer, 0, BUFFER_LENGTH);
		
		if (recv_pct(sock, &listfilebuffer,sizeof(listfilebuffer)) != cp.filesize)
			err_sys("receive list file buffer failed,exit");
		//split buffer with "|"
		printf("This is the file name from server:\n");
		char* result;
		result = strtok(listfilebuffer.data, "|");
		while (result != NULL)
		{
			printf("%s\n", result);
			result = strtok(NULL, "|");
		}
		//avoid last ack drop
		for(int i=0;i<10;i++)
		{
			int t = TimeOutUDP(sock,0,300000);
			if(t<0)
			{printf("select function error.");}
			else if(t==1)
			{
				recvfrom(sock,(char *)&listfilebuffer ,sizeof(listfilebuffer),0,(struct sockaddr*)&from,&fromlen);
				if(listfilebuffer.sequence != nextrecvSeq)
				{
					ACK ack;
					ack.sequence = listfilebuffer.sequence;//if expect 1 but recv 0 ,then send ack0;
					sendto(sock,(char*)&ack,sizeof(ack),0,(struct sockaddr*)&RouterAddr,RouterAddrLen);
				}
			}
			else
				continue;
		}
	}

	if(strcmp(method.c_str(), "4") == 0)
	{
		ControlPacket controlpacket;
		strcpy(controlpacket.filename, FileName.c_str());
		controlpacket.type = DELETE_FILE;
		controlpacket.sequence = nextsendSeq;
		printf("control packet ready\n");
		send_pct(sock,&controlpacket,sizeof(controlpacket));
		printf("control packet done\n");
		//wait for the msg from server
		ControlPacket cp;
		recv_pct(sock,&cp,sizeof(cp));//overwrite previous cp struct is ok.
		if(strcmp(cp.filename,"null")==0)
		{
			printf("The file you require does not exist on server side!.\n");
		}
		else
		{
			printf("The file has been deleted.\n");
		}
		//avoid last ack drop
		for(int i=0;i<10;i++)
		{
				int t = TimeOutUDP(sock,0,300000);
				if(t<0)
				{printf("select function error.");}
				else if(t==1)
				{
					recvfrom(sock,(char *)&cp ,sizeof(cp),0,(struct sockaddr*)&from,&fromlen);
					if(cp.sequence != nextrecvSeq)
					{
						ACK ack;
						ack.sequence = cp.sequence;//if expect 1 but recv 0 ,then send ack0;
						sendto(sock,(char*)&ack,sizeof(ack),0,(struct sockaddr*)&RouterAddr,RouterAddrLen);
					}
				}
				else
					continue;
		}
	}
	//close the client socket
	closesocket(sock);

}
TcpClient::~TcpClient()
{
	/* When done uninstall winsock.dll (WSACleanup()) and exit */ 
	WSACleanup();  
}


void TcpClient::err_sys(char * fmt,...) //from Richard Stevens's source code
{     
	perror(NULL);
	va_list args;
	va_start(args,fmt);
	fprintf(stderr,"error: ");
	vfprintf(stderr,fmt,args);
	fprintf(stderr,"\n");
	va_end(args);
	exit(1);
}

unsigned long TcpClient::ResolveName(char name[])
{
	struct hostent *host;            /* Structure containing host information */
	
	if ((host = gethostbyname(name)) == NULL)
		err_sys("gethostbyname() failed");
	
	/* Return the binary, network byte ordered address */
	return *((unsigned long *) host->h_addr_list[0]);
}

/*
msg_recv returns the length of bytes in the msg_ptr->buffer,which have been recevied successfully.
*/
int TcpClient::msg_recv(int sock,Msg * msg_ptr)
{
	int rbytes,n;
	
	for(rbytes=0;rbytes<MSGHDRSIZE;rbytes+=n)
		if((n=recv(sock,(char *)msg_ptr+rbytes,MSGHDRSIZE-rbytes,0))<=0)
			err_sys("Receive MSGHDR Error");
	
	for(rbytes=0;rbytes<msg_ptr->length;rbytes+=n)
		if((n=recv(sock,(char *)msg_ptr->buffer+rbytes,msg_ptr->length-rbytes,0))<=0)
			err_sys( "Receive Buffer Error");
	
	return msg_ptr->length;
}

/* msg_send returns the length of bytes in msg_ptr->buffer,which have been sent out successfully
   MSGHDRSIZE = sizeof(enum)+sizeof(int) = 4 + 4 = 8 bytes (sizeof header)
   sizeof(*msg_ptr) = MSGHDRSIZE+msg_ptr->length
   if n!= MSGHDRSIZE+msg_ptr->length, then sending fail
 */
int TcpClient::msg_send(int sock,Msg * msg_ptr)
{
	int n;
	if((n=send(sock,(char *)msg_ptr,MSGHDRSIZE+msg_ptr->length,0))!=(MSGHDRSIZE+msg_ptr->length))
		err_sys("Send MSGHDRSIZE+length Error");
	return (n-MSGHDRSIZE);//return effective length of data(no header)
	
}

void TcpClient::handshake(){
	HandShakeACK recv_hsack,send_hsack;
	RouterAddrLen = sizeof(RouterAddr);
	fromlen = sizeof(from);
	int clientRandom = rand()%256;//random [0..255]
	if(0==(clientRandom % 2))
		nextsendSeq = SEQ0;
	else
		nextsendSeq = SEQ1;
	unsigned int serverRandom;
	sendto(sock,(char*)&clientRandom,sizeof(unsigned int),0,
		(struct sockaddr*)&RouterAddr,RouterAddrLen);
	if(TRACE)
	{
		fout<<"sent first handshake :"<<clientRandom<<endl;
	}
	printf("send client first handshake to server\n");
	while(true)
	{
		int ready = TimeOutUDP(sock,0,300000);//300ms
		if(ready < 0)
		{printf("%d\n",WSAGetLastError());break;}
		else if(ready == 0)
		{
			if(TRACE)
			{
				fout<<"timeout!"<<endl;
			}
			sendto(sock,(char*)&clientRandom,sizeof(unsigned int),0,
				(struct sockaddr*)&RouterAddr,RouterAddrLen);//timeout and resend
			if(TRACE)
			{
				fout<<"resend first handshake :"<<clientRandom<<endl;
			}
			printf("recv ack time out, resend first handshake\n");
		}
		else if(ready==1)
		{
			recvfrom(sock,(char*)&recv_hsack,sizeof(recv_hsack),0,(struct sockaddr*)&from,&fromlen);
			if(TRACE)
			{
				fout<<"receive ACK for first handshake "<<endl;
			}
			printf("recv handshake ack from server\n");
			send_hsack.ack = recv_hsack.local_random;
			sendto(sock,(char*)&send_hsack,sizeof(send_hsack),0,(struct sockaddr*)&RouterAddr,RouterAddrLen);
			if(TRACE)
			{
				fout<<"sent ACK for last handshake"<<endl;
				fout<<"handshake complete."<<endl;
			}
			printf("send client handshake ack to server\n");
			break;//not timeout 
		}
	}
	printf("wait for handshake complete.....no more 2s\n");
	//avoid last packet drop
	for(int i=0;i<10;i++)
	{
		sendto(sock,(char*)&send_hsack,sizeof(send_hsack),0,(struct sockaddr*)&RouterAddr,RouterAddrLen);
	}
	serverRandom = recv_hsack.local_random;//get server random from recv packet
	if(serverRandom%2==0)
		nextrecvSeq = SEQ0;
	else
		nextrecvSeq = SEQ1;
	printf("nextsend:%d,nextrecv%d\n",nextsendSeq,nextrecvSeq);
	printf("handshake completed...\n");
}

int TcpClient::TimeOutUDP(SOCKET socket,long sec, long usec)
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

int TcpClient::send_pct(SOCKET socket , ControlPacket * cp , int len)
{
	ACK ack;
	int n = 0;
	n=sendto(socket,(char*)cp,len,0,(struct sockaddr*)&RouterAddr,RouterAddrLen);
	if(TRACE)
	{
		fout<<"sent packet "<<cp->sequence<<endl;
	}
	while(true)
	{
		int ready = TimeOutUDP(socket,0,300000);//300ms
		if(ready < 0)
		{printf("Select function error.");}
		if(ready == 0)
		{
			sendto(socket,(char*)cp,len,0,(struct sockaddr*)&RouterAddr,RouterAddrLen);
			if(TRACE)
			{
				fout<<"time out , resent packet "<<cp->sequence<<endl;
			}
		}
		else
		{
			recvfrom(socket,(char*)&ack,sizeof(ack),0,(struct sockaddr*)&from,&fromlen);
			if(ack.sequence == nextsendSeq)
			{
				if(TRACE)
				{
					fout<<"receive ACK for packet "<<cp->sequence<<endl;
				}
				printf("Receive control ack from server %d\n",ack.sequence);
				switchSendSeq();
				break;
			}
			else
			{
				sendto(socket,(char*)cp,len,0,(struct sockaddr*)&RouterAddr,RouterAddrLen);
				if(TRACE)
				{
					fout<<"Receive wrong ACK, resent packet : "<<cp->sequence<<endl;
				}
			}
		}
	}
	return n;
}

int TcpClient::send_pct(SOCKET socket, DataPacket *dp)
{
	ACK ack;
	int n= 0;
	if((n=sendto(socket,(char *)dp,DATAPACKETHEAD+dp->length,0,(struct sockaddr*)&RouterAddr,RouterAddrLen))!=(DATAPACKETHEAD+dp->length))
		err_sys("Send DATAPACKETHEAD+length Error");
	if(TRACE)
	{
		fout<<"sent packet "<<dp->sequence<<endl;
		numberofpacket++;
		numberofbytesent+=(DATAPACKETHEAD + dp->length);
	}
	while(true)
	{
		int ready = TimeOutUDP(socket,0,300000);//300ms
		if(ready < 0)
		{printf("Select function error.");}
		if(ready == 0)
		{
			if((n=sendto(socket,(char *)dp,DATAPACKETHEAD+dp->length,0,(struct sockaddr*)&RouterAddr,RouterAddrLen))!=(DATAPACKETHEAD+dp->length))
				err_sys("Send DATAPACKETHEAD+length Error");
			if(TRACE)
			{
				fout<<"time out, resent packet "<<dp->sequence<<endl;
				numberofpacket++;
				numberofbytesent+=(DATAPACKETHEAD + dp->length);
			}
		}
		else
		{
			recvfrom(socket,(char*)&ack,sizeof(ack),0,(struct sockaddr*)&from,&fromlen);
			if(ack.sequence == nextsendSeq)
			{
				if(TRACE)
				{
					fout<<"receive ACK for packet "<<dp->sequence<<endl;
				}
				printf("Receive datapacket ack from server %d\n",ack.sequence);
				switchSendSeq();
				break;
			}
			else
			{
				if((n=sendto(socket,(char *)dp,DATAPACKETHEAD+dp->length,0,(struct sockaddr*)&RouterAddr,RouterAddrLen))!=(DATAPACKETHEAD+dp->length))
					err_sys("Send DATAPACKETHEAD+length Error");
				if(TRACE)
				{
					fout<<"receive wrong ACK ,resent packet "<<dp->sequence<<endl;
					numberofpacket++;
					numberofbytesent+=(DATAPACKETHEAD + dp->length);
				}
			}
		}
	}
	return n-DATAPACKETHEAD;
}

int TcpClient::recv_pct(SOCKET socket, ControlPacket *cp, int len)
{
	ACK ack;
	int n = 0;
	while(true)
	{
		n=recvfrom(socket,(char*)cp,len,0,(struct sockaddr*)&from,&fromlen);

		printf("recv control packet from server, file size :%d\n",cp->filesize);
		if(cp->sequence!=nextrecvSeq)
		{
			printf("worng seq!\n");
			ack.sequence = cp->sequence;//if expect 1 but recv 0 ,then send ack0;
			if(sendto(socket,(char*)&ack,sizeof(ack),0,(struct sockaddr*)&RouterAddr,RouterAddrLen)!=sizeof(ack))
			{
				printf("resend control ack error.\n");
			}
			if(TRACE)
			{
				fout<<"receive wrong packet ,sent ACK "<<cp->sequence<<endl;
			}
		}
		else //expected packet
		{
			if(TRACE)
			{
				fout<<"receive packet "<<cp->sequence<<endl;
			}
			printf("correct seq!\n");
			ack.sequence = nextrecvSeq;
			if(sendto(socket,(char*)&ack,sizeof(ack),0,(struct sockaddr*)&RouterAddr,RouterAddrLen)!=sizeof(ack))
			{
				printf("send control ack error.\n");
			}
			if(TRACE)
			{
				fout<<"sent ACK for packet "<<cp->sequence<<endl;
			}
			switchRecvSeq();
			break;
		}
	}
	return n;
}

int TcpClient::recv_pct(SOCKET socket, DataPacket * dp,int len)
{
	ACK ack;
	while(true)
	{
		int rbytes,n;
		if((n=recvfrom(socket,(char *)dp ,len,0,(struct sockaddr*)&from,&fromlen))<=0)
			printf("recvfrom error : %d\n", WSAGetLastError());
		if(dp->sequence != nextrecvSeq)
		{
			ack.sequence = dp->sequence;//if expect 1 but recv 0 ,then send ack0;
			if(sendto(socket,(char*)&ack,sizeof(ack),0,(struct sockaddr*)&RouterAddr,RouterAddrLen)!=sizeof(ack))
			{
				printf("resend data ack error.\n");
			}
			if(TRACE)
			{
				fout<<"receive wrong packet ,sent ACK "<<dp->sequence<<endl;
			}
		}
		else //expected packet
		{
			if(TRACE)
			{
				fout<<"receive packet "<<dp->sequence<<endl;
			}
			ack.sequence = nextrecvSeq;
			if( sendto(socket,(char*)&ack,sizeof(ack),0,(struct sockaddr*)&RouterAddr,RouterAddrLen)!=sizeof(ack))
			{
				printf("send data ack error.\n");
			}
			if(TRACE)
			{
				fout<<"sent ACK for packet "<<dp->sequence<<endl;
			}
			switchRecvSeq();
			break;
		}
	}
	return dp->length;//return real data length(not included header)
}

void TcpClient::switchRecvSeq()
{
	if(nextrecvSeq==SEQ1)
		nextrecvSeq =SEQ0;
	else
		nextrecvSeq = SEQ1;
}

void TcpClient::switchSendSeq()
{
	if(nextsendSeq==SEQ1)
		nextsendSeq =SEQ0;
	else
		nextsendSeq = SEQ1;
}


