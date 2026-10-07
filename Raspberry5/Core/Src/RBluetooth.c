#include "RBluetooth.h"

#include <bluetooth/bluetooth.h>
#include <bluetooth/rfcomm.h>
#include <bluetooth/sdp.h>
#include <bluetooth/sdp_lib.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define RFCOMM_MIN_CHANNEL 1
#define RFCOMM_MAX_CHANNEL 30
#define PAIR_AGENT_EXIT_GRACE_SECONDS 5

#ifndef IOT_BT_PAIR_AGENT_DEFAULT
#define IOT_BT_PAIR_AGENT_DEFAULT "./bluetooth_pair_agent.py"
#endif

static int DiscoverRfcommChannel(const bdaddr_t *bluetoothAddress);
static int WriteAllFileDescriptor(int fileDescriptor, const void *data, size_t length);
static int WaitForPairAgent(pid_t processId, int timeoutSeconds);

int ConnectBluetoothDevice(const char *bluetoothMac, int timeoutMs, uint8_t *rfcommChannel)
{
    struct sockaddr_rc bluetoothAddress;
    struct pollfd socketEvent;
    socklen_t socketErrorSize = sizeof(int);
    int discoveredChannel;
    int bluetoothFd;
    int socketFlags;
    int socketError = 0;
    int connectResult;

    if(bluetoothMac == NULL || timeoutMs <= 0 || rfcommChannel == NULL)
    {
        errno = EINVAL;
        return -1;
    }

    memset(&bluetoothAddress, 0, sizeof(bluetoothAddress));
    bluetoothAddress.rc_family = AF_BLUETOOTH;
    if(str2ba(bluetoothMac, &bluetoothAddress.rc_bdaddr) != 0)
    {
        errno = EINVAL;
        return -1;
    }

    discoveredChannel = DiscoverRfcommChannel(&bluetoothAddress.rc_bdaddr);
    if(discoveredChannel < RFCOMM_MIN_CHANNEL || discoveredChannel > RFCOMM_MAX_CHANNEL)
    {
        return -1;
    }
    bluetoothAddress.rc_channel = (uint8_t)discoveredChannel;

    bluetoothFd = socket(AF_BLUETOOTH, SOCK_STREAM | SOCK_CLOEXEC, BTPROTO_RFCOMM);
    if(bluetoothFd < 0)
    {
        return -1;
    }

    socketFlags = fcntl(bluetoothFd, F_GETFL, 0);
    if(socketFlags < 0 || fcntl(bluetoothFd, F_SETFL, socketFlags | O_NONBLOCK) < 0)
    {
        close(bluetoothFd);
        return -1;
    }

    connectResult = connect(bluetoothFd, (struct sockaddr *)&bluetoothAddress, sizeof(bluetoothAddress));
    if(connectResult < 0 && errno != EINPROGRESS)
    {
        close(bluetoothFd);
        return -1;
    }

    if(connectResult < 0)
    {
        socketEvent.fd = bluetoothFd;
        socketEvent.events = POLLOUT;
        socketEvent.revents = 0;

        do
        {
            connectResult = poll(&socketEvent, 1, timeoutMs);
        }
        while(connectResult < 0 && errno == EINTR);

        if(connectResult <= 0)
        {
            if(connectResult == 0)
            {
                errno = ETIMEDOUT;
            }
            close(bluetoothFd);
            return -1;
        }

        if(getsockopt(bluetoothFd, SOL_SOCKET, SO_ERROR, &socketError, &socketErrorSize) < 0 || socketError != 0)
        {
            if(socketError != 0)
            {
                errno = socketError;
            }
            close(bluetoothFd);
            return -1;
        }
    }

    if(fcntl(bluetoothFd, F_SETFL, socketFlags) < 0)
    {
        close(bluetoothFd);
        return -1;
    }

    *rfcommChannel = (uint8_t)discoveredChannel;
    return bluetoothFd;
}

int PairBluetoothDevice(const char *bluetoothMac, const char *pin, int timeoutSeconds)
{
    const char *pairAgentPath = getenv("IOT_BT_PAIR_AGENT");
    bdaddr_t bluetoothAddress;
    size_t pinLength;
    int pinPipe[2];
    pid_t processId;
    char timeoutText[16];
    int writeResult;

    if(bluetoothMac == NULL || pin == NULL || timeoutSeconds <= 0 || str2ba(bluetoothMac, &bluetoothAddress) != 0)
    {
        errno = EINVAL;
        return -1;
    }

    pinLength = strnlen(pin, BLUETOOTH_PIN_SIZE + 1);
    if(pinLength == 0 || pinLength > BLUETOOTH_PIN_SIZE || strspn(pin, "0123456789") != pinLength)
    {
        errno = EINVAL;
        return -1;
    }

    if(pairAgentPath == NULL || *pairAgentPath == '\0')
    {
        pairAgentPath = IOT_BT_PAIR_AGENT_DEFAULT;
    }
    snprintf(timeoutText, sizeof(timeoutText), "%d", timeoutSeconds);
    if(pipe(pinPipe) != 0)
    {
        return -1;
    }

    processId = fork();
    if(processId < 0)
    {
        close(pinPipe[0]);
        close(pinPipe[1]);
        return -1;
    }
    if(processId == 0)
    {
        close(pinPipe[1]);
        if(dup2(pinPipe[0], STDIN_FILENO) < 0)
        {
            _exit(127);
        }
        close(pinPipe[0]);
        execl("/usr/bin/python3", "python3", pairAgentPath, bluetoothMac, timeoutText, (char *)NULL);
        _exit(127);
    }

    close(pinPipe[0]);
    writeResult = WriteAllFileDescriptor(pinPipe[1], pin, pinLength);
    if(writeResult == 0)
    {
        writeResult = WriteAllFileDescriptor(pinPipe[1], "\n", 1);
    }
    close(pinPipe[1]);
    if(writeResult != 0)
    {
        kill(processId, SIGTERM);
        waitpid(processId, NULL, 0);
        return -1;
    }

    return WaitForPairAgent(processId, (timeoutSeconds * 2) + PAIR_AGENT_EXIT_GRACE_SECONDS);
}

static int DiscoverRfcommChannel(const bdaddr_t *bluetoothAddress)
{
    sdp_session_t *session;
    sdp_list_t *searchList = NULL;
    sdp_list_t *attributeList = NULL;
    sdp_list_t *responseList = NULL;
    sdp_list_t *response;
    uuid_t serialPortUuid;
    uint32_t attributeRange = 0x0000ffff;
    int rfcommChannel = -1;
    int discoveryError = 0;

    session = sdp_connect(BDADDR_ANY, bluetoothAddress, SDP_RETRY_IF_BUSY);
    if(session == NULL)
    {
        return -1;
    }

    sdp_uuid16_create(&serialPortUuid, SERIAL_PORT_SVCLASS_ID);
    searchList = sdp_list_append(NULL, &serialPortUuid);
    attributeList = sdp_list_append(NULL, &attributeRange);
    if(searchList == NULL || attributeList == NULL)
    {
        discoveryError = ENOMEM;
        goto cleanup;
    }
    if(sdp_service_search_attr_req(session, searchList, SDP_ATTR_REQ_RANGE, attributeList, &responseList) < 0)
    {
        discoveryError = errno;
        goto cleanup;
    }

    for(response = responseList; response != NULL; response = response->next)
    {
        sdp_record_t *record = (sdp_record_t *)response->data;
        sdp_list_t *protocols = NULL;

        if(sdp_get_access_protos(record, &protocols) == 0)
        {
            int channel = sdp_get_proto_port(protocols, RFCOMM_UUID);

            sdp_list_foreach(protocols, (sdp_list_func_t)sdp_list_free, NULL);
            sdp_list_free(protocols, NULL);
            if(channel >= RFCOMM_MIN_CHANNEL && channel <= RFCOMM_MAX_CHANNEL)
            {
                rfcommChannel = channel;
                break;
            }
        }
    }

cleanup:
    for(response = responseList; response != NULL; response = response->next)
    {
        sdp_record_free((sdp_record_t *)response->data);
    }
    sdp_list_free(responseList, NULL);
    sdp_list_free(attributeList, NULL);
    sdp_list_free(searchList, NULL);
    sdp_close(session);
    if(rfcommChannel < 0)
    {
        errno = discoveryError == 0 ? ENOENT : discoveryError;
    }
    return rfcommChannel;
}

static int WriteAllFileDescriptor(int fileDescriptor, const void *data, size_t length)
{
    const uint8_t *current = (const uint8_t *)data;
    size_t writtenLength = 0;

    while(writtenLength < length)
    {
        ssize_t result = write(fileDescriptor, current + writtenLength, length - writtenLength);

        if(result < 0)
        {
            if(errno == EINTR)
            {
                continue;
            }
            return -1;
        }
        if(result == 0)
        {
            errno = EIO;
            return -1;
        }
        writtenLength += (size_t)result;
    }
    return 0;
}

static int WaitForPairAgent(pid_t processId, int timeoutSeconds)
{
    struct timespec startTime;
    struct timespec currentTime;
    struct timespec pollInterval = {0, 100000000};
    int processStatus;

    if(clock_gettime(CLOCK_MONOTONIC, &startTime) != 0)
    {
        kill(processId, SIGTERM);
        waitpid(processId, NULL, 0);
        return -1;
    }

    while(1)
    {
        pid_t waitResult = waitpid(processId, &processStatus, WNOHANG);

        if(waitResult == processId)
        {
            if(WIFEXITED(processStatus) && WEXITSTATUS(processStatus) == 0)
            {
                return 0;
            }
            errno = EACCES;
            return -1;
        }
        if(waitResult < 0)
        {
            if(errno == EINTR)
            {
                continue;
            }
            return -1;
        }
        if(clock_gettime(CLOCK_MONOTONIC, &currentTime) != 0)
        {
            break;
        }
        if(currentTime.tv_sec - startTime.tv_sec >= timeoutSeconds)
        {
            errno = ETIMEDOUT;
            break;
        }
        nanosleep(&pollInterval, NULL);
    }

    kill(processId, SIGTERM);
    waitpid(processId, NULL, 0);
    return -1;
}

void DisconnectBluetoothDevice(int bluetoothFd)
{
    if(bluetoothFd >= 0)
    {
        shutdown(bluetoothFd, SHUT_RDWR);
        close(bluetoothFd);
    }
}
