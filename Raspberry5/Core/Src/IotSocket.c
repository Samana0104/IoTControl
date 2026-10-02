#include "IotSocket.h"

void StartServer(int port)
{

    int servSock, clntSock;
    struct sockaddr_in servAdr, clntAdr;
	int sockOption = 1;
	int clntAdrSz;
	static clntCnt = 0;
	pthread_t tId[MAX_CLNT] = {0};

	ClientInfo * clientInfo = (ClientInfo *)calloc(sizeof(ClientInfo),MAX_CLNT);


	servSock = socket(PF_INET, SOCK_STREAM, 0);

    memset(&servAdr, 0, sizeof(servAdr));
	servAdr.sin_family=AF_INET;
	servAdr.sin_addr.s_addr=htonl(INADDR_ANY);
	servAdr.sin_port=htons(atoi(port));

	setsockopt(servSock, SOL_SOCKET, SO_REUSEADDR, (void*)sockOption, sizeof(sockOption));

	if(bind(servSock, (struct sockaddr *)&servAdr, sizeof(servAdr))==-1)
		error_handling("bind() error");

	if(listen(servSock, 5) == -1)
		error_handling("listen() error");

	while(1) 
	{
		clntAdrSz = sizeof(clntAdr);
		clntSock = accept(servSock, (struct sockaddr *)&clntAdr, &clntAdrSz);
		if(clntCnt >= MAX_CLNT)
		{
				printf("socket full\n");
				shutdown(clntSock,SHUT_WR);
				continue;
		}
		else if(clntSock < 0)
		{
				perror("accept()");
				continue;
		}
		else if(clntSock == 0)
		{
			strcpy(clientInfo[i].ip,inet_ntoa(clntAdr.sin_addr));
			pthread_mutex_lock(&mutx);
			client_info[i].index = i; 
			client_info[i].fd = clntSock; 
			clntCnt++;
			pthread_mutex_unlock(&mutx);
			sprintf(msg,"[%s] New connected! (ip:%s,fd:%d,sockcnt:%d)\n",pArray[0],inet_ntoa(clntAdr.sin_addr),clntSock,clntCnt);
			log_file(msg);
			write(clntSock, msg,strlen(msg));
			pthread_create(tId+i, NULL, clnt_connection, (void *)(client_info + i));
			pthread_detach(tId[i]);
			break;
		}
		if(i == MAX_CLNT)
		{
				sprintf(msg,"[%s] Authentication Error!\n",pArray[0]);
				write(clntSock, msg,strlen(msg));
				log_file(msg);
				shutdown(clntSock,SHUT_WR);
		}
		else 
			shutdown(clntSock,SHUT_WR);
	}
	
}

void * ConnectClnt(pthread_mutex_t *mutx, void *arg)
{
		ClientInfo * clientInfo = (ClientInfo *)arg;
		int strLen = 0;
		int index = clientInfo->index;
		uint8_t headerData[20] = {0};
		HeaderData header = {0};

		// 임시변수
		char strBuff[BUF_SIZE*2]; 
		// char to_msg[34*ID_SIZE+1];
		// MSG_INFO msg_info;


		// ClientInfo  * firstClientInfo;

		// firstClientInfo = (ClientInfo *)((void *)clientInfo - (void *)( sizeof(ClientInfo) * index ));
		while(1)
		{
				
			memset(headerData,0x0,20);
			strLen = read(clientInfo->fd, headerData, 20);
				
			if(strLen <= 0)
					break;
			else if(strLen == 4)
			{
			    header.head0 	= headerData[0];
    			header.head1 	= headerData[1];
    			header.cmd 			= headerData[2];
    			header.dataLen 		= headerData[3];
				msg_event(header, clientInfo);
			}
			else // strLen is not 4 : request header data
			{
				// requestHead(firstClientInfo);
				continue;					
			
			}

			sprintf(strBuff,"Recive Data : %c, %c, %hhu, %hhu%\n",header.head0,header.head1,header.cmd,header.dataLen);
			
			log_file(strBuff);
		}

		close(clientInfo->fd);

		// sprintf(strBuff,"Disconnect ID:%s (ip:%s,fd:%d,sockcnt:%d)\n",clientInfo->id,clientInfo->ip,clientInfo->fd,clntCnt-1);
		// log_file(strBuff);

		// pthread_mutex_lock(&mutx);
		// clntCnt--;
		// clientInfo->fd = -1;
		// pthread_mutex_unlock(&mutx);

}

void EventMsg(HeaderData head, ClientInfo * clientInfo)
{
	int strLen = 0;
	char recvData[80];
	if((head.head0 = 'o')&&(head.head0 = 'k'))
	{
		if(head.cmd==4)
		{
			HeaderData* sendHead = {'R','Q',0,0};

			write(clientInfo->fd, sendHead, 4);

			Cmd_List cmd = (Cmd_List)head.cmd;
			
			
			while(1)
			{
				strLen += read(clientInfo->fd, recvData, head.dataLen);

				if(strLen==head.dataLen)
				{
					recvData[strLen]='\0';
					break;
				}
			}		
		}
	}
}


// 아래 함수는 차후 common 으로 이동
void LogFile(char * msgStr)
{
		fputs(msgStr,stdout);
}