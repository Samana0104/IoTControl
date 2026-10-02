#include "IotClient.h"

#include <stdio.h>
#include <string.h>

#define INPUT_BUFFER_SIZE (MAX_MESSAGE_SIZE + 2)

static void DiscardRemainingInput(void);

int main(int argc, char *argv[])
{
    IotClient client;
    char message[INPUT_BUFFER_SIZE];

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

    puts("Connected. Enter a message or 'quit'.");

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
