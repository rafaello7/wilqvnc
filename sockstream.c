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
        log_fatal("connect: %s", gai_strerror(s));
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
        log_fatal_errno("connect");
    freeaddrinfo(result);
    int isOn = 1;
    if( setsockopt(sockFd, IPPROTO_TCP, TCP_NODELAY, &isOn, sizeof(isOn)) )
        log_error_errno("set socket TCP_NODELAY failed");
    SockStream *strm = malloc(sizeof(SockStream));
    strm->sockFd = sockFd;
    strm->readOff = strm->readSize = strm->writeOff = 0;
    return strm;
}

SockStream *sock_accept(void)
{
    int listenFd, sockFd, isOn = 1;
    unsigned port = 5900;

    if( (listenFd = socket(AF_INET, SOCK_STREAM, 0)) < 0 )
        log_fatal_errno("socket");
    if( setsockopt(listenFd, SOL_SOCKET, SO_REUSEADDR, &isOn,
                sizeof(isOn)) )
        log_error_errno("set socket SO_REUSEADDR failed");
    struct sockaddr_in sin;
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = htonl(INADDR_ANY);
    sin.sin_port = htons(port);
    if( bind(listenFd, (struct sockaddr*)&sin, sizeof(sin)) < 0 )
        log_fatal_errno("bind");
    if( listen(listenFd, 5) < 0 )
        log_fatal_errno("listen");
    while( (sockFd = accept(listenFd, NULL, 0)) >= 0 ) {
        switch( 0 /*fork()*/ ) {
        case -1:
            log_fatal_errno("fork");
        case 0:
            close(listenFd);
            if( setsockopt(sockFd, IPPROTO_TCP, TCP_NODELAY, &isOn,
                        sizeof(isOn)) )
                log_error_errno("set socket TCP_NODELAY failed");
            SockStream *strm = malloc(sizeof(SockStream));
            strm->sockFd = sockFd;
            strm->readOff = strm->readSize = strm->writeOff = 0;
            return strm;
        default:
            close(sockFd);
            break;
        }
    }
    log_fatal_errno("accept");
    return NULL;
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
            if( rd <= 0 ) {
                if( rd == 0 )
                    log_fatal("end of stream");
                else
                    log_fatal_errno("socket read");
            }
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
    enum { IOV_SIZE = 128 };
    struct iovec iov[IOV_SIZE];
    int lineNo;

    if( width == 0 || height == 0 )
        return;
    while( strm->readSize - strm->readOff >= width ) {
        memcpy(buf, strm->readBuf + strm->readOff, width);
        strm->readOff += width;
        if( --height == 0 )
            return;
        buf = (char*)buf + bytesPerLine;
    }
    int off = strm->readSize - strm->readOff;
    memcpy(buf, strm->readBuf + strm->readOff, off);
    while( height >= IOV_SIZE ) {
        for(lineNo = 1; lineNo < IOV_SIZE; ++lineNo) {
            iov[lineNo].iov_base = buf + lineNo * bytesPerLine;
            iov[lineNo].iov_len = width;
        }
        while( (lineNo = off / width) < IOV_SIZE ) {
            int lineOff = off - lineNo * width;
            iov[lineNo].iov_base = buf + lineNo * bytesPerLine + lineOff;
            iov[lineNo].iov_len = width - lineOff;
            int rd = readv(strm->sockFd, iov + lineNo, IOV_SIZE - lineNo);
            if( rd <= 0 ) {
                if( rd == 0 )
                    log_fatal("end of stream");
                else
                    log_fatal_errno("socket read");
            }
            off += rd;
        }
        height -= IOV_SIZE;
        if( height == 0 ) {
            strm->readOff = strm->readSize = 0;
            return;
        }
        buf = (char*)buf + IOV_SIZE * bytesPerLine;
        off = 0;
    }
    for(lineNo = 1; lineNo < height; ++lineNo) {
        iov[lineNo].iov_base = buf + lineNo * bytesPerLine;
        iov[lineNo].iov_len = width;
    }
    iov[height].iov_base = strm->readBuf;
    iov[height].iov_len = sizeof(strm->readBuf);
    while( (lineNo = off / width) < height ) {
        int lineOff = off - lineNo * width;
        iov[lineNo].iov_base = buf + lineNo * bytesPerLine + lineOff;
        iov[lineNo].iov_len = width - lineOff;
        int rd = readv(strm->sockFd, iov + lineNo,
                height - lineNo + 1);
        if( rd <= 0 ) {
            if( rd == 0 )
                log_fatal("end of stream");
            else
                log_fatal_errno("socket read");
        }
        off += rd;
    }
    strm->readSize = off - height * width;
    strm->readOff = 0;
}

void sock_discard(SockStream *strm, unsigned bytes)
{
    char buf[16384];

    while( bytes > sizeof(buf) ) {
        sock_read(strm, buf, sizeof(buf));
        bytes -= sizeof(buf);
    }
    if( bytes > 0 )
        sock_read(strm, buf, bytes);
}

void sock_write(SockStream *strm, const void *buf, int count)
{
    if( strm->writeOff + count > sizeof(strm->writeBuf)) {
        struct iovec iov[2];
        int iovBeg = 0, iovEnd = 0;

        if( strm->writeOff != 0 ) {
            iov[iovEnd].iov_base = strm->writeBuf;
            iov[iovEnd].iov_len = strm->writeOff;
            ++iovEnd;
        }
        iov[iovEnd].iov_base = (void*)buf;
        iov[iovEnd].iov_len = count;
        ++iovEnd;
        while( 1 ) {
            int wr = writev(strm->sockFd, iov + iovBeg, iovEnd - iovBeg);
            if( wr < 0 )
                log_fatal_errno("writev");
            while( iovBeg < iovEnd && iov[iovBeg].iov_len <= wr ) {
                wr -= iov[iovBeg].iov_len;
                ++iovBeg;
            }
            if( iovBeg == iovEnd )
                break;
            iov[iovBeg].iov_base = (char*)iov[iovBeg].iov_base + wr;
            iov[iovBeg].iov_len -= wr;
        }
        strm->writeOff = 0;
    }else{
        memcpy(strm->writeBuf + strm->writeOff, buf, count);
        strm->writeOff += count;
    }
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

void sock_writeRect(SockStream *strm, const char *buf, int bytesPerLine,
        int width, int height)
{
    enum { IOV_SIZE = 128 };
    struct iovec iov[IOV_SIZE];
    int i;

    if( strm->writeOff + width * height <= sizeof(strm->writeBuf) ) {
        for(i = 0; i < height; ++i) {
            memcpy(strm->writeBuf + strm->writeOff, buf, width);
            buf += bytesPerLine;
            strm->writeOff += width;
        }
        return;
    }
    while( height > 0 ) {
        int iovBeg = 0, iovEnd = 0;
        if( strm->writeOff > 0 ) {
            iov[iovEnd].iov_base = strm->writeBuf;
            iov[iovEnd].iov_len = strm->writeOff;
            strm->writeOff = 0;
            ++iovEnd;
        }
        while( height > 0 && iovEnd < IOV_SIZE ) {
            iov[iovEnd].iov_base = (void*)buf;
            iov[iovEnd].iov_len = width;
            buf = (const char*)buf + bytesPerLine;
            ++iovEnd;
            --height;
        }
        while( 1 ) {
            int wr = writev(strm->sockFd, iov + iovBeg, iovEnd - iovBeg);
            if( wr < 0 )
                log_fatal_errno("writev");
            while( iovBeg < iovEnd && iov[iovBeg].iov_len <= wr ) {
                wr -= iov[iovBeg].iov_len;
                ++iovBeg;
            }
            if( iovBeg == iovEnd )
                break;
            iov[iovBeg].iov_base = (char*)iov[iovBeg].iov_base + wr;
            iov[iovBeg].iov_len -= wr;
        }
    }
}

void sock_flush(SockStream *strm)
{
    int off = 0;
    while( off < strm->writeOff ) {
        int wr = write(strm->sockFd, strm->writeBuf + off,
                strm->writeOff - off);
        if( off < 0 )
            log_fatal_errno("socket write");
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

