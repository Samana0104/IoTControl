#include "RNetLink.h"

#include <errno.h>
#include <openssl/err.h>
#include <sys/socket.h>
#include <unistd.h>

static int ReadTls(RNetLink *link, void *buffer, size_t size, int *wantWrite);
static int WriteTls(RNetLink *link, const void *buffer, size_t length);

int RNetLinkHandshake(RNetLink *link, int *wantWrite)
{
    int result;
    int tlsError;

    *wantWrite = 0;
    ERR_clear_error();
    result = SSL_accept(link->tls);
    if(result == 1)
    {
        return 1;
    }
    tlsError = SSL_get_error(link->tls, result);
    if(tlsError == SSL_ERROR_WANT_READ)
    {
        return 0;
    }
    if(tlsError == SSL_ERROR_WANT_WRITE)
    {
        *wantWrite = 1;
        return 0;
    }
    ERR_clear_error();
    return -1;
}

int RNetLinkRead(RNetLink *link, void *buffer, size_t size, int *wantWrite)
{
    ssize_t result;

    *wantWrite = 0;
    if(link->type == SESSION_TCP)
    {
        return ReadTls(link, buffer, size, wantWrite);
    }
    result = recv(link->fd, buffer, size, MSG_DONTWAIT);
    if(result > 0)
    {
        return (int)result;
    }
    if(result < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR))
    {
        return 0;
    }
    return -1;
}

int RNetLinkWrite(RNetLink *link, const void *buffer, size_t length)
{
    ssize_t result;

    if(link->type == SESSION_TCP)
    {
        return WriteTls(link, buffer, length);
    }
    result = send(link->fd, buffer, length, MSG_DONTWAIT | MSG_NOSIGNAL);
    if(result > 0)
    {
        return (int)result;
    }
    if(result < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR))
    {
        return 0;
    }
    return -1;
}

void RNetLinkShutdown(RNetLink *link)
{
    shutdown(link->fd, SHUT_RDWR);
}

void RNetLinkClose(RNetLink *link)
{
    if(link->type == SESSION_TCP)
    {
        SSL_free(link->tls);
        link->tls = NULL;
        close(link->fd);
    }
    else
    {
        DisconnectBluetoothDevice(link->fd);
    }
    link->fd = -1;
}

static int ReadTls(RNetLink *link, void *buffer, size_t size, int *wantWrite)
{
    int result;
    int tlsError;

    ERR_clear_error();
    result = SSL_read(link->tls, buffer, (int)size);
    if(result > 0)
    {
        return result;
    }
    tlsError = SSL_get_error(link->tls, result);
    if(tlsError == SSL_ERROR_WANT_READ)
    {
        return 0;
    }
    if(tlsError == SSL_ERROR_WANT_WRITE)
    {
        *wantWrite = 1;
        return 0;
    }
    ERR_clear_error();
    return -1;
}

static int WriteTls(RNetLink *link, const void *buffer, size_t length)
{
    int result;
    int tlsError;

    ERR_clear_error();
    result = SSL_write(link->tls, buffer, (int)length);
    if(result > 0)
    {
        return result;
    }
    tlsError = SSL_get_error(link->tls, result);
    if(tlsError == SSL_ERROR_WANT_WRITE || tlsError == SSL_ERROR_WANT_READ)
    {
        return 0;
    }
    ERR_clear_error();
    return -1;
}
