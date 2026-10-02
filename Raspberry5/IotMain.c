#include "IotSocket.h"

int main(int argc, char*argv[])
{
    if(argc != 2) 
    {
		printf("Usage : %s <port>\n",argv[0]);
		exit(1);
	}
    StartServer(argv[1]);

}