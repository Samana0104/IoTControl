#pragma once

#include <stddef.h>

#include "IoTPacket.h"

typedef struct st_mysql MYSQL;

/* Connects once with RConfigGet()->database (load Config/DBConfig.json first) to check it.
   Must succeed before any query. */
int InitializeDatabase(void);
/* This thread's MySQL connection: opened on first use and reused (autocommit on).
   Closed automatically when the thread exits. NULL on connection failure. Queries: RDatabaseQuery.h */
MYSQL *GetDatabaseConnection(void);
/* Closes this thread's connection (lost connection, or before the main thread exits).
   The next GetDatabaseConnection() reconnects. */
void ResetDatabaseConnection(void);
/* 1: password matches, 0: wrong ID or password, -1: DB error.
   An unknown ID takes as long as a wrong password (dummy hash check). */
int VerifyMember(const char *memberId, size_t memberIdLength, const char *password, size_t passwordLength);
/* Signs up a member: hashes password with argon2 (libsodium) and inserts it.
   type: "STM32", "ARDUINO" or "PC". 1: added, 0: ID already exists, -1: invalid argument or DB error. */
int RegisterMember(const char *memberId, const char *password, const char *type);
