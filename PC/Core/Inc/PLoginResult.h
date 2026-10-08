#pragma once

#include <QMetaType>

typedef enum
{
    LOGIN_SUCCESS = 0,
    LOGIN_REJECTED,
    LOGIN_NO_SERVER,
    LOGIN_TIMEOUT,
    LOGIN_BAD_PACKET
} LoginResult;

Q_DECLARE_METATYPE(LoginResult)
