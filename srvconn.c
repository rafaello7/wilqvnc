#include "srvconn.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <rpc/des_crypt.h>
#include <sys/time.h>
#include <time.h>
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

// TODO: remove <time.h> header on remove the dump below
void dumpToFile(const unsigned *img, int itemsPerLine,
                const RectangleArea *rect, int encBytes)
{
    char fname[200];
    struct timeval tv;
    struct tm *tmp;

    if( rect->width < 25 && rect->height < 25 )
        return;
    gettimeofday(&tv, NULL);
    tmp = localtime(&tv.tv_sec);
    sprintf(fname, "/tmp/dmp%02d%02d_%02d%02d%02d_%06ld_%dx%d_%d.img",
            tmp->tm_mon+1, tmp->tm_mday, tmp->tm_hour, tmp->tm_min,
            tmp->tm_sec, tv.tv_usec, rect->width, rect->height, encBytes);
    FILE *fp = fopen(fname, "w");
    if( fp == NULL )
        log_error_errno("unable to open %s", fname);
    for(int i = 0; i < rect->height; ++i) {
        if( fwrite(img + itemsPerLine * (i+rect->y) + rect->x,
                    rect->width * sizeof(int), 1, fp) != 1 )
            log_error_errno("write error %s", fname);
    }
    fclose(fp);
}

static int encodeTRLE(const unsigned *img, int itemsPerLine,
        const RectangleArea *rect, unsigned squareWidth, char *buf)
{
    char *bp = buf;
    int x, y, i, j, k;
    unsigned imgOff = rect->y * itemsPerLine + rect->x;

    for(y = 0; y < rect->height; y += squareWidth) {
        unsigned tileHeight = rect->height - y > squareWidth ? squareWidth :
            rect->height - y;
        for(x = 0; x < rect->width; x += squareWidth) {
            unsigned tileWidth = rect->width - x > squareWidth ? squareWidth :
                rect->width - x;
            unsigned tileOff = imgOff + x;
            unsigned colors[128], ncolors = 0;
            int hasheads[256], hashnext[128];

            memset(hasheads, 0, sizeof(hasheads));
            for(i = 0; i < tileHeight && ncolors < 128; ++i) {
                for(j = 0; j < tileWidth && ncolors < 128; ++j) {
                    unsigned color = img[tileOff + j] & 0xffffff;
                    unsigned hash = (color + (color >> 8) + (color >> 16))
                        & 0xff;
                    if( hasheads[hash] == 0 ) {
                        hasheads[hash] = ncolors + 1;
                        hashnext[ncolors] = -1;
                        colors[ncolors] = color;
                        ++ncolors;
                    }else{
                        k = hasheads[hash] - 1;
                        while( colors[k] != color && hashnext[k] != -1 )
                            k = hashnext[k];
                        if( colors[k] != color ) {
                            hashnext[k] = ncolors;
                            hashnext[ncolors] = -1;
                            colors[ncolors] = color;
                            ++ncolors;
                        }
                    }
                }
                tileOff += itemsPerLine;
            }
            if( ncolors < 128 ) {
                *bp++ = ncolors | (ncolors <= 16 ? 0 : 0x80);
                for(i = 0; i < ncolors; ++i) {
                    unsigned color = colors[i];
                    *bp++ = color;
                    *bp++ = color >> 8;
                    *bp++ = color >> 16;
                }
                if( ncolors > 1 ) {
                    if( ncolors <= 16 ) {
                        unsigned shift;
                        if( ncolors <= 2 ) {
                            shift = 1;
                        }else if( ncolors <= 4 ) {
                            shift = 2;
                        }else
                            shift = 4;
                        tileOff = imgOff + x;
                        for(i = 0; i < tileHeight; ++i) {
                            unsigned b = 0, bcount = 0;
                            for(j = 0; j < tileWidth; ++j) {
                                unsigned color = img[tileOff + j] & 0xffffff;
                                unsigned hash = (color + (color >> 8) +
                                        (color >> 16)) & 0xff;
                                for(k = hasheads[hash] - 1; colors[k] != color;
                                        k = hashnext[k])
                                {
                                }
                                b = b << shift | k;
                                bcount += shift;
                                if( bcount == 8 ) {
                                    *bp++ = b;
                                    b = 0;
                                    bcount = 0;
                                }
                            }
                            if( bcount != 0 )
                                *bp++ = b << (8 - bcount);
                            tileOff += itemsPerLine;
                        }
                    }else{
                        tileOff = imgOff + x;
                        unsigned curPixel, prevDist;
                        for(i = 0; i < tileHeight; ++i) {
                            for(int j = 0; j < tileWidth; ++j) {
                                unsigned color = img[tileOff + j] & 0xffffff;
                                if( i == 0 && j == 0 ) {
                                    curPixel = color;
                                    prevDist = 0;
                                }else if( color != curPixel ) {
                                    unsigned hash = (curPixel + (curPixel >> 8)
                                            + (curPixel >> 16)) & 0xff;
                                    for(k = hasheads[hash] - 1;
                                        colors[k] != curPixel; k = hashnext[k])
                                    {
                                    }
                                    if( prevDist > 0 ) {
                                        *bp++ = k | 0x80;
                                        while( prevDist >= 255 ) {
                                            *bp++ = 255;
                                            prevDist -= 255;
                                        }
                                        *bp++ = prevDist;
                                    }else
                                        *bp++ = k;
                                    curPixel = color;
                                    prevDist = 0;
                                }else
                                    ++prevDist;
                            }
                            tileOff += itemsPerLine;
                        }
                        unsigned hash = (curPixel + (curPixel >> 8)
                                + (curPixel >> 16)) & 0xff;
                        for(k = hasheads[hash] - 1; colors[k] != curPixel;
                                k = hashnext[k])
                        {
                        }
                        if( prevDist > 0 ) {
                            *bp++ = k | 0x80;
                            while( prevDist >= 255 ) {
                                *bp++ = 255;
                                prevDist -= 255;
                            }
                            *bp++ = prevDist;
                        }else
                            *bp++ = k;
                    }
                }
            }else{
                // raw encoding
                *bp++ = 0;
                tileOff = imgOff + x;
                for(i = 0; i < tileHeight; ++i) {
                    for(j = 0; j < tileWidth; ++j) {
                        unsigned color = img[tileOff + j];
                        *bp++ = color;
                        *bp++ = color >> 8;
                        *bp++ = color >> 16;
                    }
                    tileOff += itemsPerLine;
                }
            }
        }
        imgOff += squareWidth * itemsPerLine;
    }
    return bp - buf;
}

void encodeZRLE(SockStream *strm, const char *curImg,
        int bytesPerPixel, int bytesPerLine, const RectangleArea *rect)
{
    // TODO: associate with SockStream
    static z_stream zstrm;
    static int isInitialized = 0;
    int rectlen, srclen, complen, deflateRes;
    char *bufsrc, *bufdest;
    unsigned long long tmBeg = curTimeMs();

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
    unsigned long long tmCur = curTimeMs();
    log_info("ZRLE %dx%d %d -> %d %lld ms",
            rect->width, rect->height, rectlen, complen, tmCur - tmBeg);
    sock_writeU32(strm, 16);    // ZRLE
    sock_writeU32(strm, complen);
    sock_write(strm, bufdest, complen);
    free(bufdest);
    free(bufsrc);
}

static int encodeDiff(const unsigned *prevImg, const unsigned *curImg,
        int itemsPerLine, const RectangleArea *rect, char *bufsrc)
{
    int dataOff = rect->y * itemsPerLine + rect->x;
    unsigned *buf = (unsigned*)bufsrc;
    int srclen = 0;

    unsigned curPixel;
    unsigned prevDist = 255;
    for(int i = 0; i < rect->height; ++i) {
        for(int j = 0; j < rect->width; ++j) {
            ++prevDist;
            if( prevDist == 256 || curImg[dataOff+j] != prevImg[dataOff+j]) {
                if( i || j )
                    buf[srclen++] = (curPixel&0xffffff) | (prevDist-1) << 24;
                curPixel = curImg[dataOff + j];
                prevDist = 0;
            }
        }
        dataOff += itemsPerLine;
    }
    buf[srclen++] = (curPixel&0xffffff) | prevDist << 24;
    return srclen * sizeof(int);
}

struct BitPacker {
    char *bp;
    char byte;
    unsigned bits;
    unsigned avail;
};

static void pushBits(struct BitPacker *bp, unsigned data,
        unsigned dataBitCount)
{
    while( dataBitCount ) {
        unsigned bitCount = 8 - bp->bits;
        if( bitCount > dataBitCount )
            bitCount = dataBitCount;
        unsigned dataBits =  (data >> (dataBitCount - bitCount))
            & ((1 << bitCount) - 1);
        bp->byte = bp->byte << bitCount | dataBits;
        bp->bits += bitCount;
        dataBitCount -= bitCount;
        if( bp->bits == 8 ) {
            if( bp->avail == 0 )
                log_fatal("pushBits: buffer full");
            *bp->bp++ = bp->byte;
            bp->bits = 0;
            --bp->avail;
        }
    }
}

struct Tile {
    int squareCount;
    int squareBits;
    int color;
};

struct EncSubTileParam {
    struct BitPacker bpColors;
    struct BitPacker bpSquares;
    int rectWidth, rectHeight;
    const unsigned *colors;
    const int *hasheads;
    const int *hashnext;
    unsigned colorBits;
    struct Tile *const *tileSquares;
};

static void encodeSubTile(struct EncSubTileParam *estp, int lvl, int x, int y)
{
    if( lvl == 0 ) {
        unsigned color = estp->tileSquares[0][y * estp->rectWidth + x].color;
        if( estp->colors != NULL ) {
            unsigned hash = (color + (color >> 12)) & 0xfff;
            int colorIdx = estp->hasheads[hash] - 1;
            while( estp->colors[colorIdx] != color )
                colorIdx = estp->hashnext[colorIdx];
            color = colorIdx;
        }
        pushBits(&estp->bpColors, color, estp->colorBits);
        //log_info("encodeSubTile: lvl=%d, x=%d, y=%d, color=%d",
        //        lvl, x, y, colorIdx);
    }else{
        int psubWidth = 1 << (lvl-1);
        int phcount = (estp->rectWidth + psubWidth - 1) / psubWidth;
        int pvcount = (estp->rectHeight + psubWidth - 1) / psubWidth;
        int hcount = (phcount+1) / 2;
        int squareCount = estp->tileSquares[lvl][y * hcount + x].squareCount;

        if( squareCount > 1 ) {
            pushBits(&estp->bpSquares, 1, 1);
            for(int sy = 0; sy < 2; ++sy) {
                int py = 2 * y + sy;
                if( py < pvcount ) {
                    for(int sx = 0; sx < 2; ++sx) {
                        int px = 2 * x + sx;
                        if( px < phcount ) {
                            encodeSubTile(estp, lvl-1, px, py);
                        }
                    }
                }
            }
        }else{
            unsigned color = estp->tileSquares[lvl][y * hcount + x].color;
            if( estp->colors != NULL ) {
                unsigned hash = (color + (color >> 12)) & 0xfff;
                int colorIdx = estp->hasheads[hash] - 1;
                while( estp->colors[colorIdx] != color )
                    colorIdx = estp->hashnext[colorIdx];
                color = colorIdx;
            }
            pushBits(&estp->bpSquares, 0, 1);
            pushBits(&estp->bpColors, color, estp->colorBits);
            //log_info("encodeSubTile: lvl=%d, x=%d, y=%d, color=%d",
            //        lvl, x, y, colorIdx);
        }
    }
}

/* The idea: imagine a square having upper left corner at the upper left corner
 * of rectangle to encode. The square covers whole rectangle to encode and
 * the square side length is a power of two.  If the whole rectangle within
 * square is covered with one color, put '0' bit in output and then put the
 * square color in output. If not, put '1' bit in output, divide the square
 * into four smaller ones and dispatch every smaller square recursively. It
 * means, if the smaller square is covered with one color, put '0' bit and then
 * the square color in output.  Otherwise put '1' bit and divide the square
 * into four smaller ones.  And so forth.  The four smaller squares are
 * dispatched in order: upper left, upper right, lower left, lower right.
 * Sub-squares outside the encoded rectanlge are omitted. Sub-squares of size
 * 1x1 pixel haven't the split bit.
 *
 * Colors are outputted in encoded form, as indexes in color palette, which is
 * outputted before the encoded rectangle. The number of bits of outputted color
 * index depends on number of colors used; it is as small as possible.  It
 * means, if the whole rectangle to encode is covered with single color then no
 * bit is used for index. When 2 colors are used - one bit is used. When 3 or 4
 * - two bits are used. When 5 .. 8 - three bits. And so forth.
 */
static int encodeTila(const unsigned *img, int itemsPerLine,
        const RectangleArea *rect, char *buf)
{
    int dataOff = rect->y * itemsPerLine + rect->x;
    unsigned *colors = NULL, ncolors = 0, colorsAlloc = 0;
    int hasheads[4096], *hashnext = NULL, k;
    int tileWidth = 1;
    int rectlen = rect->width * rect->height * sizeof(int);

    while( tileWidth < rect->width || tileWidth < rect->height )
        tileWidth *= 2;
    // calculate number of colors
    memset(hasheads, 0, sizeof(hasheads));
    for(int i = 0; i < rect->height; ++i) {
        for(int j = 0; j < rect->width; ++j) {
            unsigned color = img[dataOff + j] & 0xffffff;
            unsigned hash = (color + (color >> 12)) & 0xfff;
            if( ncolors == colorsAlloc ) {
                colorsAlloc += 4096;
                colors = realloc(colors, colorsAlloc * sizeof(int));
                hashnext = realloc(hashnext, colorsAlloc * sizeof(int));
            }
            if( hasheads[hash] == 0 ) {
                hasheads[hash] = ncolors + 1;
                hashnext[ncolors] = -1;
                colors[ncolors] = color;
                ++ncolors;
            }else{
                k = hasheads[hash] - 1;
                while( colors[k] != color && hashnext[k] != -1 )
                    k = hashnext[k];
                if( colors[k] != color ) {
                    hashnext[k] = ncolors;
                    hashnext[ncolors] = -1;
                    colors[ncolors] = color;
                    ++ncolors;
                }
            }
        }
        dataOff += itemsPerLine;
    }
    struct Tile *tileSquares[15];
    int levelCount = 0;
    while( tileWidth >= (1 << levelCount) ) {
        int subWidth = 1 << levelCount;
        int hcount = (rect->width + subWidth - 1) / subWidth;
        int vcount = (rect->height + subWidth - 1) / subWidth;
        tileSquares[levelCount] = malloc(hcount * vcount * sizeof(struct Tile));
        ++levelCount;
    }
    dataOff = rect->y * itemsPerLine + rect->x;
    for(int i = 0; i < rect->height; ++i) {
        for(int j = 0; j < rect->width; ++j) {
            unsigned curPixel = img[dataOff + j] & 0xffffff;
            tileSquares[0][i * rect->width + j].squareCount = 1;
            tileSquares[0][i * rect->width + j].squareBits = 0;
            tileSquares[0][i * rect->width + j].color = curPixel;
        }
        dataOff += itemsPerLine;
    }
    for(int lvl = 1; lvl < levelCount; ++lvl) {
        int psubWidth = 1 << (lvl-1);
        int phcount = (rect->width + psubWidth - 1) / psubWidth;
        int pvcount = (rect->height + psubWidth - 1) / psubWidth;
        int hcount = (phcount+1) / 2, vcount = (pvcount+1) / 2;
        for(int i = 0; i < vcount; ++i) {
            for(int j = 0; j < hcount; ++j) {
                unsigned color =
                    tileSquares[lvl-1][2*i * phcount + 2*j].color;
                int squareCount = 0, isMultiColor = 0, squareBits = 1;
                for(int si = 0; si < 2; ++si) {
                    for(int sj = 0; sj < 2; ++sj) {
                        if( 2*i + si < pvcount && 2*j + sj < phcount ) {
                            int squareCount2 = tileSquares[lvl-1]
                                [(2*i+si) * phcount + 2*j + sj].squareCount;
                            int squareBits2 = tileSquares[lvl-1]
                                [(2*i+si) * phcount + 2*j + sj].squareBits;
                            unsigned color2 = tileSquares[lvl-1]
                                [(2*i+si) * phcount + 2*j + sj].color;
                            squareCount += squareCount2;
                            squareBits  += squareBits2;
                            if( squareCount2 > 1 || color2 != color )
                                isMultiColor = 1;
                        }
                    }
                }
                tileSquares[lvl][i * hcount + j].squareCount
                    = isMultiColor ? squareCount : 1;
                tileSquares[lvl][i * hcount + j].squareBits
                    = isMultiColor ? squareBits : 1;
                tileSquares[lvl][i * hcount + j].color = color;
            }
        }
    }
    int colorBits;
    if( ncolors <= 1 )
        colorBits = 0;
    else{
        colorBits = 1;
        while( colorBits < 32 && ncolors > 1 << colorBits )
            colorBits *= 2;
    }
    int squareCount = tileSquares[levelCount-1][0].squareCount;
    int squareBits = tileSquares[levelCount-1][0].squareBits;
    int estimateWithPalette = ncolors * sizeof(int) +
            (squareCount * colorBits + 7) / 8;
    int estimateNoPalette = squareCount * sizeof(int);
    int estimateColorBytes;
    if( estimateWithPalette >= estimateNoPalette ) {
        ncolors = 0;
        colorBits = 32;
        estimateColorBytes = estimateNoPalette;
        free(colors);
        colors = NULL;
        free(hashnext);
        hashnext = NULL;
    }else
        estimateColorBytes = estimateWithPalette;
    int estimateSquareBytes = (squareBits + 7) / 8;
    int encBytes = -1;
    if( estimateColorBytes + estimateSquareBytes + 8 < rectlen ) {
        char *bp = buf;
        // put out color palette
        *bp++ = ncolors >> 24;
        *bp++ = ncolors >> 16;
        *bp++ = ncolors >> 8;
        *bp++ = ncolors;
        memcpy(bp, colors, ncolors * sizeof(int));
        bp += ncolors * sizeof(int);
        *bp++ = squareCount >> 24;
        *bp++ = squareCount >> 16;
        *bp++ = squareCount >> 8;
        *bp++ = squareCount;

        struct EncSubTileParam estp;
        estp.bpColors.bp = bp;
        estp.bpColors.byte = 0;
        estp.bpColors.bits = 0;
        estp.bpColors.avail = (squareCount * colorBits + 7) / 8;
        estp.bpSquares.bp = bp + estp.bpColors.avail;
        estp.bpSquares.byte = 0;
        estp.bpSquares.bits = 0;
        estp.bpSquares.avail = estimateSquareBytes;
        estp.rectWidth = rect->width;
        estp.rectHeight = rect->height;
        estp.colors = colors;
        estp.hasheads = hasheads;
        estp.hashnext = hashnext;
        estp.colorBits = colorBits;
        estp.tileSquares = tileSquares;
        encodeSubTile(&estp, levelCount-1, 0, 0);

        if( estp.bpColors.avail != (estp.bpColors.bits? 1 : 0) )
            log_fatal("encodeTila: wrong colors estimation, avail=%d, bits=%d",
                    estp.bpColors.avail, estp.bpColors.bits);
        if( estp.bpColors.bits )
            *estp.bpColors.bp++ = estp.bpColors.byte << (8-estp.bpColors.bits);

        if( estp.bpSquares.avail != (estp.bpSquares.bits? 1 : 0) )
            log_fatal("encodeTila: wrong squares estimation, avail=%d, bits=%d",
                    estp.bpSquares.avail, estp.bpSquares.bits);
        if( estp.bpSquares.bits )
            *estp.bpSquares.bp++
                = estp.bpSquares.byte << (8 - estp.bpSquares.bits);
        encBytes = estp.bpSquares.bp - buf;
    }else{
        log_debug("encodeTila: estimated size greater than buffer size,"
               " estimated color bytes=%d, square bytes=%d, total=%d, "
               "rectlen=%d", estimateColorBytes, estimateSquareBytes,
               estimateColorBytes + estimateSquareBytes + 8, rectlen);
    }
    free(colors);
    free(hashnext);
    for(int lvl = 0; lvl < levelCount; ++lvl)
        free(tileSquares[lvl]);
    return encBytes;
}

void srvconn_sendRectEncoded(SockStream *strm, const char *prevImg,
        const char *curImg, int bytesPerPixel, int bytesPerLine,
        const RectangleArea *rect)
{
    const CmdLineParams *params = cmdline_getParams();
    EncodingType encType = params->encType;
    CompressionType compr = params->compr;
    int rectlen = rect->width * rect->height * bytesPerPixel;

    if( (encType == ENC_DIFF && prevImg == NULL) ||
            bytesPerPixel != sizeof(int) || bytesPerLine % sizeof(int) != 0 )
        encType = ENC_NONE;
    if( encType != ENC_NONE || compr != COMPR_NONE ) {
        int srclen = -1, complen;
        char *bufsrc, *bufdest;
        const char *compressed;
        unsigned long long tmBeg = curTimeMs();

        bufsrc = malloc(rectlen);
        switch( encType ) {
        case ENC_DIFF:
            srclen = encodeDiff((const unsigned*)prevImg,
                    (const unsigned*)curImg, bytesPerLine / sizeof(int),
                    rect, bufsrc);
            break;
        case ENC_TRLE:
            srclen = encodeTRLE((const unsigned*)curImg,
                    bytesPerLine / sizeof(int), rect, 64, bufsrc);
            break;
        case ENC_TILA:
            srclen = encodeTila((const unsigned*)curImg,
                    bytesPerLine / sizeof(int), rect, bufsrc);
            break;
        case ENC_NONE:
            break;
        }
        if( srclen < 0 ) {
            int dataOff = rect->y * bytesPerLine + rect->x * bytesPerPixel;
            int linelen = rect->width * bytesPerPixel;
            srclen = rectlen;
            int srcOff = 0;

            for(int i = 0; i < rect->height; ++i) {
                memcpy(bufsrc + srcOff, curImg + dataOff, linelen);
                dataOff += bytesPerLine;
                srcOff += linelen;
            }
            encType = ENC_NONE;
        }
        switch( compr ) {
        case COMPR_LZ4:
            complen = LZ4_compressBound(srclen);
            compressed = bufdest = malloc(complen);
            complen = LZ4_compress_fast(bufsrc, bufdest, srclen, complen,
                    params->lz4Level);
            break;
        case COMPR_ZSTD:
            complen = ZSTD_compressBound(srclen);
            compressed = bufdest = malloc(complen);
            complen = ZSTD_compress(bufdest, complen, bufsrc, srclen, 
                    params->zstdLevel);
            break;
        case COMPR_ZLIB:
            {
                complen = compressBound(srclen);
                compressed = bufdest = malloc(complen);
                uLongf dlen = complen;
                if( compress2((Bytef*)bufdest, &dlen, (Bytef*)bufsrc,
                            srclen, params->zlibLevel) != Z_OK )
                    log_fatal("compress2 fail");
                complen = dlen;
            }
            break;
        default:
            bufdest = NULL;
            complen = srclen;
            break;
        }
        if( complen >= srclen ) {
            compressed = bufsrc;
            complen = srclen;
            compr = COMPR_NONE;
        }
        unsigned long long tmCur = curTimeMs();
        if( rect->width > 25 || rect->height > 25 )
            log_info("%7d -> %7d -> %7d, %3d%%  %3llu ms", rectlen, srclen,
                    complen, 100 * complen / rectlen, tmCur - tmBeg);
        sock_writeU32(strm, 0x514c4957);
        sock_writeU16(strm, encType);
        sock_writeU16(strm, compr);
        sock_writeU32(strm, complen);
        if( encType != ENC_NONE )
            sock_writeU32(strm, srclen);
        sock_write(strm, compressed, complen);
        free(bufdest);
        free(bufsrc);
    }else{
        log_info("%7d -> %7d -> %7d, 100%%    0 ms", rectlen, rectlen, rectlen);
        sock_writeU32(strm, 0);
        sock_writeRect(strm, curImg + rect->y * bytesPerLine +
                rect->x * bytesPerPixel, bytesPerLine,
                rect->width * bytesPerPixel, rect->height);
    }
}

