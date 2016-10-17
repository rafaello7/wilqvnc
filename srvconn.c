#include "srvconn.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <rpc/des_crypt.h>
#include <sys/time.h>
#include <lz4.h>
#include <zstd.h>
#include <zlib.h>
#include "srvcmdline.h"
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

static unsigned long long curTimeMs(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000LL + tv.tv_usec / 1000LL;
}

static int encodeTRLE(const unsigned *img, int itemsPerLine,
        const RectangleArea *rect, unsigned squareWidth, char *buf)
{
    char *bp = buf;
    int x, y, i, j, k;
    unsigned imgOff = rect-> y * itemsPerLine + rect->x;

    for(y = 0; y < rect->height; y += squareWidth) {
        unsigned tileHeight = rect->height - y > squareWidth ? squareWidth :
            rect->height - y;
        for(x = 0; x < rect->width; x += squareWidth) {
            unsigned tileWidth = rect->width - x > squareWidth ? squareWidth :
                rect->width - x;
            unsigned tileOff = imgOff + x;
            unsigned colors[128], ncolors = 0;
            for(i = 0; i < tileHeight && ncolors < 128; ++i) {
                for(j = 0; j < tileWidth && ncolors < 128; ++j) {
                    unsigned color = img[tileOff + j] & 0xffffff;
                    for(k = 0; k < ncolors && color != colors[k]; ++k) {
                    }
                    colors[ncolors++] = color;
                }
                tileOff += itemsPerLine;
            }
            if( 1 || ncolors == 128 ) {
                // raw encoding
                *bp++ = 0;
                tileOff = imgOff + x;
                for(i = 0; i < tileHeight; ++i) {
                    for(j = 0; j < tileWidth; ++j) {
                        unsigned color = img[tileOff + j];
                        *bp++ = color >> 16;
                        *bp++ = color >> 8;
                        *bp++ = color;
                    }
                    tileOff += itemsPerLine;
                }
            }else{
            }
        }
        imgOff += squareWidth * itemsPerLine;
    }
    return bp - buf;
}

static void encodeZRLE(SockStream *strm, const char *curImg,
        int bytesPerPixel, int bytesPerLine, const RectangleArea *rect)
{
    // TODO: associate with SockStream
    static z_stream zstrm;
    static int isInitialized = 0;
    int rectlen, srclen, complen, deflateRes;
    char *bufsrc, *bufdest;

    rectlen = rect->width * rect->height * bytesPerPixel;
    bufsrc = malloc(rectlen);
    if( ! isInitialized ) {
        zstrm.zalloc = NULL;
        zstrm.zfree = NULL;
        zstrm.opaque = NULL;
        int initRes = deflateInit(&zstrm, Z_DEFAULT_COMPRESSION);
        if( initRes != Z_OK )
            log_fatal("deflateInit error=%d", initRes);
        isInitialized = 1;
    }
    srclen = encodeTRLE((const unsigned*)curImg, bytesPerLine / bytesPerPixel,
            rect, 64, bufsrc);
    complen = deflateBound(&zstrm, srclen);
    bufdest = malloc(complen);
    zstrm.next_in = (Bytef*)bufsrc;
    zstrm.avail_in = srclen;
    zstrm.next_out = (Bytef*)bufdest;
    zstrm.avail_out = complen;
    while( 1 ) {
        deflateRes = deflate(&zstrm, Z_SYNC_FLUSH);
        if( deflateRes != Z_OK )
            log_fatal("deflate error %d", deflateRes);
        if( zstrm.avail_out != 0 )
            break;
        log_info(">> deflate buffer too small");
        complen += 4096;
        bufdest = realloc(bufdest, complen);
        zstrm.next_out = (Bytef*)bufdest;
        zstrm.avail_out = 4096;
    }
    complen -= zstrm.avail_out;
    sock_writeU32(strm, 16);    // ZRLE
    sock_writeU32(strm, complen);
    sock_write(strm, bufdest, complen);
    free(bufdest);
    free(bufsrc);
}

void srvconn_sendRectEncoded(SockStream *strm, const char *prevImg,
        const char *curImg, int bytesPerPixel, int bytesPerLine,
        const RectangleArea *rect)
{
    const CmdLineParams *params = cmdline_getParams();
    int useDiff;

    encodeZRLE(strm, curImg, bytesPerPixel, bytesPerLine, rect);
    return;
    useDiff = params->useDiff && prevImg != NULL &&
        bytesPerPixel == sizeof(int) && bytesPerLine % sizeof(int) == 0;
    if( useDiff || params->compr != COMPR_NONE ) {
        int rectlen, srclen, complen, comprMethod;
        char *bufsrc, *bufdest;
        const char *compressed;
        unsigned long long tmBeg = curTimeMs();

        rectlen = rect->width * rect->height * bytesPerPixel;
        bufsrc = malloc(rectlen);
        if( useDiff ) {
            const unsigned *prev = (const unsigned*)prevImg;
            const unsigned *cur = (const unsigned*)curImg;
            int itemsPerLine = bytesPerLine / sizeof(int);
            int dataOff = rect->y * itemsPerLine + rect->x;
            unsigned *buf = (unsigned*)bufsrc;
            srclen = 0;

            unsigned curPixel;
            unsigned prevDist = 255;
            for(int i = 0; i < rect->height; ++i) {
                for(int j = 0; j < rect->width; ++j) {
                    if( ++prevDist == 256 || cur[dataOff+j] != prev[dataOff+j])
                    {
                        if( i || j ) {
                            buf[srclen++] =
                                (curPixel&0xffffff) | (prevDist-1) << 24;
                        }
                        curPixel = cur[dataOff + j];
                        prevDist = 0;
                    }
                }
                dataOff += itemsPerLine;
            }
            buf[srclen++] = (curPixel&0xffffff) | prevDist << 24;
            srclen *= sizeof(int);
        }else{
            int dataOff = rect->y * bytesPerLine + rect->x * bytesPerPixel;
            int linelen = rect->width * bytesPerPixel;
            srclen = rectlen;
            int srcOff = 0;

            for(int i = 0; i < rect->height; ++i) {
                memcpy(bufsrc + srcOff, curImg + dataOff, linelen);
                dataOff += bytesPerLine;
                srcOff += linelen;
            }
        }
        switch( params->compr ) {
        case COMPR_LZ4:
            complen = LZ4_compressBound(srclen);
            compressed = bufdest = malloc(complen);
            complen = LZ4_compress_fast(bufsrc, bufdest, srclen, complen,
                    params->lz4Level);
            comprMethod = 1;
            break;
        case COMPR_ZSTD:
            complen = ZSTD_compressBound(srclen);
            compressed = bufdest = malloc(complen);
            complen = ZSTD_compress(bufdest, complen, bufsrc, srclen, 
                    params->zstdLevel);
            comprMethod = 2;
            break;
        default:
            bufdest = NULL;
            complen = srclen;
            break;
        }
        if( complen >= srclen ) {
            compressed = bufsrc;
            complen = srclen;
            comprMethod = 0;
        }
        unsigned long long tmCur = curTimeMs();
        log_info("%d -> %d, %d%%  %llu ms", rectlen, complen,
                100 * complen / rectlen, tmCur - tmBeg);
        sock_writeU32(strm, 0x514c4957);
        sock_writeU16(strm, useDiff ? 1 : 0);
        sock_writeU16(strm, comprMethod);
        if( useDiff )
            sock_writeU32(strm, srclen);
        sock_writeU32(strm, complen);
        sock_write(strm, compressed, complen);
        free(bufdest);
        free(bufsrc);
    }else{
        sock_writeU32(strm, 0);
        sock_writeRect(strm, curImg + rect->y * bytesPerLine +
                rect->x * bytesPerPixel, bytesPerLine,
                rect->width * bytesPerPixel, rect->height);
    }
}

