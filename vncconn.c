#include "vncconn.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <rpc/des_crypt.h>
#include "vnclog.h"


VncVersion vncconn_exchangeVersion(SockStream *strm)
{
    static const char VER33[] = "RFB 003.003\n";
    static const char VER37[] = "RFB 003.007\n";
    static const char VER38[] = "RFB 003.008\n";
    char buf[12];
    VncVersion vncVer;

    sock_read(strm, buf, 12);
    if( ! memcmp(buf, VER33, 12) )
        vncVer = VNCVER_3_3;
    else if( ! memcmp(buf, VER37, 12) )
        vncVer = VNCVER_3_7;
    else if( !memcmp(buf, VER38, 12) )
        vncVer = VNCVER_3_8;
    else
        vnclog_fatal("unsupported server version %.11s", buf);
    sock_write(strm, buf, 12);
    sock_flush(strm);
    return vncVer;
}

void vncconn_exchangeAuth(SockStream *strm, const char *passwdFile,
        VncVersion vncVer)
{
    static char pfileKey[] = "\350J\326`\304r\032\340";
    char buf[1024], pass[8];
    int i, c, len, selectedAuthNo = 0;

    if( vncVer == VNCVER_3_3 ) {
        selectedAuthNo = sock_readU32(strm);
    }else{
        int authCnt = sock_readU8(strm);
        vnclog_debug("authentication methods count: %d", authCnt);
        for( ; authCnt > 0; --authCnt ) {
            int authNo = sock_readU8(strm);
            vnclog_debug("  %d", authNo);
            if( selectedAuthNo == 0 || authNo < selectedAuthNo )
                selectedAuthNo = authNo;
        }
    }
    if( selectedAuthNo == 0 ) {
        len = sock_readU32(strm);
        if( len >= sizeof(buf) )
            len = sizeof(buf) - 1;
        sock_read(strm, buf, len);
        buf[len] = '\0';
        vnclog_fatal("server error: %s", buf);
    }
    if( selectedAuthNo == 1 ) {
        sock_writeU8(strm, 1);
        sock_flush(strm);
    }else if( selectedAuthNo == 2 ) {
        if( passwdFile != NULL ) {
            FILE *fp = fopen(passwdFile, "r");
            if( fp == NULL )
                vnclog_fatal_errno("unable to open password file");
            if( fread(pass, 8, 1, fp) != 1 )
                vnclog_fatal("bad password file");
            if(ecb_crypt(pfileKey, pass, 8, DES_DECRYPT|DES_SW) != DESERR_NONE)
                vnclog_fatal("ecb_crypt error");
        }else{
            char *p = getpass("Server password: ");
            if( p == NULL )
                exit(1);
            strncpy(pass, p, 8);
        }
        sock_writeU8(strm, 2);
        sock_flush(strm);
        sock_read(strm, buf, 16);
        // "mirror" password bytes
        for(i = 0; i < 8; ++i) {
            c = pass[i];
            pass[i] = (c&1) << 7 | (c&2) << 5 | (c&4) << 3 | (c&8) << 1 |
                (c >> 1 & 8) | (c >> 3 & 4) | (c >> 5 & 2) | (c >> 7 & 1);
        }
        if( ecb_crypt(pass, buf, 16, DES_ENCRYPT | DES_SW) != DESERR_NONE )
            vnclog_fatal("ecb_crypt error");
        sock_write(strm, buf, 16);
        sock_flush(strm);
    }else
        vnclog_fatal("unsupported authentication type");
    if( selectedAuthNo != 1 || vncVer == VNCVER_3_8 ) {
        if( sock_readU32(strm) != 0 ) {     // authentication result
            if( vncVer == VNCVER_3_8 ) {
                len = sock_readU32(strm);
                sock_read(strm, buf, len);
                vnclog_fatal("%.*s", len, buf);
            }else
                vnclog_fatal("Authentication failed.");
        }
    }
}

void vncconn_setEncodings(SockStream *strm, int enableHextile)
{
    int encodingCount = 3;

    if( enableHextile )
        ++encodingCount;
    sock_writeU8(strm, 2);    // message type
    sock_writeU8(strm, 0);    // padding
    sock_writeU16(strm, encodingCount);   // number of encodings
    sock_writeU32(strm, 0);   // Raw encoding
    sock_writeU32(strm, 1);   // CopyRect encoding
    sock_writeU32(strm, 2);   // RRE encoding
    if( enableHextile )
        sock_writeU32(strm, 5);   // Hextile encoding
    sock_flush(strm);
}

void vncconn_setPixelFormat(SockStream *strm, const PixelFormat *pixelFormat)
{
    char padding[3] = "";

    sock_writeU8(strm, 0);
    sock_write(strm, padding, 3);
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
    sock_flush(strm);
}

void vncconn_sendFramebufferUpdateRequest(SockStream *strm, int incremental,
        int x, int y, int width, int height)
{
    sock_writeU8(strm, 3);
    sock_writeU8(strm, incremental);
    sock_writeU16(strm, x);
    sock_writeU16(strm, y);
    sock_writeU16(strm, width);
    sock_writeU16(strm, height);
    sock_flush(strm);
}

void vncconn_sendKeyEvent(SockStream *strm, int isDown, unsigned keysym)
{
    sock_writeU8(strm, 4);
    sock_writeU8(strm, isDown ? 1 : 0);
    sock_writeU16(strm, 0);
    sock_writeU32(strm, keysym);
    sock_flush(strm);
}

void vncconn_sendPointerEvent(SockStream *strm, unsigned state, int x, int y)
{
    sock_writeU8(strm, 5);
    sock_writeU8(strm, state);
    sock_writeU16(strm, x);
    sock_writeU16(strm, y);
    sock_flush(strm);
}

