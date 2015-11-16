#include "Server.h"


vector<string> get_all_files_names_within_folder();
char* substr(char* arr, int begin, int len)
{
	char* res = new char[len];
	for (int i = 0; i < len; i++)
		res[i] = *(arr + begin + i);
	res[len] = 0;
	return res;
}
TcpServer::TcpServer()
{
	//ofstream fout;			//log file
	fn="serverlog.txt";//log file

	numberofpacket = 0;
	numberofbytesent = 0;
	WSADATA wsadata;
	if (WSAStartup(0x0202,&wsadata)!=0)
		TcpServer::err_sys("Starting WSAStartup() error\n");

	//Display name of local host
	if(gethostname(servername,HOSTNAME_LENGTH)!=0) //get the hostname
		TcpServer::err_sys("Get the host name error,exit");

	printf("Server: %s waiting to be contacted for time/size request...\n",servername);


	//Create the server socket
	if ((serverSock = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0)
		TcpServer::err_sys("Create socket error,exit");

	//Fill-in Server Port and Address info.
	ServerPort=REQUEST_PORT;
	memset(&ServerAddr, 0, sizeof(ServerAddr));      /* Zero out structure */
	ServerAddr.sin_family = AF_INET;                 /* Internet address family */
	ServerAddr.sin_addr.s_addr = htonl(INADDR_ANY);  /* Any incoming interface */
	ServerAddr.sin_port = htons(ServerPort);         /* Local port */

	//Bind the server socket
	if (bind(serverSock, (struct sockaddr *) &ServerAddr, sizeof(ServerAddr)) < 0)
		TcpServer::err_sys("Bind socket error,exit");

}

TcpServer::~TcpServer()
{
	WSACleanup();
}

//////////////////////////////TcpServer Class //////////////////////////////////////////
void TcpServer::err_sys(char * fmt,...)
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
unsigned long TcpServer::ResolveName(char name[])
{
	struct hostent *host;            /* Structure containing host information */

	if ((host = gethostbyname(name)) == NULL)
		err_sys("gethostbyname() failed");

	/* Return the binary, network byte ordered address */
	return *((unsigned long *) host->h_addr_list[0]);
}

//receive duplicate control packet
void TcpServer::RecvDupCp(SOCKET socket,ControlPacket* cp,int len)
{
	for(int i=0;i<10;i++)
	{
		int r = TimeOutUDP(socket,0,300000);
		if(r>0)
		{
			recvfrom(socket,(char*)cp,len,0,(struct sockaddr*)&ClientAddr,&clientLen);
			ACK ack;
			ack.sequence = cp->sequence;
			sendto(socket,(char*)&ack,sizeof(ack),0,(struct sockaddr*)&ClientAddr,clientLen);
		}
		else
			continue;
	}
}

void TcpServer::RecvDuppacket(SOCKET socket,char* p,int len)
{
	for(int i=0;i<10;i++)
	{
		int r = TimeOutUDP(socket,0,300000);
		if(r>0)
			recvfrom(socket,p,len,0,(struct sockaddr*)&ClientAddr,&clientLen);
		else
			continue;
	}
}

void TcpServer::start() 
{
	handshake();
	ControlPacket controlpacket;
	ZeroMemory(controlpacket.filename,FILENAME_LENGTH);
	recv_pct(serverSock,&controlpacket,sizeof(controlpacket));
	if(controlpacket.type==POST)                         
	{
		//recv duplicated control packet from sender
		ControlPacket dupcp;
		RecvDupCp(serverSock,&dupcp,sizeof(dupcp));//wait 1.5seconds for duplicate packet

		FILE *pfile;
		pfile = fopen(controlpacket.filename,"wb");
		GBNProtocol *gbn = new GBNProtocol();
		int total = (controlpacket.filesize/FILECHUNK + (controlpacket.filesize%FILECHUNK !=0?1:0));
		int w_size = 4;
		gbn->GBN_Receive(pfile,serverSock,ClientAddr,total,w_size);
		fclose(pfile);
		printf("receiving completes...");
		system("pause");

	}
	if(controlpacket.type==GET)
	{
		//avoid last ack drop
		for(int i=0;i<10;i++)
		{
			int t = TimeOutUDP(serverSock,0,300000);
			if(t<0)
			{printf("select function error.");}
			else if(t==1)
			{
				recvfrom(serverSock,(char *)&controlpacket ,sizeof(controlpacket),0,(struct sockaddr*)&ClientAddr,&clientLen);
				if(controlpacket.sequence != nextrecvSeq)
				{
					ACK ack;
					ack.sequence = controlpacket.sequence;//if expect 1 but recv 0 ,then send ack0;
					sendto(serverSock,(char*)&ack,sizeof(ack),0,(struct sockaddr*)&ClientAddr,clientLen);
				}
			}
			else
				continue;
		}

		struct _stat stat_buf;
		int result;
		if((result = _stat(controlpacket.filename,&stat_buf))!=0)
		{
			strcpy(controlpacket.filename,"null");
			controlpacket.sequence = nextsendSeq;
			send_pct(serverSock,&controlpacket,sizeof(controlpacket));
			printf("No such file,exits!\n");
		}
		else
		{
			ifstream file(controlpacket.filename,ios::binary);
			file.seekg(0,ios::end);
			unsigned int fileSize = file.tellg();
			file.close();
			ControlPacket cp;
			strcpy(cp.filename,controlpacket.filename);
			cp.filesize = fileSize;
			cp.sequence = nextsendSeq;
			printf("control packet ready\n");
			send_pct(serverSock,&cp,sizeof(cp));
			printf("control packet done\n");
			//handle duplicate ack
			char dup[100];
			RecvDuppacket(serverSock,dup,sizeof(dup));
			//////////GBN sending/////////////////////////
			FILE * pfile;
			pfile = fopen(cp.filename,"rb");
			int total = (fileSize/FILECHUNK + (fileSize%FILECHUNK !=0?1:0));
			int w_size = 4;
			GBNProtocol *gbn = new GBNProtocol();
			gbn->Protocol(pfile,total,serverSock,ClientAddr,ClientAddr,w_size);
			fclose(pfile);
			printf("sending file completed..\n");
			RecvDuppacket(serverSock,dup,sizeof(dup));
		}
	}
	if (controlpacket.type == LIST)
	{
		//avoid last ack drop
		for(int i=0;i<10;i++)
		{
			int t = TimeOutUDP(serverSock,0,300000);
			if(t<0)
			{printf("select function error.");}
			else if(t==1)
			{
				recvfrom(serverSock,(char *)&controlpacket ,sizeof(controlpacket),0,(struct sockaddr*)&ClientAddr,&clientLen);
				if(controlpacket.sequence != nextrecvSeq)
				{
					ACK ack;
					ack.sequence = controlpacket.sequence;//if expect 1 but recv 0 ,then send ack0;
					sendto(serverSock,(char*)&ack,sizeof(ack),0,(struct sockaddr*)&ClientAddr,clientLen);
				}
			}
			else
				continue;
		}

		string filelist;
		Msg listfilebuffer;
		vector<string> listfile = get_all_files_names_within_folder();
		for (vector<string>::iterator it = listfile.begin(); it != listfile.end(); it++)
		{
			filelist.append(*it);
			filelist.append("|");
		}
		//convert from string to char*
		char* filelistarray = new char[filelist.size() + 1];
		filelistarray[filelist.size()] = 0;
		memcpy(filelistarray, filelist.c_str(), filelist.size());
		//put char* into msg buffer
		listfilebuffer.length = filelist.size();
		memset(listfilebuffer.buffer, 0, BUFFER_LENGTH);
		memcpy(listfilebuffer.buffer, filelistarray, filelist.size());
		ControlPacket controlpacketGet;
		controlpacketGet.sequence = nextsendSeq;
		controlpacketGet.filesize = filelist.size();
		printf("ready to send control packet\n");
		send_pct(serverSock, &controlpacketGet, sizeof(controlpacketGet));
		printf("server done control packet\n");
		//handle duplicate ack
		char dup[100];
		RecvDuppacket(serverSock,dup,sizeof(dup));
		DataPacket datapacket;
		datapacket.sequence = nextsendSeq;
		datapacket.length = listfilebuffer.length;
		ZeroMemory(datapacket.data, UDPDATABUFFER);
		//char *p = substr(fileBuffer2, bytesSent, bytesToSend);
		memcpy(datapacket.data, listfilebuffer.buffer, datapacket.length);
		int factsent = send_pct(serverSock, &datapacket);
		printf("server send data to client in get function\n");
		delete[] filelistarray;
		printf("success send server list to client\n");
		memset(dup, '\0', sizeof(dup));
	}

	if(controlpacket.type == DELETE_FILE)
	{
			//avoid last ack drop
		for(int i=0;i<10;i++)
		{
			int t = TimeOutUDP(serverSock,0,300000);
			if(t<0)
			{printf("select function error.");}
			else if(t==1)
			{
				recvfrom(serverSock,(char *)&controlpacket ,sizeof(controlpacket),0,(struct sockaddr*)&ClientAddr,&clientLen);
				if(controlpacket.sequence != nextrecvSeq)
				{
					ACK ack;
					ack.sequence = controlpacket.sequence;//if expect 1 but recv 0 ,then send ack0;
					sendto(serverSock,(char*)&ack,sizeof(ack),0,(struct sockaddr*)&ClientAddr,clientLen);
				}
			}
			else
				continue;
		}

		struct _stat stat_buf;
		int result;
		if((result = _stat(controlpacket.filename,&stat_buf))!=0)
		{
			strcpy(controlpacket.filename,"null");
			controlpacket.sequence = nextsendSeq;
			send_pct(serverSock,&controlpacket,sizeof(controlpacket));
			printf("No such file,exits!\n");
		}
		else
		{
			remove(controlpacket.filename);
			strcpy(controlpacket.filename,"success");
			controlpacket.sequence = nextsendSeq;
			send_pct(serverSock,&controlpacket,sizeof(controlpacket));
			printf("file has benn deleted!\n");

		}

	}
	//close the client socket
	closesocket(serverSock);	


}

void TcpServer::handshake(){
	HandShakeACK send_hsack;
	clientLen = sizeof(ClientAddr);
	int serverRandom = rand()%256 + 10;//random [0..255]
	if(0==(serverRandom % 2))
		nextsendSeq = SEQ0;
	else
		nextsendSeq = SEQ1;
	unsigned int clientRandom;
	recvfrom(serverSock,(char*)&clientRandom,sizeof(unsigned int),0,
		(struct sockaddr*)&ClientAddr,&clientLen);
	//Open the log file
	fout.open(fn);
	if(TRACE)
	{
		fout<<"receive first handshake "<<clientRandom<<endl;
	}
	printf("server receive first handshake from client\n");
	//clientRandom = ntohl(clientRandom);
	send_hsack.ack = clientRandom;
	send_hsack.local_random = serverRandom;
	HandShakeACK recv_hsack;
	while(1)
	{
		
		sendto(serverSock,(char*)&send_hsack,sizeof(send_hsack),0,
			(struct sockaddr*)&ClientAddr,clientLen);
		if(TRACE)
		{
			fout<<"sent second handshake and ack for first handshake "<<serverRandom<<endl;
		}
		printf("send ack and server handshake to client\n");
		recvfrom(serverSock,(char*)&recv_hsack,sizeof(recv_hsack),0,
			(struct sockaddr*)&ClientAddr,&clientLen);
		printf("recv handshake ack from client\n");
		if(recv_hsack.ack==serverRandom)
		{
			if(TRACE)
			{
				fout<<"receive ack for first handshake "<<endl;
				fout<<"handshake complete."<<endl;
			}
			break;
		}
	}
	//recv duplicate handshake packet
	printf("wait for handshake complete.....no more 2s\n");
	for(int i=0;i<10;i++)
	{
		int t = TimeOutUDP(serverSock,0,300000);
		if(t==1)
			recvfrom(serverSock,(char*)&recv_hsack,sizeof(recv_hsack),0,
				(struct sockaddr*)&ClientAddr,&clientLen);
		else
			continue;
	}
	//detect delay handshake packet
	for(int i = 0;i<3;i++)
	{
		int t = TimeOutUDP(serverSock,0,300000);
		if(t==1)
			recvfrom(serverSock,(char*)&recv_hsack,sizeof(recv_hsack),0,
			(struct sockaddr*)&ClientAddr,&clientLen);
		else
			continue;
	}

	if(clientRandom%2==0)
		nextrecvSeq = SEQ0;
	else
		nextrecvSeq = SEQ1;
	printf("nextsend:%d,nextrecv%d\n",nextsendSeq,nextrecvSeq);
	printf("handshake completed..\n");
}

int TcpServer::TimeOutUDP(SOCKET socket,long sec, long usec)
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

int TcpServer::SendACK(SOCKET socket,char * buff)
{
	ACK ack;
	return 0;
}

//receive control packet
int TcpServer::recv_pct(SOCKET socket , ControlPacket * cp , int len)
{
	ACK ack;
	int n = 0;
	while(true)
	{
		//ZeroMemory(cp,len);//empty packet
		n=recvfrom(socket,(char*)cp,len,0,(struct sockaddr*)&ClientAddr,&clientLen);
		printf("recv first control packet from client, file name :%s\n",cp->filename);
		if(cp->sequence!=nextrecvSeq)
		{
			printf("seq number wrong!\n");
			ack.sequence = cp->sequence;//if expect 1 but recv 0 ,then send ack0;
			if(sendto(socket,(char*)&ack,sizeof(ack),0,(struct sockaddr*)&ClientAddr,clientLen)!=sizeof(ack))
			{
				printf("resend control ack error.\n");
			}
			if(TRACE)
			{
				fout<<"receive wrong packet, sent ACK "<<cp->sequence<<endl;
			}
		}
		else //expected packet
		{
			if(TRACE)
			{
				fout<<"receive packet "<<cp->sequence<<endl;
			}
			printf("seq number correct!\n");
			ack.sequence = nextrecvSeq;
			if(sendto(socket,(char*)&ack,sizeof(ack),0,(struct sockaddr*)&ClientAddr,clientLen)!=sizeof(ack))
			{
				printf("send control ack error.\n");
			}
			if(TRACE)
			{
				fout<<"sent ACK for packet  "<<cp->sequence<<endl;
			}
			switchRecvSeq();
			break;
		}
	}
	return n;
}

//receive data packet
int TcpServer::recv_pct(SOCKET socket , DataPacket * dp , int len)
{
	ACK ack;
	while(true)
	{
		int rbytes,n;
		if((n=recvfrom(socket,(char *)dp ,len,0,(struct sockaddr*)&ClientAddr,&clientLen))<=0)
			printf("recvfrom error : %d\n", WSAGetLastError());
		if(dp->sequence != nextrecvSeq)
		{
			ack.sequence = dp->sequence;//if expect 1 but recv 0 ,then send ack0;
			if(sendto(socket,(char*)&ack,sizeof(ack),0,(struct sockaddr*)&ClientAddr,clientLen)!=sizeof(ack))
			{
				printf("resend data ack error.\n");
			}
			if(TRACE)
			{
				fout<<"receive wrong packet, sent ACK "<<dp->sequence<<endl;
			}
		}
		else //expected packet
		{
			if(TRACE)
			{
				fout<<"receive packet "<<dp->sequence<<endl;
			}
			ack.sequence = nextrecvSeq;
			if( sendto(socket,(char*)&ack,sizeof(ack),0,(struct sockaddr*)&ClientAddr,clientLen)!=sizeof(ack))
			{printf("send data ack error.\n");}
			if(TRACE)
			{
				fout<<"sent ACK for packet  "<<dp->sequence<<endl;
			}
			switchRecvSeq();
			break;
		}
	}
	return dp->length;//return real data length(not included header)
}

int TcpServer::send_pct(SOCKET socket, ControlPacket * cp, int len)
{
	ACK ack;
	int n = 0;
	n=sendto(socket,(char*)cp,len,0,(struct sockaddr*)&ClientAddr,clientLen);
	if(TRACE)
	{
		fout<<"sent packet  "<<cp->sequence<<endl;
	}
	while(true)
	{
		int ready = TimeOutUDP(socket,0,300000);//300ms
		if(ready < 0)
		{printf("Select function error.\n");}
		if(ready == 0)
		{
			sendto(socket,(char*)cp,len,0,(struct sockaddr*)&ClientAddr,clientLen);
			if(TRACE)
			{
				fout<<"time out, resent packet  "<<cp->sequence<<endl;
			}
		}
		else
		{
			recvfrom(socket,(char*)&ack,sizeof(ack),0,(struct sockaddr*)&ClientAddr,&clientLen);
			if(ack.sequence == nextsendSeq)
			{
				printf("Receive control ack from client %d\n",ack.sequence);
				switchSendSeq();
				if(TRACE)
				{
					fout<<"receive ACK for packet  "<<cp->sequence<<endl;
				}
				break;
			}
			else
			{
				sendto(socket,(char*)cp,len,0,(struct sockaddr*)&ClientAddr,clientLen);
				if(TRACE)
				{
					fout<<"receive wrong ACK, resent packet  "<<cp->sequence<<endl;
				}
			}
		}
	}
	return n;
}

int TcpServer::send_pct(SOCKET socket, DataPacket * dp)
{
	ACK ack;
	int n= 0;
	if((n=sendto(socket,(char *)dp,DATAPACKETHEAD+dp->length,0,(struct sockaddr*)&ClientAddr,clientLen))!=(DATAPACKETHEAD+dp->length))
		err_sys("Send DATAPACKETHEAD+length Error\n");
	if(TRACE)
	{
		fout<<"sent packet  "<<dp->sequence<<endl;
		numberofpacket++;
		numberofbytesent+=(DATAPACKETHEAD + dp->length);
	}
	while(true)
	{
		int ready = TimeOutUDP(socket,0,300000);//300ms
		if(ready < 0)
		{printf("Select function error.\n");}
		if(ready == 0)
		{
			if((n=sendto(socket,(char *)dp,DATAPACKETHEAD+dp->length,0,(struct sockaddr*)&ClientAddr,clientLen))!=(DATAPACKETHEAD+dp->length))
				err_sys("Send DATAPACKETHEAD+length Error\n");
			if(TRACE)
			{
				fout<<"time out, resent packet  "<<dp->sequence<<endl;
				numberofpacket++;
				numberofbytesent+=(DATAPACKETHEAD + dp->length);
			}
		}
		else
		{
			recvfrom(socket,(char*)&ack,sizeof(ack),0,(struct sockaddr*)&ClientAddr,&clientLen);
			if(ack.sequence == nextsendSeq)
			{
				printf("Receive datapacket ack from client %d\n",ack.sequence);
				switchSendSeq();
				if(TRACE)
				{
					fout<<"receive ACK for packet  "<<dp->sequence<<endl;
				}
				break;
			}
			else
			{
				if((n=sendto(socket,(char *)dp,DATAPACKETHEAD+dp->length,0,(struct sockaddr*)&ClientAddr,clientLen))!=(DATAPACKETHEAD+dp->length))
					err_sys("Send DATAPACKETHEAD+length Error\n");
				if(TRACE)
				{
					fout<<"receive wrong ACK, resent packet  "<<dp->sequence<<endl;
					numberofpacket++;
					numberofbytesent+=(DATAPACKETHEAD + dp->length);
				}
			}
		}
	}
	return n-DATAPACKETHEAD;
}

void TcpServer::switchRecvSeq()
{
	if(nextrecvSeq==SEQ1)
		nextrecvSeq =SEQ0;
	else
		nextrecvSeq = SEQ1;
}

void TcpServer::switchSendSeq()
{
	if(nextsendSeq==SEQ1)
		nextsendSeq =SEQ0;
	else
		nextsendSeq = SEQ1;
}

vector<string> get_all_files_names_within_folder()
{
	//find current directory without executable file name
	int stringsize = 0;
	string filenamestring;
	const unsigned long maxDir = 260;
	char currentDir[maxDir];
	GetCurrentDirectory(maxDir, currentDir);
	string folder = string(currentDir);
	//get list of filename in this directory
	vector<string> names;
	char search_path[200];
	sprintf(search_path, "%s\\*", folder.c_str());
	WIN32_FIND_DATA fd; 
	HANDLE hFind = ::FindFirstFile(search_path, &fd); 
	if(hFind != INVALID_HANDLE_VALUE) 
	{ 
		do 
		{ 
			// read all (real) files in current folder
			// , delete '!' read other 2 default folder . and ..
			if(! (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ) 
			{
				names.push_back(fd.cFileName);
			}
		}while(::FindNextFile(hFind, &fd)); 
		::FindClose(hFind); 
	} 
	return names;
}
////////////////////////////////////////////////////////////////////////////////////////




