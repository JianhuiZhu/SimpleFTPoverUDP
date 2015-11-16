#include "Server.h"

int main(void)
{
	while (1)
	{
		TcpServer ts;
		ts.start();
	}

	return 0;
}