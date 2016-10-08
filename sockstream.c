#include "sockstream.h"
#include "vnclog.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <netdb.h>


struct SockStream {
    int sockFd;
    char readBuf[64];
    char writeBuf[64];
    int readOff, readSize, writeOff;
    struct iovec *iov;
    int iov_nitems;
};

SockStream *sock_connectVNCHost(const char *hostVNC)
{
    struct addrinfo hints, *result, *rp;
    int sockFd;
    char *host = strdup(hostVNC);
    unsigned port = 5900;
    char portStr[20];

    char *disp = strchr(host, ':');
    if( disp != NULL ) {
        *disp++ = '\0';
        port += atoi(disp);
    }
    sprintf(portStr, "%u", port);
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = 0;
    hints.ai_protocol = 0;
    int s = getaddrinfo(host, portStr, &hints, &result);
    if(s != 0)
        vnclog_fatal("connect: %s", gai_strerror(s));
    free(host);

    for( rp = result; rp != NULL; rp = rp->ai_next ) {
        sockFd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sockFd == -1)
            continue;
        if( connect(sockFd, rp->ai_addr, rp->ai_addrlen) != -1 )
            break;
        close(sockFd);
    }
    if (rp == NULL)
        vnclog_fatal_errno("connect");
    freeaddrinfo(result);
    int isOn = 1;
    if( setsockopt(sockFd, IPPROTO_TCP, TCP_NODELAY, &isOn, sizeof(isOn)) )
        vnclog_error_errno("set socket TCP_NODELAY failed");
    SockStream *strm = malloc(sizeof(SockStream));
    strm->sockFd = sockFd;
    strm->readOff = strm->readSize = strm->writeOff = 0;
    strm->iov = NULL;
    strm->iov_nitems = 0;
    return strm;
}

void sock_read(SockStream *strm, void *buf, int toRead)
{
    if( strm->readOff < strm->readSize ) {
        int toCopy = strm->readSize - strm->readOff;
        if( toRead < toCopy )
            toCopy = toRead;
        memcpy(buf, strm->readBuf + strm->readOff, toCopy);
        buf = (char*)buf + toCopy;
        strm->readOff += toCopy;
        toRead -= toCopy;
    }
    if( toRead > 0 ) {
        struct iovec iov[2];
        iov[1].iov_base = strm->readBuf;
        iov[1].iov_len = sizeof(strm->readBuf);
        while( 1 ) {
            iov[0].iov_base = buf;
            iov[0].iov_len = toRead;
            int rd = readv(strm->sockFd, iov, 2);
            if( rd < 0 )
                vnclog_fatal_errno("socket read");
            toRead -= rd;
            if( toRead <= 0 )
                break;
            buf = (char*)buf + rd;
        }
        strm->readOff = 0;
        strm->readSize = -toRead;
    }
}

unsigned sock_readU8(SockStream *strm)
{
    unsigned char uc;

    sock_read(strm, &uc, 1);
    return uc;
}

unsigned sock_readU16(SockStream *strm)
{
    uint16_t u16;

    sock_read(strm, &u16, 2);
    return ntohs(u16);
}

unsigned sock_readU32(SockStream *strm)
{
    uint32_t u32;

    sock_read(strm, &u32, 4);
    return ntohl(u32);
}

void sock_readRect(SockStream *strm, char *buf, int bytesPerLine,
        int width, int height)
{
    int lineNo;

    while( height > 0 && strm->readSize - strm->readOff >= width ) {
        memcpy(buf, strm->readBuf + strm->readOff, width);
        strm->readOff += width;
        --height;
        buf = (char*)buf + bytesPerLine;
    }
    if( height == 0 )
        return;
    int off = strm->readSize - strm->readOff;
    if( off > 0 )
        memcpy(buf, strm->readBuf + strm->readOff, off);
    if( height >= strm->iov_nitems ) {
        strm->iov_nitems = height + 1;
        strm->iov = realloc(strm->iov,
                strm->iov_nitems * sizeof(*strm->iov));
    }
    for(lineNo = 1; lineNo < height; ++lineNo) {
        strm->iov[lineNo].iov_base = buf + lineNo * bytesPerLine;
        strm->iov[lineNo].iov_len = width;
    }
    strm->iov[height].iov_base = strm->readBuf;
    strm->iov[height].iov_len = sizeof(strm->readBuf);
    while( (lineNo = off / width) < height ) {
        int lineOff = off - lineNo * width;
        strm->iov[lineNo].iov_base = buf + lineNo * bytesPerLine + lineOff;
        strm->iov[lineNo].iov_len = width - lineOff;
        int rd = readv(strm->sockFd,
                strm->iov + lineNo, height - lineNo + 1);
        if( rd < 0 )
            vnclog_fatal_errno("socket read");
        off += rd;
    }
    strm->readSize = off - height * width;
    strm->readOff = 0;
}

void sock_write(SockStream *strm, const void *buf, int count)
{
    while( strm->writeOff + count >= sizeof(strm->writeBuf) ) {
        int toCopy = sizeof(strm->writeBuf) - strm->writeOff;
        memcpy(strm->writeBuf + strm->writeOff, buf, toCopy);
        buf = (const char*)buf + toCopy;
        count -= toCopy;
        strm->writeOff = sizeof(strm->writeBuf);
        sock_flush(strm);
    }
    memcpy(strm->writeBuf + strm->writeOff, buf, count);
    strm->writeOff += count;
}

void sock_writeU8(SockStream *strm, unsigned val)
{
    unsigned char uc = val;

    sock_write(strm, &uc, 1);
}

void sock_writeU16(SockStream *strm, unsigned val)
{
    uint16_t u16 = htons(val);

    sock_write(strm, &u16, 2);
}

void sock_writeU32(SockStream *strm, unsigned val)
{
    uint32_t u32 = htonl(val);

    sock_write(strm, &u32, 4);
}

void sock_flush(SockStream *strm)
{
    int off = 0;
    while( off < strm->writeOff ) {
        int wr = write(strm->sockFd, strm->writeBuf + off,
                strm->writeOff - off);
        if( off < 0 )
            vnclog_fatal_errno("socket write");
        off += wr;
    }
    strm->writeOff = 0;
}

int sock_isDataAvail(SockStream *strm)
{
    return strm->readOff < strm->readSize;
}

int sock_fd(SockStream *strm)
{
    return strm->sockFd;
}

void sock_close(SockStream *strm)
{
    close(strm->sockFd);
    free(strm);
}

