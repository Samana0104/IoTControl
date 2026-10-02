#include "IotSocket.h"

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

		// sprintf(strBuff,"Disconnect ID:%s (ip:%s,fd:%d,sockcnt:%d)\n",clientInfo->id,clientInfo->ip,clientInfo->fd,clnt_cnt-1);
		// log_file(strBuff);

		// pthread_mutex_lock(&mutx);
		// clnt_cnt--;
		// clientInfo->fd = -1;
		// pthread_mutex_unlock(&mutx);

}

void EventMsg(HeaderData head, ClientInfo * clientInfo)
{
	int strLen = 0;
	char recvData[80];
	if((head.head0 = 'o')&&(head.head0 = 'k'))
	{
		HeaderData* sendHead = {'R','Q',0,0};

		write(clientInfo->fd, sendHead, 4);

		Cmd_List cmd = (Cmd_List)head.cmd;

		if(cmd==4)
		{
			strLen = read(clientInfo->fd, recvData, head.dataLen);
		}
	}
}


// 아래 함수는 차후 common 으로 이동
void LogFile(char * msgstr)
{
		fputs(msgstr,stdout);
}