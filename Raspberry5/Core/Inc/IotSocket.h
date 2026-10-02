#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sys/stat.h>
#include <dirent.h>
#include <sys/time.h>
#include <time.h>
#include <errno.h>
// #include "IoTCommand.h"

#define BUF_SIZE 100
#define ID_SIZE 10
#define HEADER_SIZE 4
#define MAX_CLNT 30
typedef struct {
		int index;
		int fd;
		char ip[20];
		char id[ID_SIZE];
		char pw[ID_SIZE];
}ClientInfo;

//common 행
typedef struct {
		char head0;
		char head1;
		uint8_t cmd;
		uint8_t dataLen;
}HeaderData;

typedef enum
{
	CMD_DHT11_DATA = 0,
	CMD_FAN_DATA,
	CMD_CON_DATA,
	CMD_MEM_DATA,
	CMD_CHAT_DATA
}Cmd_List;