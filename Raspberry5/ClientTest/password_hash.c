#include "IoTPacket.h"

#include <sodium.h>
#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#define PASSWORD_INPUT_SIZE (MEM_PW_SIZE + 2)

static int ReadPassword(char *password, size_t passwordSize);

int main(void)
{
    char password[PASSWORD_INPUT_SIZE];
    char passwordHash[crypto_pwhash_STRBYTES];
    size_t passwordLength;

    if(sodium_init() < 0)
    {
        fputs("libsodium initialization failed\n", stderr);
        return 1;
    }

    if(ReadPassword(password, sizeof(password)) != 0)
    {
        fputs("Password must be between 1 and 64 bytes\n", stderr);
        return 1;
    }

    passwordLength = strlen(password);
    if(crypto_pwhash_str(passwordHash, password, passwordLength, crypto_pwhash_OPSLIMIT_INTERACTIVE, crypto_pwhash_MEMLIMIT_INTERACTIVE) != 0)
    {
        fputs("Password hashing failed\n", stderr);
        sodium_memzero(password, sizeof(password));
        return 1;
    }

    sodium_memzero(password, sizeof(password));
    puts(passwordHash);
    sodium_memzero(passwordHash, sizeof(passwordHash));
    return 0;
}

static int ReadPassword(char *password, size_t passwordSize)
{
    struct termios originalSettings;
    struct termios hiddenSettings;
    int terminalSettingsChanged = 0;
    char *newLine;

    if(tcgetattr(STDIN_FILENO, &originalSettings) == 0)
    {
        hiddenSettings = originalSettings;
        hiddenSettings.c_lflag &= (tcflag_t)~ECHO;
        if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &hiddenSettings) == 0)
        {
            terminalSettingsChanged = 1;
        }
    }

    fputs("Password: ", stderr);
    fflush(stderr);
    if(fgets(password, passwordSize, stdin) == NULL)
    {
        if(terminalSettingsChanged)
        {
            tcsetattr(STDIN_FILENO, TCSAFLUSH, &originalSettings);
            fputc('\n', stderr);
        }
        return -1;
    }

    if(terminalSettingsChanged)
    {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &originalSettings);
        fputc('\n', stderr);
    }

    newLine = strchr(password, '\n');
    if(newLine == NULL || newLine == password)
    {
        sodium_memzero(password, passwordSize);
        return -1;
    }

    *newLine = '\0';
    return 0;
}
