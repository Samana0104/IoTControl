#include "RCmdFirmware.h"
#include "IoTFirmware.h"
#include "RPacketBt.h"
#include "RPacketFirmware.h"
#include "RSession.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

// PC가 Samba 공유(\\pi\samba\firmware\stm32)에 올린 STM32 앱 이미지 (빌드 결과 IoTControl-v<버전>.bin)
#define FIRMWARE_DIRECTORY "/srv/samba/firmware/stm32"
#define FIRMWARE_PATH_SIZE 512
// 전송 후 장치가 재부팅하고 부트로더가 설치를 끝낼 때까지 기다리는 시간
#define FIRMWARE_INSTALL_WAIT_SECONDS 6
// 재부팅 뒤 BT 세션이 끊겼으면 다시 연결을 시도하는 횟수와 간격
#define FIRMWARE_RECONNECT_TRIES 5
#define FIRMWARE_RECONNECT_INTERVAL_SECONDS 3

// CLI 스레드에서만 사용 (112KB라 스택 대신 정적 버퍼)
static uint8_t imageBuffer[FIRMWARE_APP_MAX_SIZE];

static int LoadFirmwareFile(const char *fileName, size_t *imageSize, uint32_t *version);
static void ReconnectAfterInstall(const char *memberId);

// 공유 폴더의 이미지와 버전 (STM32 앱으로 확인되지 않으면 invalid)
static void FwList(TCPServer *server, const char *args)
{
    DIR *directory;
    struct dirent *entry;
    size_t fileCount = 0;

    (void)server;
    (void)args;
    directory = opendir(FIRMWARE_DIRECTORY);
    if(directory == NULL)
    {
        printf("Cannot open %s: %s\n", FIRMWARE_DIRECTORY, strerror(errno));
        return;
    }
    printf("%s\n", FIRMWARE_DIRECTORY);
    puts("VERSION  SIZE     FILE");
    while((entry = readdir(directory)) != NULL)
    {
        size_t imageSize;
        uint32_t version;

        if(entry->d_name[0] == '.')
        {
            continue;
        }
        ++fileCount;
        if(LoadFirmwareFile(entry->d_name, &imageSize, &version) == 0)
        {
            printf("%-8u %-8zu %s\n", (unsigned int)version, imageSize, entry->d_name);
        }
        else
        {
            printf("%-8s %-8s %s\n", "invalid", "-", entry->d_name);
        }
    }
    closedir(directory);
    if(fileCount == 0)
    {
        puts("No firmware files.");
    }
}

// 연결된 STM32(BT)에 이미지를 보내고, 재부팅 뒤 BT 세션을 확인 (끊겼으면 다시 연결)
// 전송하는 동안 CLI가 멈춤 (진행률은 로그)
static void FwPush(TCPServer *server, const char *args)
{
    char memberId[MEM_ID_SIZE + 2];
    char fileName[FIRMWARE_PATH_SIZE];
    char extra[2];
    RSessionSnapshot snapshot;
    size_t imageSize;
    uint32_t version;
    int fd;

    if(sscanf(args, "%9s %511s %1s", memberId, fileName, extra) != 2 || strlen(memberId) > MEM_ID_SIZE)
    {
        puts("Usage: fw push <member ID> <file> (file in " FIRMWARE_DIRECTORY ", see fw list)");
        return;
    }
    if(server->socket < 0)
    {
        puts("Start the server first: server start [port]");
        return;
    }
    fd = RSessionFindBtFd(memberId);
    if(fd < 0 || RSessionFindByFd(fd, &snapshot) != 0)
    {
        printf("No Bluetooth session for id=%s (bt connect %s first)\n", memberId, memberId);
        return;
    }
    if(snapshot.memberType != MEMBER_TYPE_STM32)
    {
        printf("Firmware update supports STM32 only: id=%s, type=%s\n", memberId, RSessionMemberTypeName(snapshot.memberType));
        return;
    }
    if(LoadFirmwareFile(fileName, &imageSize, &version) != 0)
    {
        printf("Not a valid STM32 application image: %s (see fw list)\n", fileName);
        return;
    }

    printf("Sending %s (version %u, %zu bytes) to id=%s fd=%d. This takes about %zu seconds...\n", fileName, (unsigned int)version, imageSize, memberId, fd, imageSize / 800 + 5);
    fflush(stdout);
    if(RPacketFirmwarePush(fd, imageBuffer, imageSize) != 0)
    {
        printf("Firmware update failed: %s (see log)\n", strerror(errno));
        return;
    }
    printf("Image accepted. Waiting %d seconds for the bootloader to install version %u...\n", FIRMWARE_INSTALL_WAIT_SECONDS, (unsigned int)version);
    fflush(stdout);
    sleep(FIRMWARE_INSTALL_WAIT_SECONDS);
    ReconnectAfterInstall(memberId);
}

// 파일 이름만 받음 (경로, 숨김 파일 거부). 0: imageBuffer에 읽고 STM32 앱 이미지로 확인됨, -1: 실패
static int LoadFirmwareFile(const char *fileName, size_t *imageSize, uint32_t *version)
{
    char path[FIRMWARE_PATH_SIZE];
    struct stat fileStat;
    ssize_t readSize;
    int file;

    if(fileName[0] == '.' || strchr(fileName, '/') != NULL)
    {
        return -1;
    }
    if(snprintf(path, sizeof(path), "%s/%s", FIRMWARE_DIRECTORY, fileName) >= (int)sizeof(path))
    {
        return -1;
    }
    file = open(path, O_RDONLY | O_CLOEXEC);
    if(file < 0)
    {
        return -1;
    }
    if(fstat(file, &fileStat) != 0 || !S_ISREG(fileStat.st_mode) || fileStat.st_size <= 0 || (size_t)fileStat.st_size > sizeof(imageBuffer))
    {
        close(file);
        return -1;
    }
    readSize = read(file, imageBuffer, (size_t)fileStat.st_size);
    close(file);
    if(readSize != fileStat.st_size)
    {
        return -1;
    }
    *imageSize = (size_t)readSize;
    return CheckFirmwareImage(imageBuffer, *imageSize, version);
}

// MCU만 재부팅되고 HC-05 링크는 유지될 수 있음. 세션이 남아 있으면 그대로 두고, 끊겼으면 다시 연결
static void ReconnectAfterInstall(const char *memberId)
{
    for(int tryCount = 0; tryCount < FIRMWARE_RECONNECT_TRIES; ++tryCount)
    {
        int fd;

        if(RSessionFindBtFd(memberId) < 0)
        {
            RPacketBtConnectMember(memberId);
        }
        fd = RSessionFindBtFd(memberId);
        if(fd >= 0)
        {
            printf("Firmware update done: id=%s, fd=%d (check the device log for the new version)\n", memberId, fd);
            return;
        }
        sleep(FIRMWARE_RECONNECT_INTERVAL_SECONDS);
    }
    printf("Firmware sent, but Bluetooth did not reconnect: id=%s (try bt connect %s)\n", memberId, memberId);
}

static const RCommand FIRMWARE_COMMANDS[] =
{
    {"list", FwList},
    {"push", FwPush}
};

void RCmdFirmware(TCPServer *server, const char *args)
{
    RCommandDispatch(server, args, "fw", FIRMWARE_COMMANDS, RCOMMAND_COUNT(FIRMWARE_COMMANDS));
}
