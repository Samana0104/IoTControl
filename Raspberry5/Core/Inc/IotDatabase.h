#pragma once

#include <stddef.h>

int InitializeDatabase(void);
int VerifyMember(const char *memberId, size_t memberIdLength, const char *password, size_t passwordLength);
