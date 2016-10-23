#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XShm.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/shm.h>
#include <sys/uio.h>
#include <string.h>
#include <sys/select.h>
#include <lz4.h>
#include <zstd.h>
#include <zlib.h>
#include <bzlib.h>
#include "clidisplay.h"
#include "vnclog.h"


struct DisplayConnection {
    Display *d;
    XShmSegmentInfo shmInfo;
    Window win;
    XImage *img;
    GC gc;
    fd_set fds;
    KeySym lastKeysymDown;
};

DisplayConnection *clidisp_open(int width, int height, const char *title,
        int argc, char *argv[], int fullScreen)
{
    Display *d;

    if( (d = XOpenDisplay(NULL)) == NULL )
        log_fatal("unable to open display");
    DisplayConnection *conn = malloc(sizeof(DisplayConnection));
    conn->d = d;
    int defScreenNum = XDefaultScreen(d);
    Visual *defVis = XDefaultVisual(d, defScreenNum);
    int defDepth = XDefaultDepth(d, defScreenNum);
    memset(&conn->shmInfo, 0, sizeof(conn->shmInfo));
    if( XShmQueryExtension(d) ) {
        conn->img = XShmCreateImage(d, defVis, defDepth, ZPixmap, NULL,
                &conn->shmInfo, width, height);
        conn->shmInfo.shmid = shmget(IPC_PRIVATE,
                conn->img->bytes_per_line * conn->img->height, IPC_CREAT|0777);
        log_debug("shm id: %d", conn->shmInfo.shmid);
        conn->shmInfo.shmaddr =
            conn->img->data = shmat (conn->shmInfo.shmid, 0, 0);
        conn->shmInfo.readOnly = False;
        Status st = XShmAttach(d, &conn->shmInfo);
        shmctl(conn->shmInfo.shmid, IPC_RMID, NULL);
        if( st == 0 )
            log_fatal("XShmAttach failed");
    }else{
        log_info("shm extension is not available");
        conn->img = XCreateImage(d, defVis, defDepth, ZPixmap, 0, NULL,
                width, height, 32, 0);
        conn->img->data = malloc(conn->img->bytes_per_line * height);
    }
    XSetWindowAttributes attrs;
    attrs.background_pixel = 0x204060;
    attrs.event_mask = KeyPressMask | KeyReleaseMask |
        ButtonPressMask | ButtonReleaseMask | PointerMotionMask |
        FocusChangeMask | ExposureMask;
    attrs.override_redirect = fullScreen;
    // create dummy cursor
    Pixmap pixmap = XCreatePixmap(d, XDefaultRootWindow(d), 1, 1, 1);
    XColor color;
    memset(&color, 0, sizeof(color));
    attrs.cursor = XCreatePixmapCursor(d, pixmap, pixmap, &color, &color, 0, 0);
    XFreePixmap(d, pixmap);
    Screen *defScreen = XDefaultScreenOfDisplay(d);
    conn->win = XCreateWindow(d, XDefaultRootWindow(d), 0, 0,
            fullScreen ? XWidthOfScreen(defScreen) : width,
            fullScreen ? XHeightOfScreen(defScreen) : height,
            0, CopyFromParent, InputOutput, CopyFromParent,
            CWBackPixel | CWEventMask | CWCursor | CWOverrideRedirect, &attrs);
    Atom WM_DELETE_WINDOW = XInternAtom(d, "WM_DELETE_WINDOW", False); 
    XSetWMProtocols(d, conn->win, &WM_DELETE_WINDOW, 1);
    char titlebuf[256];
    sprintf(titlebuf, "%.240s - Wilq VNC", title);
    XClassHint classHint;
    classHint.res_name = "wilqvnc";
    classHint.res_class = "WilqVNC";
    XmbSetWMProperties(d, conn->win, titlebuf, NULL, argv,
            argc, NULL, NULL, &classHint);
    XMapWindow(d, conn->win);
    if( fullScreen )
        XGrabKeyboard(d, conn->win, True, GrabModeAsync,
                GrabModeAsync, CurrentTime);
    XFlush(d);
    conn->gc = XCreateGC(conn->d, conn->win, 0, NULL);
    FD_ZERO(&conn->fds);
    conn->lastKeysymDown = NoSymbol;
    return conn;
}

static void extractShiftMaxFromMask(unsigned long mask,
        unsigned *resMax, unsigned *resShift)
{
    unsigned shift = 0;

    if( mask == 0 )
        log_fatal("your display is not true-color one");
    while( (mask & 1) == 0 ) {
        mask >>= 1;
        ++shift;
    } 
    *resMax = mask;
    *resShift = shift;
}

void clidisp_getPixelFormat(DisplayConnection *conn, PixelFormat *pixelFormat)
{
    pixelFormat->bitsPerPixel = conn->img->bits_per_pixel;
    int defScreenNum = XDefaultScreen(conn->d);
    pixelFormat->depth = XDefaultDepth(conn->d, defScreenNum);
    pixelFormat->bigEndian = conn->img->byte_order == MSBFirst;
    pixelFormat->trueColor = True;
    extractShiftMaxFromMask(conn->img->red_mask, &pixelFormat->maxRed,
            &pixelFormat->shiftRed);
    extractShiftMaxFromMask(conn->img->green_mask, &pixelFormat->maxGreen,
            &pixelFormat->shiftGreen);
    extractShiftMaxFromMask(conn->img->blue_mask, &pixelFormat->maxBlue,
            &pixelFormat->shiftBlue);
    log_debug("pixel format:");
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
}

void clidisp_flush(DisplayConnection *conn)
{
    if( conn->shmInfo.shmaddr != NULL ) {
        XShmPutImage(conn->d, conn->win, conn->gc, conn->img, 0, 0, 0, 0,
                conn->img->width, conn->img->height, False);
    }else{
        XPutImage(conn->d, conn->win, conn->gc, conn->img, 0, 0, 0, 0,
                conn->img->width, conn->img->height);
    }
    XFlush(conn->d);
}

static unsigned convertMouseButtonState(unsigned state)
{
    return (state & Button1Mask ? 1 : 0) | (state & Button2Mask ? 2 : 0) |
            (state & Button3Mask ? 4 : 0) | (state & Button4Mask ? 8 : 0) |
            (state & Button5Mask ? 16 : 0);
}

static void processPendingEvents(DisplayConnection *conn,
        DisplayEvent *displayEvent, Bool assumeFirstIsPending)
{
    XEvent xev;
    KeySym keysym;

    while( displayEvent->evType == VET_NONE &&
            (assumeFirstIsPending || XPending(conn->d) != 0) )
    {
        XNextEvent(conn->d, &xev);
        assumeFirstIsPending = False;
        switch( xev.type ) {
        case KeyPress:
            XLookupString(&xev.xkey, NULL, 0, &keysym, NULL);
            if( keysym != NoSymbol ) {
                displayEvent->evType = VET_KEY;
                displayEvent->kev.isDown = 1;
                displayEvent->kev.keysym = keysym;
                conn->lastKeysymDown = keysym;
            }
            break;
        case KeyRelease:
            XLookupString(&xev.xkey, NULL, 0, &keysym, NULL);
            if( keysym != NoSymbol ) {
                conn->lastKeysymDown = NoSymbol;
                displayEvent->evType = VET_KEY;
                displayEvent->kev.isDown = 0;
                displayEvent->kev.keysym = keysym;
            }
            break;
        case FocusIn:
            break;
        case FocusOut:
#if 0
            if( conn->isFullScreen ) {
                // XXX: why focus goes nowhere when xscreensaver turns "on"
                Window win;
                int d;
                XGetInputFocus(conn->d, &win, &d);
                if( win == None && d == 0 ) {
                    log_debug("set input focus to mine");
                    XSetInputFocus(conn->d, conn->win,
                            RevertToPointerRoot, CurrentTime);
                }
            }
#endif
            if( conn->lastKeysymDown != NoSymbol ) {
                log_debug("send key %lu UP on leave",
                        conn->lastKeysymDown);
                // mimic keyup
                displayEvent->evType = VET_KEY;
                displayEvent->kev.isDown = 0;
                displayEvent->kev.keysym = conn->lastKeysymDown;
                conn->lastKeysymDown = NoSymbol;
            }
            break;
        case ButtonPress:
            displayEvent->evType = VET_MOUSE;
            displayEvent->pev.x = xev.xbutton.x;
            displayEvent->pev.y = xev.xbutton.y;
            displayEvent->pev.buttonMask =
                convertMouseButtonState(xev.xbutton.state |
                (xev.xbutton.button == Button1 ? Button1Mask : 0) |
                (xev.xbutton.button == Button2 ? Button2Mask : 0) |
                (xev.xbutton.button == Button3 ? Button3Mask : 0) |
                (xev.xbutton.button == Button4 ? Button4Mask : 0) |
                (xev.xbutton.button == Button5 ? Button5Mask : 0));
            break;
        case ButtonRelease:
            displayEvent->evType = VET_MOUSE;
            displayEvent->pev.x = xev.xbutton.x;
            displayEvent->pev.y = xev.xbutton.y;
            displayEvent->pev.buttonMask =
                convertMouseButtonState(xev.xbutton.state &
                ~(xev.xbutton.button == Button1 ? Button1Mask : 0) &
                ~(xev.xbutton.button == Button2 ? Button2Mask : 0) &
                ~(xev.xbutton.button == Button3 ? Button3Mask : 0) &
                ~(xev.xbutton.button == Button4 ? Button4Mask : 0) &
                ~(xev.xbutton.button == Button5 ? Button5Mask : 0));
            break;
        case MotionNotify:
            displayEvent->evType = VET_MOUSE;
            displayEvent->pev.x = xev.xbutton.x;
            displayEvent->pev.y = xev.xbutton.y;
            displayEvent->pev.buttonMask =
                convertMouseButtonState(xev.xbutton.state);
            break;
        case Expose:
            clidisp_flush(conn);
            break;
        case ClientMessage:
            // assume WM_DELETE_WINDOW
            displayEvent->evType = VET_CLOSE;
            break;
        default:
            log_info("unhandled event: %d", xev.type);
            break;
        }
    }
}

int clidisp_nextEvent(DisplayConnection *conn, SockStream *strm,
        DisplayEvent *displayEvent, int wait)
{
    Bool isEvFd = sock_isDataAvail(strm);
    struct timeval tmout;

    tmout.tv_sec = 0;
    tmout.tv_usec = 0;
    displayEvent->evType = VET_NONE;
    processPendingEvents(conn, displayEvent, False);
    while( displayEvent->evType == VET_NONE && !isEvFd ) {
        int dispFd = XConnectionNumber(conn->d);
        int sockFd = sock_fd(strm);
        FD_SET(dispFd, &conn->fds);
        FD_SET(sockFd, &conn->fds);
        int selCnt = select((dispFd > sockFd ? dispFd : sockFd)+1,
                &conn->fds, NULL, NULL, wait ? NULL : &tmout);
        if( selCnt < 0 )
            log_fatal_errno("select");
        if( selCnt == 0 )
            break;  // no wait, no data pending
        if( FD_ISSET(dispFd, &conn->fds) ) {
            FD_CLR(dispFd, &conn->fds);
            processPendingEvents(conn, displayEvent, True);
        }
        if( FD_ISSET(sockFd, &conn->fds) ) {
            FD_CLR(sockFd, &conn->fds);
            isEvFd = True;
        }
    }
    return isEvFd;
}

void clidisp_putRectFromSocket(DisplayConnection *conn, SockStream *strm,
        int x, int y, int width, int height)
{
    int bytesPerLine = conn->img->bytes_per_line;
    int bytespp = (conn->img->bits_per_pixel + 7) / 8;

    sock_readRect(strm, conn->img->data + y * bytesPerLine + x * bytespp,
            bytesPerLine, width * bytespp, height);
}

struct BitUnpacker {
    const unsigned char *bp;
    char byte;
    unsigned bits;
    unsigned avail;
};

static unsigned getBits(struct BitUnpacker *bp, unsigned dataBitCount)
{
    unsigned res = 0, bitsRemain = dataBitCount;
    while( bitsRemain ) {
        if( bp->bits == 0 ) {
            if( bp->avail == 0 )
                log_fatal("getBits: buffer empty");
            bp->byte = *bp->bp++;
            bp->bits = 8;
            --bp->avail;
        }
        unsigned bitCount = bp->bits;
        if( bitCount > bitsRemain )
            bitCount = bitsRemain;
        unsigned dataBits =  (bp->byte >> (bp->bits - bitCount))
            & ((1 << bitCount) - 1);
        res = res << bitCount | dataBits;
        bp->bits -= bitCount;
        bitsRemain -= bitCount;
    }
    return res;
}

static void decodeSubTile(struct BitUnpacker *bpColors,
        struct BitUnpacker *bpSquares,
        int lvl, int x, int y, int rectWidth, int rectHeight,
        unsigned *colors, unsigned colorBits,
        unsigned *img, int itemsPerLine)
{
    int isSplit;

    if( lvl == 0 ) {
        isSplit = 0;
    }else{
        isSplit = getBits(bpSquares, 1);
    }
    if( isSplit ) {
        int psubWidth = 1 << (lvl-1);
        int phcount = (rectWidth + psubWidth - 1) / psubWidth;
        int pvcount = (rectHeight + psubWidth - 1) / psubWidth;
        for(int sy = 0; sy < 2; ++sy) {
            int py = 2 * y + sy;
            if( py < pvcount ) {
                for(int sx = 0; sx < 2; ++sx) {
                    int px = 2 * x + sx;
                    if( px < phcount ) {
                        decodeSubTile(bpColors, bpSquares, lvl-1, px, py,
                                rectWidth, rectHeight, colors, colorBits, img,
                                itemsPerLine);
                    }
                }
            }
        }
    }else{
        unsigned color = getBits(bpColors, colorBits);
        //printf("   decodeSubTile: lvl=%d, x=%d, y=%d color=%d\n",
        //            lvl, x, y, colorIdx);
        if( colors != NULL )
            color = colors[color];
        int subWidth = 1 << lvl;
        for(int sy = y * subWidth; sy < (y+1) * subWidth && sy < rectHeight;
                ++sy)
        {
            unsigned *imgRow = img + sy * itemsPerLine;
            for(int sx = x * subWidth; sx < (x+1) * subWidth && sx < rectWidth;
                    ++sx)
                imgRow[sx] = color;
        }
    }
}

static void decodeTila(const void *data, int datalen, unsigned *img,
        int x, int y, int width, int height, int itemsPerLine)
{
    const unsigned char *bp = data;
    unsigned *colors, ncolors, colorBits, squareCount;

    ncolors = *bp++ << 24;
    ncolors |= *bp++ << 16;
    ncolors |= *bp++ << 8;
    ncolors |= *bp++;
    if( ncolors > 0 ) {
        colors = malloc(ncolors * sizeof(int));
        memcpy(colors, bp, ncolors * sizeof(int));
        bp += ncolors * sizeof(int);
        if( ncolors <= 1 )
            colorBits = 0;
        else{
            colorBits = 1;
            while( colorBits < 32 && ncolors > 1 << colorBits )
                colorBits *= 2;
        }
    }else{
        colors = NULL;
        colorBits = sizeof(int) * 8;
    }
    int tileWidth = 1;
    int levelCount = 1;
    while( tileWidth < width || tileWidth < height ) {
        tileWidth *= 2;
        ++levelCount;
    }
    squareCount = *bp++ << 24;
    squareCount |= *bp++ << 16;
    squareCount |= *bp++ << 8;
    squareCount |= *bp++;
    struct BitUnpacker bpColors, bpSquares;
    bpColors.bp = bp;
    bpColors.byte = 0;
    bpColors.bits = 0;
    bpColors.avail = (squareCount * colorBits + 7) / 8;
    bpSquares.bp = bp + bpColors.avail;
    bpSquares.byte = 0;
    bpSquares.bits = 0;
    bpSquares.avail = datalen - (bp - (const unsigned char*)data)
        - bpColors.avail;
    decodeSubTile(&bpColors, &bpSquares, levelCount-1, 0, 0, width, height,
            colors, colorBits, img + y * itemsPerLine + x, itemsPerLine);
    if( bpColors.avail != 0 )
        log_fatal("decodeTila: unexpected extra color bytes count=%d",
                bpColors.avail);
    free(colors);
}

void clidisp_decodeWILQ(DisplayConnection *conn, SockStream *strm,
        int x, int y, int width, int height)
{
    int encMethod, comprMethod, srclen, complen, res;
    int bytesPerLine = conn->img->bytes_per_line;
    int bytespp = (conn->img->bits_per_pixel + 7) / 8;
    char *compressed, *uncompressed;

    encMethod = sock_readU16(strm);
    comprMethod = sock_readU16(strm);
    complen = sock_readU32(strm);
    if( encMethod != ENC_NONE )
        srclen = sock_readU32(strm);
    else
        srclen = width * height * bytespp; 
    compressed = malloc(complen);
    sock_read(strm, compressed, complen);
    switch( comprMethod ) {
    case COMPR_NONE:
        uncompressed = compressed;
        if( complen != srclen )
            log_fatal("decodeRaw: size does not match, got %d, expected %d",
                    complen, srclen);
        break;
    case COMPR_LZ4:
        uncompressed = malloc(srclen);
        res = LZ4_decompress_safe(compressed, uncompressed, complen, srclen);
        if( res != srclen )
            log_fatal("LZ4_decompress_safe: srclen=%d, res=%d", srclen, res);
        break;
    case COMPR_ZSTD:
        uncompressed = malloc(srclen);
        res = ZSTD_decompress(uncompressed, srclen, compressed, complen);
        if( res != srclen ) {
            if( ZSTD_isError(res) )
                log_fatal("ZSTD_decompress: complen=%d, srclen=%d, res=%d, %s",
                        complen, srclen, res, ZSTD_getErrorName(res));
            log_fatal("ZSTD_decompress: srclen=%d, res=%d", srclen, res);
        }
        break;
    case COMPR_ZLIB:
        {
            uncompressed = malloc(srclen);
            uLongf slen = srclen;
            if( uncompress((Bytef*)uncompressed, &slen,
                        (Bytef*)compressed, complen) != Z_OK )
                log_fatal("uncompress error");
            if( slen != srclen )
                log_fatal("uncompress size mismatch");
        }
        break;
    case COMPR_BZ2:
        {
            uncompressed = malloc(srclen);
            unsigned slen = srclen;
            if( BZ2_bzBuffToBuffDecompress(uncompressed, &slen,
                        compressed, complen, 0, 0) != BZ_OK )
                log_fatal("BZ2_bzBuffToBuffDecompress error");
            if( slen != srclen )
                log_fatal("BZ2_bzBuffToBuffDecompress size mismatch");
        }
        break;
    default:
        log_fatal("unsupported WILQ compression method %d", comprMethod);
    }
    if( encMethod == ENC_DIFF ) {
        int itemsPerLine = bytesPerLine / sizeof(int);
        const unsigned *buf = (const unsigned*)uncompressed;
        unsigned *img = (unsigned*)(conn->img->data + y * bytesPerLine
                + x * sizeof(int));
        int col = 0, row = 0, nitems = srclen / sizeof(int);
        for(int i = 0; i < nitems; ++i) {
            if( row >= height )
                log_fatal("decodeDiff: bad data, %dx%d, row=%d, col=%d, "
                        "i=%d nitems=%d", width, height, row, col, i, nitems);
            unsigned b = buf[i];
            img[col] = b & 0xffffff;
            col += (b >> 24) + 1;
            while( col >= width ) {
                img += itemsPerLine;
                ++row;
                col -= width;
            }
            //log_info("%d %d->(%d, %d)", i, (b >> 24) + 1, col, row);
        }
        if( row != height || col != 0 )
            log_fatal("decodeDiff: bad data, %dx%d, row=%d, col=%d, "
                        "nitems=%d", width, height, row, col, nitems);
    }else if( encMethod == ENC_TRLE ) {
        clidisp_decodeTRLE(conn, uncompressed, srclen, x, y, width, height, 64);
    }else if( encMethod == ENC_TILA ) {
        int itemsPerLine = bytesPerLine / bytespp;

        decodeTila(uncompressed, srclen, (unsigned*)conn->img->data,
                x, y, width, height, itemsPerLine);
    }else if( encMethod == ENC_NONE ) {
        int srcOff = 0;
        int srcLineBytes = width * bytespp;
        int dataOff = y * bytesPerLine + x * bytespp;
        for(int i = 0; i < height; ++i) {
            memcpy(conn->img->data + dataOff, uncompressed + srcOff, 
                    srcLineBytes);
            dataOff += bytesPerLine;
            srcOff += srcLineBytes;
        }
    }else
        log_fatal("unsupported WILQ encoding method %d", encMethod);
    free(compressed);
    if( uncompressed != compressed )
        free(uncompressed);
}

void clidisp_copyRect(DisplayConnection *conn, int srcX, int srcY,
        int destX, int destY, int width, int height)
{
    int i, bytespp = (conn->img->bits_per_pixel + 7) / 8;

    if( srcY >= destY ) {
        for(i = 0; i < height; ++i) {
            memmove(conn->img->data +
                    (destY+i) * conn->img->bytes_per_line +
                    destX * bytespp,
                    conn->img->data +
                    (srcY+i) * conn->img->bytes_per_line +
                    srcX * bytespp, width * bytespp);
        }
    }else{
        for(i = height-1; i >= 0; --i) {
            memmove(conn->img->data + (destY+i) *
                    conn->img->bytes_per_line + destX * bytespp,
                    conn->img->data + (srcY+i) *
                    conn->img->bytes_per_line + srcX * bytespp,
                    width * bytespp);
        }
    }
}

void clidisp_fillRect(DisplayConnection *conn, const char *pixel, int x, int y,
        int width, int height)
{
    int i, bytespp = (conn->img->bits_per_pixel + 7) / 8;

    for(i = 0; i < width; ++i) {
        memcpy(conn->img->data + y * conn->img->bytes_per_line +
                (x+i) * bytespp, pixel, bytespp);
    }
    for(i = 1; i < height; ++i) {
        memcpy(conn->img->data + (y+i) * conn->img->bytes_per_line +
                x * bytespp,
                conn->img->data + y * conn->img->bytes_per_line + x * bytespp,
                width * bytespp);
    }
}

void clidisp_decodeTRLE(DisplayConnection *conn, const void *data, int datalen,
        int x, int y, int width, int height, unsigned squareWidth)
{
    const unsigned char *dp = data;
    int sx, sy, i, j;
    int bytespp = (conn->img->bits_per_pixel + 7) / 8;
    int itemsPerLine = conn->img->bytes_per_line / bytespp;
    unsigned imgOff = y * itemsPerLine + x;
    unsigned *img = (unsigned*)conn->img->data;
    unsigned colors[128], ncolorsLast = 0;

    for(sy = 0; sy < height; sy += squareWidth) {
        unsigned tileHeight = height - sy > squareWidth ? squareWidth :
            height - sy;
        for(sx = 0; sx < width; sx += squareWidth) {
            unsigned tileWidth = width - sx > squareWidth ? squareWidth :
                width - sx;
            unsigned tileOff = imgOff + sx;
            unsigned ncolors = *dp++;
            if( ncolors == 0 ) {
                for(i = 0; i < tileHeight; ++i) {
                    for(j = 0; j < tileWidth; ++j) {
                        // TODO: this depends on endianess
                        unsigned color = *dp++;
                        color |= *dp++ << 8;
                        color |= *dp++ << 16;
                        img[tileOff + j] = color;
                    }
                    tileOff += itemsPerLine;
                }
            }else if( ncolors == 128 ) {
                unsigned run = 0, r, color;
                for(i = 0; i < tileHeight; ++i) {
                    for(j = 0; j < tileWidth; ++j) {
                        if( run == 0 ) {
                            // TODO: this depends on endianess
                            color = *dp++;
                            color |= *dp++ << 8;
                            color |= *dp++ << 16;

                            run = 1;
                            while( (r = *dp++) == 255 )
                                run += 255;
                            run += r;
                        }
                        img[tileOff + j] = color;
                        --run;
                    }
                    tileOff += itemsPerLine;
                }
            }else{
                if( ncolors == 127 || ncolors == 129 ) {
                    ncolors = ncolorsLast | (ncolors & 0x80);
                }else{
                    ncolorsLast = ncolors & 0x7f;
                    for(i = 0; i < ncolorsLast; ++i) {
                        // TODO: this depends on endianess
                        unsigned color = *dp++;
                        color |= *dp++ << 8;
                        color |= *dp++ << 16;
                        colors[i] = color;
                    }
                }
                if( ncolors < 128 ) {
                    unsigned shift, mask;
                    if( ncolors == 1 ) {
                        shift = 0;
                    }else if( ncolors == 2 ) {
                        shift = 1;
                    }else if( ncolors <= 4 ) {
                        shift = 2;
                    }else if( ncolors <= 16 )
                        shift = 4;
                    else
                        shift = 8;
                    mask = (1 << shift) - 1;
                    for(i = 0; i < tileHeight; ++i) {
                        unsigned bcount = shift == 0 ? 0 : 8, b;
                        for(j = 0; j < tileWidth; ++j) {
                            if( bcount == 8 ) {
                                b = *dp++;
                                bcount = 0;
                            }
                            b <<= shift;
                            img[tileOff + j] = colors[(b >> 8) & mask];
                            bcount += shift;
                        }
                        tileOff += itemsPerLine;
                    }
                }else{  // RLE
                    unsigned b, run = 0, r;
                    for(i = 0; i < tileHeight; ++i) {
                        for(j = 0; j < tileWidth; ++j) {
                            if( run == 0 ) {
                                b = *dp++;
                                run = 1;
                                if( b & 0x80 ) {
                                    while( (r = *dp++) == 255 )
                                        run += 255;
                                    run += r;
                                    b &= 0x7f;
                                }
                            }
                            img[tileOff + j] = colors[b];
                            --run;
                        }
                        tileOff += itemsPerLine;
                    }
                }
            }
        }
        imgOff += squareWidth * itemsPerLine;
    }
    if( dp - (const unsigned char*)data != datalen )
        log_fatal("ZRLE data length mismatch: got %d bytes, used %d bytes",
                datalen, dp - (const unsigned char*)data);
}

void clidisp_close(DisplayConnection *conn)
{
    if( conn != NULL ) {
        if( conn->shmInfo.shmaddr != NULL ) {
            XShmDetach(conn->d, &conn->shmInfo);
            shmdt(conn->shmInfo.shmaddr);
        }
        XDestroyImage(conn->img);
        XDestroyWindow(conn->d, conn->win);
        XCloseDisplay(conn->d);
    }
    free(conn);
}

