#include "cliconn.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <rpc/des_crypt.h>
#include "vnclog.h"
#include "sockstream.h"
#include <sys/time.h>
#include <zlib.h>


struct ClientConnection {
    SockStream *strm;
    z_stream zstrm;
    int width;
    int height;
    char *name;
};

static VncVersion exchangeVersion(CliConn *conn)
{
    static const char VER33[] = "RFB 003.003\n";
    static const char VER37[] = "RFB 003.007\n";
    static const char VER38[] = "RFB 003.008\n";
    char buf[12];
    VncVersion vncVer;

    sock_read(conn->strm, buf, 12);
    if( ! memcmp(buf, VER33, 12) )
        vncVer = VNCVER_3_3;
    else if( ! memcmp(buf, VER37, 12) )
        vncVer = VNCVER_3_7;
    else if( !memcmp(buf, VER38, 12) )
        vncVer = VNCVER_3_8;
    else
        log_fatal("unsupported server version %.11s", buf);
    sock_write(conn->strm, buf, 12);
    sock_flush(conn->strm);
    return vncVer;
}

static void exchangeAuth(CliConn *conn, const char *passwdFile,
        VncVersion vncVer)
{
    static char pfileKey[] = "\350J\326`\304r\032\340";
    char buf[1024], pass[8];
    int i, c, len, selectedAuthNo = 0;

    if( vncVer == VNCVER_3_3 ) {
        selectedAuthNo = sock_readU32(conn->strm);
    }else{
        int authCnt = sock_readU8(conn->strm);
        log_debug("authentication methods count: %d", authCnt);
        for( ; authCnt > 0; --authCnt ) {
            int authNo = sock_readU8(conn->strm);
            log_debug("  %d", authNo);
            if( selectedAuthNo == 0 || authNo < selectedAuthNo )
                selectedAuthNo = authNo;
        }
    }
    if( selectedAuthNo == 0 ) {
        len = sock_readU32(conn->strm);
        if( len >= sizeof(buf) )
            len = sizeof(buf) - 1;
        sock_read(conn->strm, buf, len);
        buf[len] = '\0';
        log_fatal("server error: %s", buf);
    }
    if( selectedAuthNo == 1 ) {
        if( vncVer != VNCVER_3_3 ) {
            sock_writeU8(conn->strm, 1);
            sock_flush(conn->strm);
        }
    }else if( selectedAuthNo == 2 ) {
        if( passwdFile != NULL ) {
            FILE *fp = fopen(passwdFile, "r");
            if( fp == NULL )
                log_fatal_errno("unable to open password file");
            if( fread(pass, 8, 1, fp) != 1 )
                log_fatal("bad password file");
            fclose(fp);
            if(ecb_crypt(pfileKey, pass, 8, DES_DECRYPT|DES_SW) != DESERR_NONE)
                log_fatal("ecb_crypt error");
        }else{
            char *p = getpass("Server password: ");
            if( p == NULL )
                exit(1);
            strncpy(pass, p, 8);
        }
        if( vncVer != VNCVER_3_3 ) {
            sock_writeU8(conn->strm, 2);
            sock_flush(conn->strm);
        }
        sock_read(conn->strm, buf, 16);
        // "mirror" password bytes
        for(i = 0; i < 8; ++i) {
            c = pass[i];
            pass[i] = (c&1) << 7 | (c&2) << 5 | (c&4) << 3 | (c&8) << 1 |
                (c >> 1 & 8) | (c >> 3 & 4) | (c >> 5 & 2) | (c >> 7 & 1);
        }
        if( ecb_crypt(pass, buf, 16, DES_ENCRYPT | DES_SW) != DESERR_NONE )
            log_fatal("ecb_crypt error");
        sock_write(conn->strm, buf, 16);
        sock_flush(conn->strm);
    }else
        log_fatal("unsupported authentication type");
    if( selectedAuthNo != 1 || vncVer == VNCVER_3_8 ) {
        if( sock_readU32(conn->strm) != 0 ) {     // authentication result
            if( vncVer == VNCVER_3_8 ) {
                len = sock_readU32(conn->strm);
                sock_read(conn->strm, buf, len);
                log_fatal("%.*s", len, buf);
            }else
                log_fatal("Authentication failed.");
        }
    }
}

static void readPixelFormat(CliConn *conn, PixelFormat *pixelFormat)
{
    pixelFormat->bitsPerPixel = sock_readU8(conn->strm);
    pixelFormat->depth = sock_readU8(conn->strm);
    pixelFormat->bigEndian = sock_readU8(conn->strm);
    pixelFormat->trueColor = sock_readU8(conn->strm);
    pixelFormat->maxRed = sock_readU16(conn->strm);
    pixelFormat->maxGreen = sock_readU16(conn->strm);
    pixelFormat->maxBlue = sock_readU16(conn->strm);
    pixelFormat->shiftRed = sock_readU8(conn->strm);
    pixelFormat->shiftGreen = sock_readU8(conn->strm);
    pixelFormat->shiftBlue = sock_readU8(conn->strm);
    log_debug("remote pixel format:");
    log_debug("  bitsPerPixel: %d", pixelFormat->bitsPerPixel);
    log_debug("  depth:        %d", pixelFormat->depth);
    log_debug("  bigEndian:    %s",
            pixelFormat->bigEndian ? "true" : "false");
    log_debug("  trueColor:    %s",
            pixelFormat->trueColor ? "true" : "false");
    log_debug("  red   shift:  %-2d  max: %d", pixelFormat->shiftRed,
            pixelFormat->maxRed);
    log_debug("  green shift:  %-2d  max: %d", pixelFormat->shiftGreen,
            pixelFormat->maxGreen);
    log_debug("  blue  shift:  %-2d  max: %d", pixelFormat->shiftBlue,
            pixelFormat->maxBlue);
    sock_discard(conn->strm, 3);     // padding
}

CliConn *cliconn_open(const char *vncHost, const char *passwdFile)
{
    PixelFormat pixelFormat;
    int toRd, initRes;

    CliConn *conn = malloc(sizeof(CliConn));
    conn->strm = sock_connectVNCHost(vncHost);

    conn->zstrm.zalloc = NULL;
    conn->zstrm.zfree = NULL;
    conn->zstrm.opaque = NULL;
    if( (initRes = inflateInit(&conn->zstrm)) != Z_OK )
        log_fatal("inflateInit error=%d", initRes);
    VncVersion vncVer = exchangeVersion(conn);
    exchangeAuth(conn, passwdFile, vncVer);
    sock_writeU8(conn->strm, 0); // shared flag
    sock_flush(conn->strm);
    // read ServerInit
    conn->width = sock_readU16(conn->strm);
    conn->height = sock_readU16(conn->strm);
    readPixelFormat(conn, &pixelFormat);
    log_info("desktop size: %dx%dx%d", conn->width, conn->height,
            pixelFormat.bitsPerPixel);
    // name-length
    toRd = sock_readU32(conn->strm);
    conn->name = malloc(toRd+1);
    // name-string
    sock_read(conn->strm, conn->name, toRd);
    conn->name[toRd] = '\0';
    return conn;
}

int cliconn_getWidth(const CliConn *conn)
{
    return conn->width;
}

int cliconn_getHeight(const CliConn *conn)
{
    return conn->height;
}

const char *cliconn_getName(const CliConn *conn)
{
    return conn->name;
}

void cliconn_setEncodings(CliConn *conn, int enableHextile,
        int enableZRLE)
{
    int encodingCount = 3;

    if( enableHextile )
        ++encodingCount;
    if( enableZRLE )
        ++encodingCount;
    sock_writeU8(conn->strm, 2);    // message type
    sock_writeU8(conn->strm, 0);    // padding
    sock_writeU16(conn->strm, encodingCount);   // number of encodings
    sock_writeU32(conn->strm, 0);   // Raw encoding
    sock_writeU32(conn->strm, 1);   // CopyRect encoding
    sock_writeU32(conn->strm, 2);   // RRE encoding
    if( enableHextile )
        sock_writeU32(conn->strm, 5);   // Hextile encoding
    if( enableZRLE )
        sock_writeU32(conn->strm, 16);   // ZRLE encoding
    sock_flush(conn->strm);
}

void cliconn_setPixelFormat(CliConn *conn, const PixelFormat *pixelFormat)
{
    char padding[3] = "";

    sock_writeU8(conn->strm, 0);
    sock_write(conn->strm, padding, 3);
    sock_writeU8(conn->strm, pixelFormat->bitsPerPixel);
    sock_writeU8(conn->strm, pixelFormat->depth);
    sock_writeU8(conn->strm, pixelFormat->bigEndian);
    sock_writeU8(conn->strm, pixelFormat->trueColor);
    sock_writeU16(conn->strm, pixelFormat->maxRed);
    sock_writeU16(conn->strm, pixelFormat->maxGreen);
    sock_writeU16(conn->strm, pixelFormat->maxBlue);
    sock_writeU8(conn->strm, pixelFormat->shiftRed);
    sock_writeU8(conn->strm, pixelFormat->shiftGreen);
    sock_writeU8(conn->strm, pixelFormat->shiftBlue);
    sock_write(conn->strm, padding, 3);
    sock_flush(conn->strm);
}

void cliconn_sendFramebufferUpdateRequest(CliConn *conn, int incremental)
{
    sock_writeU8(conn->strm, 3);
    sock_writeU8(conn->strm, incremental);
    sock_writeU16(conn->strm, 0);
    sock_writeU16(conn->strm, 0);
    sock_writeU16(conn->strm, conn->width);
    sock_writeU16(conn->strm, conn->height);
    sock_flush(conn->strm);
}

void cliconn_sendKeyEvent(CliConn *conn, const VncKeyEvent *ev)
{
    sock_writeU8(conn->strm, 4);
    sock_writeU8(conn->strm, ev->isDown ? 1 : 0);
    sock_writeU16(conn->strm, 0);
    sock_writeU32(conn->strm, ev->keysym);
    sock_flush(conn->strm);
}

void cliconn_sendPointerEvent(CliConn *conn, const VncPointerEvent *ev)
{
    sock_writeU8(conn->strm, 5);
    sock_writeU8(conn->strm, ev->buttonMask);
    sock_writeU16(conn->strm, ev->x);
    sock_writeU16(conn->strm, ev->y);
    sock_flush(conn->strm);
}

int cliconn_nextEvent(CliConn *conn, DisplayConnection *dispConn,
        DisplayEvent *displayEvent, int wait)
{
    int cliMsg = -1;
    if( clidisp_nextEvent(dispConn, sock_isDataAvail(conn->strm),
            sock_fd(conn->strm), displayEvent, wait) )
    {
        cliMsg = sock_readU8(conn->strm);
    }
    return cliMsg;
}

static void decodeRRE(DisplayConnection *dispConn, SockStream *strm,
        int x, int y, int width, int height)
{
    char buf[16];
    int i, cnt = sock_readU32(strm); // number of subrectangles
    int bytespp = clidisp_getBytesPerPixel(dispConn);

    sock_read(strm, buf, bytespp);
    clidisp_fillRect(dispConn, buf, x, y, width, height);
    for(i = 0; i < cnt; ++i) {
        sock_read(strm, buf, bytespp);
        int sx = sock_readU16(strm);
        int sy = sock_readU16(strm);
        int sw = sock_readU16(strm);
        int sh = sock_readU16(strm);
        clidisp_fillRect(dispConn, buf, x + sx, y + sy, sw, sh);
    }
}

static void decodeHextile(DisplayConnection *dispConn, SockStream *strm,
        int x, int y, int width, int height)
{
    int i, j;
    char bg[16], fg[16], *subfg, subfg_buf[16];
    int bytespp = clidisp_getBytesPerPixel(dispConn);

    for(i = 0; i < height; i += 16) {
        int th = height - i > 16 ? 16 : height - i;
        for(j = 0; j < width; j += 16) {
            int tw = width - j > 16 ? 16 : width - j;
            unsigned mask = sock_readU8(strm);
            if( mask & 1 ) {    // raw encoding
                clidisp_putRectFromSocket(dispConn, strm, x + j, y + i, tw, th);
            }else{
                if( mask & 2 )  // BackgroundSpecified
                    sock_read(strm, bg, bytespp);
                if( mask & 4 )  // ForegroundSpecified
                    sock_read(strm, fg, bytespp);
                clidisp_fillRect(dispConn, bg, x + j, y + i, tw, th);
                if( mask & 8 )  { // AnySubrects
                    int subrectCount = sock_readU8(strm), sub;
                    for(sub = 0; sub < subrectCount; ++sub) {
                        if( mask & 16 ) {   // SubrectsColored
                            sock_read(strm, subfg_buf, bytespp);
                            subfg = subfg_buf;
                        }else
                            subfg = fg;
                        unsigned pos = sock_readU8(strm);
                        unsigned dim = sock_readU8(strm);
                        clidisp_fillRect(dispConn, subfg,
                                x + j + (pos >> 4), y + i + (pos&15),
                                (dim >> 4) + 1, (dim & 15) + 1);
                    }
                }
            }
        }
    }
}

static void decodeZRLE(DisplayConnection *dispConn, CliConn *conn,
        int x, int y, int width, int height)
{
    unsigned char *bufcompr, *bufdest;
    int comprlen, resInfl, outlen;

    comprlen = sock_readU32(conn->strm);
    bufcompr = malloc(comprlen);

    sock_read(conn->strm, bufcompr, comprlen);
    conn->zstrm.next_in = (Bytef*)bufcompr;
    conn->zstrm.avail_in = comprlen;
    // assume that encoding produces less output than "raw" one
    outlen = width * height * clidisp_getBytesPerPixel(dispConn);
    bufdest = malloc(outlen);
    conn->zstrm.next_out = (Bytef*)bufdest;
    conn->zstrm.avail_out = outlen;
    resInfl = inflate(&conn->zstrm, Z_SYNC_FLUSH);
    if( resInfl != Z_OK )
        log_fatal("inflate returned %d", resInfl);
    if( conn->zstrm.avail_in != 0 )
        log_fatal("non-zero inflate avail_in: %d", conn->zstrm.avail_in);
    outlen -= conn->zstrm.avail_out;
    clidisp_decodeTRLE(dispConn, bufdest, outlen, x, y, width, height, 64);
    free(bufcompr);
    free(bufdest);
}

static unsigned long long curTimeMs(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000LL + tv.tv_usec / 1000LL;
}

void cliconn_recvFramebufferUpdate(CliConn *conn, DisplayConnection *dispConn)
{
    int srcX, srcY, cnt;
    SockStream *strm = conn->strm;

    sock_readU8(strm); // padding
    unsigned long long updBegTm = curTimeMs();
    cnt = sock_readU16(strm); // number of rectangles
    while( cnt-- > 0 ) {
        int x = sock_readU16(strm);
        int y = sock_readU16(strm);
        int width = sock_readU16(strm);
        int height = sock_readU16(strm);
        int encType = sock_readU32(strm);
        switch( encType ) {
        case 0: // Raw encoding
            clidisp_putRectFromSocket(dispConn, strm, x, y, width, height);
            break;
        case 1: // CopyRect encoding
            srcX = sock_readU16(strm);
            srcY = sock_readU16(strm);
            clidisp_copyRect(dispConn, srcX, srcY, x, y, width, height);
            break;
        case 2: // RRE encoding
            decodeRRE(dispConn, strm, x, y, width, height);
            break;
        case 5: // Hextile encoding
            decodeHextile(dispConn, strm, x, y, width, height);
            break;
        case 16:
            decodeZRLE(dispConn, conn, x, y, width, height);
            break;
        default:
            log_fatal("unsupported encoding %d", encType);
            break;
        }
        unsigned long long curTm = curTimeMs();
        if( cnt > 0 && curTm - updBegTm > 500 ) {
            clidisp_flush(dispConn);
            updBegTm = curTm;
        }
    }
    clidisp_flush(dispConn);
}

void cliconn_recvCutTextMsg(CliConn *conn)
{
    int toRd;

    sock_discard(conn->strm, 3);   // padding
    toRd = sock_readU32(conn->strm); // text length
    sock_discard(conn->strm, toRd);
}

void cliconn_close(CliConn *conn)
{
    sock_close(conn->strm);
    inflateEnd(&conn->zstrm);
    free(conn);
}

