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

void clidisp_decodeWILQ(DisplayConnection *conn, SockStream *strm,
        int x, int y, int width, int height)
{
    int method, srclen, complen, res;
    int bytesPerLine = conn->img->bytes_per_line;
    int bytespp = (conn->img->bits_per_pixel + 7) / 8;
    char *compressed, *uncompressed;

    method = sock_readU32(strm);
    if( method & 8 )
        srclen = sock_readU32(strm);
    else
        srclen = width * height * bytespp; 
    complen = sock_readU32(strm);
    compressed = malloc(complen);
    sock_read(strm, compressed, complen);
    switch( method & 7 ) {
    case 0:
        uncompressed = compressed;
        if( complen != srclen )
            log_fatal("decodeRaw: size does not match, got %d, expected %d",
                    complen, srclen);
        break;
    case 1:
        uncompressed = malloc(srclen);
        res = LZ4_decompress_safe(compressed, uncompressed, complen, srclen);
        if( res != srclen )
            log_fatal("LZ4_decompress_safe: srclen=%d, res=%d", srclen, res);
        break;
    case 2:
        uncompressed = malloc(srclen);
        res = ZSTD_decompress(uncompressed, srclen, compressed, complen);
        if( res != srclen ) {
            if( ZSTD_isError(res) )
                log_fatal("ZSTD_decompress: complen=%d, srclen=%d, res=%d, %s",
                        complen, srclen, res, ZSTD_getErrorName(res));
            log_fatal("ZSTD_decompress: srclen=%d, res=%d", srclen, res);
        }
        break;
    case 3:
        break;
    default:
        log_fatal("unsupported WILQ compression method %d", method);
    }
    if( method & 8 ) {
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
    }else{
        int srcOff = 0;
        int srcLineBytes = width * bytespp;
        int dataOff = y * bytesPerLine + x * bytespp;
        for(int i = 0; i < height; ++i) {
            memcpy(conn->img->data + dataOff, uncompressed + srcOff, 
                    srcLineBytes);
            dataOff += bytesPerLine;
            srcOff += srcLineBytes;
        }
    }
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

