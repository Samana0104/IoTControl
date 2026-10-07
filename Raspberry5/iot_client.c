#include "IotClient.h"
#include "IoTPacket.h"

#include <sodium.h>
#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#define INPUT_BUFFER_SIZE (MAX_MESSAGE_SIZE + 2)
#define BLUETOOTH_REGISTER_COMMAND "bt-register "
#define BLUETOOTH_CONNECT_COMMAND "bt-connect "

static void DiscardRemainingInput(void);
static int ReadInput(const char *prompt, char *buffer, size_t bufferSize);
static int ReadHiddenInput(const char *prompt, char *buffer, size_t bufferSize);
static int RunBluetoothConnectionRequest(IotClient *client, const char *memberId, const char *bluetoothMac);
static int RunBluetoothConnectCommand(IotClient *client, const char *sessionMemberId, char *arguments);

int main(int argc, char *argv[])
{
    IotClient client;
    char message[INPUT_BUFFER_SIZE];
    char memberId[MEM_ID_SIZE + 2];
    char password[MEM_PW_SIZE + 2];
    char bluetoothPin[BLUETOOTH_PIN_SIZE + 2];

    /* Show command results immediately while the existing session waits for input. */
    setvbuf(stdout, NULL, _IOLBF, 0);

    if(argc != 3)
    {
        printf("Usage: %s <server IP> <port>\n", argv[0]);
        printf("TLS CA config: %s (IOT_TLS_CA_FILE=<CA certificate path>)\n", TLS_CLIENT_CONFIG_FILE);
        return 1;
    }

    InitializeClient(&client);
    if(ConnectClient(&client, argv[1], argv[2]) != 0)
    {
        perror("ConnectClient()");
        return 1;
    }

    if(ReadInput("ID: ", memberId, sizeof(memberId)) != 0 || ReadHiddenInput("Password: ", password, sizeof(password)) != 0)
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
    puts("Credentials sent. Enter a message, 'bt-register <MAC>', 'bt-connect <ID> <MAC>', or 'quit'.");

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

        if(strcmp(message, "bt-connect") == 0)
        {
            puts("Usage: bt-connect <ID> <MAC> (password is requested separately); <ID> may be omitted to use the login ID.");
            continue;
        }
        if(strncmp(message, BLUETOOTH_CONNECT_COMMAND, sizeof(BLUETOOTH_CONNECT_COMMAND) - 1) == 0)
        {
            if(RunBluetoothConnectCommand(&client, memberId, message + sizeof(BLUETOOTH_CONNECT_COMMAND) - 1) < 0)
            {
                DisconnectClient(&client);
                return 1;
            }
            continue;
        }

        if(strncmp(message, BLUETOOTH_REGISTER_COMMAND, sizeof(BLUETOOTH_REGISTER_COMMAND) - 1) == 0)
        {
            const char *bluetoothMac = message + sizeof(BLUETOOTH_REGISTER_COMMAND) - 1;

            if(ReadHiddenInput("HC-05 PIN: ", bluetoothPin, sizeof(bluetoothPin)) != 0 || RegisterBluetoothDevice(&client, bluetoothMac, bluetoothPin) != 0)
            {
                sodium_memzero(bluetoothPin, sizeof(bluetoothPin));
                perror("RegisterBluetoothDevice()");
                DisconnectClient(&client);
                return 1;
            }
            sodium_memzero(bluetoothPin, sizeof(bluetoothPin));
            puts("Bluetooth registration request sent.");
            continue;
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

static int RunBluetoothConnectCommand(IotClient *client, const char *sessionMemberId, char *arguments)
{
    char *savePointer;
    char *first = strtok_r(arguments, " \t", &savePointer);
    char *second = strtok_r(NULL, " \t", &savePointer);
    char *extra = strtok_r(NULL, " \t", &savePointer);
    const char *memberId = second != NULL ? first : sessionMemberId;
    const char *bluetoothMac = second != NULL ? second : first;

    if(first == NULL || extra != NULL || strlen(memberId) == 0 || strlen(memberId) > MEM_ID_SIZE || strlen(bluetoothMac) != BLUETOOTH_MAC_SIZE)
    {
        puts("Usage: bt-connect <ID> <MAC> (or bt-connect <MAC> for the login ID)");
        return 0;
    }
    return RunBluetoothConnectionRequest(client, memberId, bluetoothMac);
}

static int RunBluetoothConnectionRequest(IotClient *client, const char *memberId, const char *bluetoothMac)
{
    char password[MEM_PW_SIZE + 2];
    int result;

    if(ReadHiddenInput("Password: ", password, sizeof(password)) != 0)
    {
        sodium_memzero(password, sizeof(password));
        fputs("Invalid password input\n", stderr);
        return -1;
    }
    result = RequestBluetoothConnection(client, memberId, password, bluetoothMac);
    sodium_memzero(password, sizeof(password));
    if(result < 0)
    {
        perror("RequestBluetoothConnection()");
    }
    else
    {
        printf("BT result: %d (%s)\n", result, result == BLUETOOTH_CONNECT_SUCCEEDED ? "connected" : "failed");
    }
    return result;
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

static int ReadHiddenInput(const char *prompt, char *buffer, size_t bufferSize)
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

    result = ReadInput(prompt, buffer, bufferSize);
    if(terminalSettingsChanged)
    {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &originalSettings);
        fputc('\n', stdout);
    }

    return result;
}
