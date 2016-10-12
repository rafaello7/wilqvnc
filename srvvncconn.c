#include "srvvncconn.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <rpc/des_crypt.h>
#include "vnclog.h"


VncVersion srvconn_exchangeVersion(SockStream *strm)
{
    static const char VER33[] = "RFB 003.003\n";
    static const char VER37[] = "RFB 003.007\n";
    static const char VER38[] = "RFB 003.008\n";
    char buf[12];
    VncVersion vncVer;

    sock_write(strm, VER38, 12);
    sock_flush(strm);
    sock_read(strm, buf, 12);
    if( ! memcmp(buf, VER33, 12) )
        vncVer = VNCVER_3_3;
    else if( ! memcmp(buf, VER37, 12) )
        vncVer = VNCVER_3_7;
    else if( !memcmp(buf, VER38, 12) )
        vncVer = VNCVER_3_8;
    else
        log_fatal("unsupported client version %.11s", buf);
    return vncVer;
}

void srvconn_exchangeAuth(SockStream *strm, VncVersion vncVer,
        const char *passwd)
{
    static const char challenge[16] = "0123456789abcdef";
    static const char authFailMsg[] = "Authentication failed.";
    char buf[16], pass[8];
    int i, c, authSuccess = 0;

    if( passwd != NULL ) {
        if( vncVer == VNCVER_3_3 ) {
            sock_writeU32(strm, 2);   // VNCAuth
            sock_flush(strm);
        }else{
            sock_writeU8(strm, 1);
            sock_writeU8(strm, 2);    // VNCAuth
            sock_flush(strm);
            sock_readU8(strm);      // read selected auth
        }
        sock_write(strm, challenge, 16);
        sock_flush(strm);
        sock_read(strm, buf, 16);
        strncpy(pass, passwd, 8);
        // "mirror" password bytes
        for(i = 0; i < 8; ++i) {
            c = pass[i];
            pass[i] = (c&1) << 7 | (c&2) << 5 | (c&4) << 3 | (c&8) << 1 |
                (c >> 1 & 8) | (c >> 3 & 4) | (c >> 5 & 2) | (c >> 7 & 1);
        }
        if( ecb_crypt(pass, buf, 16, DES_DECRYPT | DES_SW) != DESERR_NONE )
            log_fatal("ecb_crypt error");
        authSuccess = !memcmp(buf, challenge, 16);
    }else{
        if( vncVer == VNCVER_3_3 ) {
            sock_writeU32(strm, 1);   // No authentication
            sock_flush(strm);
        }else{
            sock_writeU8(strm, 1);
            sock_writeU8(strm, 1);    // No authentication
            sock_flush(strm);
            sock_readU8(strm);      // read selected auth
        }
        authSuccess = 1;
    }
    if( passwd != NULL || vncVer == VNCVER_3_8 ) {
        if( authSuccess ) {
            sock_writeU32(strm, 0);
            sock_flush(strm);
        }else{
            sock_writeU32(strm, 1);
            if( vncVer == VNCVER_3_8 ) {
                int len = sizeof(authFailMsg)-1;
                sock_writeU32(strm, len);
                sock_write(strm, authFailMsg, len);
            }
            sock_flush(strm);
            exit(0);
        }
    }
}

void srvconn_sendServerInit(SockStream *strm, int width, int height,
        PixelFormat *pixelFormat, const char *name)
{
    char padding[3] = "";
    int len;

    sock_writeU16(strm, width);
    sock_writeU16(strm, height);
    // send PIXEL_FORMAT
    sock_writeU8(strm, pixelFormat->bitsPerPixel);
    sock_writeU8(strm, pixelFormat->depth);
    sock_writeU8(strm, pixelFormat->bigEndian);
    sock_writeU8(strm, pixelFormat->trueColor);
    sock_writeU16(strm, pixelFormat->maxRed);
    sock_writeU16(strm, pixelFormat->maxGreen);
    sock_writeU16(strm, pixelFormat->maxBlue);
    sock_writeU8(strm, pixelFormat->shiftRed);
    sock_writeU8(strm, pixelFormat->shiftGreen);
    sock_writeU8(strm, pixelFormat->shiftBlue);
    sock_write(strm, padding, 3);
    // send name
    len = strlen(name);
    sock_writeU32(strm, len);
    sock_write(strm, name, len);
    sock_flush(strm);
}

void srvconn_getEncodings(SockStream *strm)
{
    int i, encodingCount;

    sock_readU8(strm);    // padding
    encodingCount = sock_readU16(strm);
    for(i = 0; i < encodingCount; ++i) {
        sock_readU32(strm);
    }
}

void srvconn_getPixelFormat(SockStream *strm, PixelFormat *pixelFormat)
{
    sock_discard(strm, 3);     // padding
    pixelFormat->bitsPerPixel = sock_readU8(strm);
    pixelFormat->depth = sock_readU8(strm);
    pixelFormat->bigEndian = sock_readU8(strm);
    pixelFormat->trueColor = sock_readU8(strm);
    pixelFormat->maxRed = sock_readU16(strm);
    pixelFormat->maxGreen = sock_readU16(strm);
    pixelFormat->maxBlue = sock_readU16(strm);
    pixelFormat->shiftRed = sock_readU8(strm);
    pixelFormat->shiftGreen = sock_readU8(strm);
    pixelFormat->shiftBlue = sock_readU8(strm);
    sock_discard(strm, 3);     // padding
}

void srvconn_recvFramebufferUpdateRequest(SockStream *strm,
        FramebufferUpdateRequest *req)
{
    req->incremental = sock_readU8(strm);
    req->x = sock_readU16(strm);
    req->y = sock_readU16(strm);
    req->width = sock_readU16(strm);
    req->height = sock_readU16(strm);
}

void srvconn_recvKeyEvent(SockStream *strm, VncKeyEvent *kev)
{
    kev->isDown = sock_readU8(strm);
    sock_readU16(strm);     // padding
    kev->keysym = sock_readU32(strm);
}

void srvconn_recvPointerEvent(SockStream *strm, VncPointerEvent *pev)
{
    pev->buttonMask = sock_readU8(strm);
    pev->x = sock_readU16(strm);
    pev->y = sock_readU16(strm);
}

void srvconn_recvCutText(SockStream *strm)
{
    sock_discard(strm, 3);     // padding
    unsigned len = sock_readU32(strm);
    sock_discard(strm, len);
}

