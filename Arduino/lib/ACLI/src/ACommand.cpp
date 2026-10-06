#include "ACommand.h"

void CmdEcho(Print &out, const char *args)
{
    out.println(args);
}
