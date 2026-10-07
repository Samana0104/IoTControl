#include "RBluetooth.h"

#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define BLUETOOTH_COMMAND "bt-register "
#define BLUETOOTH_CONNECT_TIMEOUT_MS 5000
#define INPUT_BUFFER_SIZE 256

static int ReadBluetoothMac(int argc, char *argv[], char *bluetoothMac, size_t bluetoothMacSize);
static int RunBluetoothTerminal(int bluetoothFd);
static int WriteAll(int fileDescriptor, const void *data, size_t length);

int main(int argc, char *argv[])
{
    char bluetoothMac[BLUETOOTH_MAC_TEXT_SIZE];
    uint8_t rfcommChannel;
    int bluetoothFd;
    int terminalResult;

    signal(SIGPIPE, SIG_IGN);
    if(ReadBluetoothMac(argc, argv, bluetoothMac, sizeof(bluetoothMac)) != 0)
    {
        fprintf(stderr, "Usage: %s [bt-register <MAC>]\n", argv[0]);
        return 1;
    }

    bluetoothFd = ConnectBluetoothDevice(bluetoothMac, BLUETOOTH_CONNECT_TIMEOUT_MS, &rfcommChannel);
    if(bluetoothFd < 0)
    {
        fprintf(stderr, "HC-05 connection failed: mac=%s: %s\n", bluetoothMac, strerror(errno));
        return 1;
    }

    printf("HC-05 connected: mac=%s, channel=%u\n", bluetoothMac, (unsigned int)rfcommChannel);
    puts("Enter text to send or 'quit'. Received data is printed below.");
    terminalResult = RunBluetoothTerminal(bluetoothFd);
    DisconnectBluetoothDevice(bluetoothFd);
    return terminalResult == 0 ? 0 : 1;
}

static int ReadBluetoothMac(int argc, char *argv[], char *bluetoothMac, size_t bluetoothMacSize)
{
    char input[INPUT_BUFFER_SIZE];
    const char *macText;
    size_t macLength;

    if(argc == 3)
    {
        if(strcmp(argv[1], "bt-register") != 0)
        {
            return -1;
        }
        macText = argv[2];
    }
    else if(argc == 1)
    {
        char *newLine;

        fputs("Command: ", stdout);
        fflush(stdout);
        if(fgets(input, sizeof(input), stdin) == NULL)
        {
            return -1;
        }
        newLine = strchr(input, '\n');
        if(newLine != NULL)
        {
            *newLine = '\0';
        }
        if(strncmp(input, BLUETOOTH_COMMAND, sizeof(BLUETOOTH_COMMAND) - 1) != 0)
        {
            return -1;
        }
        macText = input + sizeof(BLUETOOTH_COMMAND) - 1;
    }
    else
    {
        return -1;
    }

    macLength = strlen(macText);
    if(macLength != BLUETOOTH_MAC_SIZE || macLength + 1 > bluetoothMacSize)
    {
        return -1;
    }
    memcpy(bluetoothMac, macText, macLength + 1);
    return 0;
}

static int RunBluetoothTerminal(int bluetoothFd)
{
    struct pollfd events[2];
    char input[INPUT_BUFFER_SIZE];
    uint8_t receiveData[INPUT_BUFFER_SIZE];

    events[0].fd = STDIN_FILENO;
    events[0].events = POLLIN;
    events[1].fd = bluetoothFd;
    events[1].events = POLLIN;

    while(1)
    {
        int pollResult;

        do
        {
            pollResult = poll(events, 2, -1);
        }
        while(pollResult < 0 && errno == EINTR);
        if(pollResult < 0)
        {
            perror("poll()");
            return -1;
        }

        if(events[1].revents & POLLIN)
        {
            ssize_t receiveLength = read(bluetoothFd, receiveData, sizeof(receiveData));

            if(receiveLength <= 0)
            {
                if(receiveLength < 0)
                {
                    perror("Bluetooth read");
                }
                else
                {
                    puts("HC-05 disconnected.");
                }
                return receiveLength == 0 ? 0 : -1;
            }
            fwrite(receiveData, 1, (size_t)receiveLength, stdout);
            fflush(stdout);
        }
        if(events[1].revents & (POLLERR | POLLHUP | POLLNVAL))
        {
            puts("HC-05 connection closed.");
            return -1;
        }

        if(events[0].revents & POLLIN)
        {
            size_t inputLength;

            if(fgets(input, sizeof(input), stdin) == NULL)
            {
                return 0;
            }
            inputLength = strlen(input);
            if(strcmp(input, "quit\n") == 0 || strcmp(input, "quit") == 0)
            {
                return 0;
            }
            if(WriteAll(bluetoothFd, input, inputLength) != 0)
            {
                perror("Bluetooth write");
                return -1;
            }
        }
        if(events[0].revents & (POLLERR | POLLHUP | POLLNVAL))
        {
            return 0;
        }
    }
}

static int WriteAll(int fileDescriptor, const void *data, size_t length)
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
