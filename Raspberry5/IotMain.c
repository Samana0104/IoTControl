#include "IotSocket.h"

#include <stdio.h>
#include <string.h>

int main(int argc, char *argv[])
{
    if(argc == 1 || (argc == 2 && strcmp(argv[1], "--cli") == 0))
    {
        return StartServerWithCli(NULL, 1) == 0 ? 0 : 1;
    }
    if(argc == 2 && strcmp(argv[1], "--no-cli") == 0)
    {
        fputs("A port is required with --no-cli.\n", stderr);
        return 1;
    }

    if(argc != 2 && argc != 3)
    {
        printf("Usage: %s [<port> [--cli|--no-cli]]\n", argv[0]);
        return 1;
    }

    if(argc == 3)
    {
        if(strcmp(argv[2], "--cli") == 0)
        {
            return StartServerWithCli(argv[1], 1) == 0 ? 0 : 1;
        }
        if(strcmp(argv[2], "--no-cli") == 0)
        {
            return StartServerWithCli(argv[1], 0) == 0 ? 0 : 1;
        }
        fprintf(stderr, "Unknown option: %s\n", argv[2]);
        return 1;
    }

    return StartServer(argv[1]) == 0 ? 0 : 1;
}
