#include "IotClient.h"
#include "IoTPacket.h"

#include <sodium.h>
#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#define INPUT_BUFFER_SIZE (MAX_MESSAGE_SIZE + 2)

static void DiscardRemainingInput(void);
static int ReadInput(const char *prompt, char *buffer, size_t bufferSize);
static int ReadPassword(char *password, size_t passwordSize);

int main(int argc, char *argv[])
{
    IotClient client;
    char message[INPUT_BUFFER_SIZE];
    char memberId[MEM_ID_SIZE + 2];
    char password[MEM_PW_SIZE + 2];

    if(argc != 3)
    {
        printf("Usage: %s <server IP> <port>\n", argv[0]);
        return 1;
    }

    InitializeClient(&client);
    if(ConnectClient(&client, argv[1], argv[2]) != 0)
    {
        perror("ConnectClient()");
        return 1;
    }

    if(ReadInput("ID: ", memberId, sizeof(memberId)) != 0 || ReadPassword(password, sizeof(password)) != 0)
    {
        fputs("Invalid credentials input\n", stderr);
        sodium_memzero(password, sizeof(password));
        DisconnectClient(&client);
        return 1;
    }

    if(AuthenticateClient(&client, memberId, password) != 0)
    {
        fputs("Failed to send credentials\n", stderr);
        sodium_memzero(password, sizeof(password));
        DisconnectClient(&client);
        return 1;
    }

    sodium_memzero(password, sizeof(password));
    puts("Credentials sent. Enter a message or 'quit'.");

    while(fgets(message, sizeof(message), stdin) != NULL)
    {
        char *newLine = strchr(message, '\n');

        if(newLine != NULL)
        {
            *newLine = '\0';
        }
        else if(!feof(stdin))
        {
            DiscardRemainingInput();
            fprintf(stderr,
                    "Message is too long. Maximum length is %u bytes.\n",
                    MAX_MESSAGE_SIZE);
            continue;
        }

        if(strcmp(message, "quit") == 0)
        {
            break;
        }

        if(SendChatMessage(&client, message) != 0)
        {
            perror("SendChatMessage()");
            DisconnectClient(&client);
            return 1;
        }

        puts("Message sent.");
    }

    DisconnectClient(&client);
    return 0;
}

static void DiscardRemainingInput(void)
{
    int inputCharacter;

    do
    {
        inputCharacter = getchar();
    } while(inputCharacter != '\n' && inputCharacter != EOF);
}

static int ReadInput(const char *prompt, char *buffer, size_t bufferSize)
{
    char *newLine;

    fputs(prompt, stdout);
    fflush(stdout);
    if(fgets(buffer, bufferSize, stdin) == NULL)
    {
        return -1;
    }

    newLine = strchr(buffer, '\n');
    if(newLine == NULL)
    {
        DiscardRemainingInput();
        return -1;
    }

    *newLine = '\0';
    return 0;
}

static int ReadPassword(char *password, size_t passwordSize)
{
    struct termios originalSettings;
    struct termios hiddenSettings;
    int terminalSettingsChanged = 0;
    int result;

    if(tcgetattr(STDIN_FILENO, &originalSettings) == 0)
    {
        hiddenSettings = originalSettings;
        hiddenSettings.c_lflag &= (tcflag_t)~ECHO;
        if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &hiddenSettings) == 0)
        {
            terminalSettingsChanged = 1;
        }
    }

    result = ReadInput("Password: ", password, passwordSize);
    if(terminalSettingsChanged)
    {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &originalSettings);
        fputc('\n', stdout);
    }

    return result;
}
