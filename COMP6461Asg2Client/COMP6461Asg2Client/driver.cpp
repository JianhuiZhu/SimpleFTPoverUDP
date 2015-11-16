#include "Client.h"
int main(int argc, char *argv[]) //argv[1]=RouterName argv[2]=filename argv[3]=time/size
{


	while (true)
	{
		TcpClient * tc = new TcpClient();
		tc->run(argc, argv);
		delete tc;
	}
	return 0;
}