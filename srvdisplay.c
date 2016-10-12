#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xdamage.h>
#include <X11/extensions/XShm.h>
#include <X11/extensions/XTest.h>
#include <X11/extensions/Xfixes.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/shm.h>
#include <sys/uio.h>
#include <string.h>
#include <sys/select.h>
#include "srvdisplay.h"
#include "vnclog.h"


struct DisplayConnection {
    Display *d;
    XShmSegmentInfo shmInfo1;
    XShmSegmentInfo shmInfo2;
    XImage *prevImg;
    XImage *curImg;
    Damage damageId;
    int xdamage_evbase;
    int xfixes_evbase;
    fd_set fds;
    VncPointerEvent lastPointerEv;
    DamageArea curDamage;       // currently damaged region
    XFixesCursorImage *cursorImg;
};

DisplayConnection *srvdisp_open(void)
{
    Display *d;
    int xtest_evbase, xtest_errbase, xtest_majorver, xtest_minorver;
    int xdamage_evbase, xdamage_errbase;
    int xfixes_evbase, xfixes_errbase;

    if( (d = XOpenDisplay(NULL)) == NULL )
        log_fatal("unable to open display");
    if( ! XDamageQueryExtension(d, &xdamage_evbase, &xdamage_errbase) )
        log_fatal("XDamage extension is not supported by server");
    if( ! XTestQueryExtension(d, &xtest_evbase, &xtest_errbase, &xtest_majorver,
                &xtest_minorver) )
        log_fatal("XTEST extension is not supported by server");
    log_info("XTest version %d.%d", xtest_majorver, xtest_minorver);
    if( ! XFixesQueryExtension(d, &xfixes_evbase, &xfixes_errbase) )
        log_fatal("XFixes extension is not supported by server");
    DisplayConnection *conn = malloc(sizeof(DisplayConnection));
    conn->d = d;
    int defScreenNum = XDefaultScreen(d);
    Visual *defVis = XDefaultVisual(d, defScreenNum);
    int defDepth = XDefaultDepth(d, defScreenNum);
    memset(&conn->shmInfo1, 0, sizeof(conn->shmInfo1));
    memset(&conn->shmInfo2, 0, sizeof(conn->shmInfo2));
    int width = XDisplayWidth(d, defScreenNum);
    int height = XDisplayHeight(d, defScreenNum);
    if( XShmQueryExtension(d) ) {
        conn->prevImg = XShmCreateImage(d, defVis, defDepth, ZPixmap, NULL,
                &conn->shmInfo1, width, height);
        conn->shmInfo1.shmid = shmget(IPC_PRIVATE,
                conn->prevImg->bytes_per_line * conn->prevImg->height,
                IPC_CREAT|0777);
        log_debug("shm1 id: %d", conn->shmInfo1.shmid);
        conn->shmInfo1.shmaddr =
            conn->prevImg->data = shmat (conn->shmInfo1.shmid, 0, 0);
        conn->shmInfo1.readOnly = False;
        Status st = XShmAttach(d, &conn->shmInfo1);
        shmctl(conn->shmInfo1.shmid, IPC_RMID, NULL);
        if( st == 0 )
            log_fatal("XShmAttach failed");
        conn->curImg = XShmCreateImage(d, defVis, defDepth, ZPixmap, NULL,
                &conn->shmInfo2, width, height);
        conn->shmInfo2.shmid = shmget(IPC_PRIVATE,
                conn->curImg->bytes_per_line * conn->curImg->height,
                IPC_CREAT|0777);
        log_debug("shm1 id: %d", conn->shmInfo2.shmid);
        conn->shmInfo2.shmaddr =
            conn->curImg->data = shmat (conn->shmInfo2.shmid, 0, 0);
        conn->shmInfo2.readOnly = False;
        st = XShmAttach(d, &conn->shmInfo2);
        shmctl(conn->shmInfo2.shmid, IPC_RMID, NULL);
        if( st == 0 )
            log_fatal("XShmAttach failed");
    }else{
        log_info("shm extension is not available");
        conn->prevImg = XCreateImage(d, defVis, defDepth, ZPixmap, 0, NULL,
                width, height, 32, 0);
        conn->prevImg->data = malloc(conn->prevImg->bytes_per_line * height);
        conn->curImg = XCreateImage(d, defVis, defDepth, ZPixmap, 0, NULL,
                width, height, 32, 0);
        conn->curImg->data = malloc(conn->curImg->bytes_per_line * height);
    }
    conn->damageId = XDamageCreate(d, XDefaultRootWindow(d),
                XDamageReportRawRectangles);
    XFixesSelectCursorInput(d, XDefaultRootWindow(conn->d),
            XFixesDisplayCursorNotifyMask);
    conn->xdamage_evbase = xdamage_evbase;
    conn->xfixes_evbase = xfixes_evbase;
    FD_ZERO(&conn->fds);
    conn->lastPointerEv.x = conn->lastPointerEv.y = -1;
    conn->lastPointerEv.buttonMask = 0;
    conn->curDamage.x = conn->curDamage.y = 0;
    conn->curDamage.width = width;
    conn->curDamage.height = height;
    conn->cursorImg = XFixesGetCursorImage(d);
    return conn;
}

int srvdisp_getWidth(DisplayConnection *conn)
{
    return XDisplayWidth(conn->d, XDefaultScreen(conn->d));
}

int srvdisp_getHeight(DisplayConnection *conn)
{
    return XDisplayHeight(conn->d, XDefaultScreen(conn->d));
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

void srvdisp_getPixelFormat(DisplayConnection *conn, PixelFormat *pixelFormat)
{
    pixelFormat->bitsPerPixel = conn->curImg->bits_per_pixel;
    int defScreenNum = XDefaultScreen(conn->d);
    pixelFormat->depth = XDefaultDepth(conn->d, defScreenNum);
    pixelFormat->bigEndian = conn->curImg->byte_order == MSBFirst;
    pixelFormat->trueColor = True;
    extractShiftMaxFromMask(conn->curImg->red_mask, &pixelFormat->maxRed,
            &pixelFormat->shiftRed);
    extractShiftMaxFromMask(conn->curImg->green_mask, &pixelFormat->maxGreen,
            &pixelFormat->shiftGreen);
    extractShiftMaxFromMask(conn->curImg->blue_mask, &pixelFormat->maxBlue,
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

void srvdisp_generateKeyEvent(DisplayConnection *conn, const VncKeyEvent *ev)
{
    KeyCode keycode = XKeysymToKeycode(conn->d, ev->keysym);
    if( keycode != 0 )
        XTestFakeKeyEvent(conn->d, keycode, ev->isDown, CurrentTime);
    else
        log_info("warn: unknown keycode for keysym %d", ev->keysym);
}

void srvdisp_generatePointerEvent(DisplayConnection *conn,
        const VncPointerEvent *ev)
{
    unsigned curMask = ev->buttonMask;
    unsigned maskDiff = conn->lastPointerEv.buttonMask ^ curMask;

    if( ev->x != conn->lastPointerEv.x || ev->y != conn->lastPointerEv.y )
        XTestFakeMotionEvent(conn->d, XDefaultScreen(conn->d), ev->x, ev->y,
                CurrentTime);
    if( maskDiff & 1 )
        XTestFakeButtonEvent(conn->d, Button1, curMask & 1, CurrentTime);
    if( maskDiff & 2 )
        XTestFakeButtonEvent(conn->d, Button2, curMask & 2, CurrentTime);
    if( maskDiff & 4 )
        XTestFakeButtonEvent(conn->d, Button3, curMask & 4, CurrentTime);
    if( maskDiff & 8 )
        XTestFakeButtonEvent(conn->d, Button4, curMask & 8, CurrentTime);
    if( maskDiff & 16 )
        XTestFakeButtonEvent(conn->d, Button5, curMask & 16, CurrentTime);
    conn->lastPointerEv = *ev;
}

static void processPendingEvents(DisplayConnection *conn,
        DisplayEvent *displayEvent, Bool assumeFirstIsPending)
{
    XEvent xev;

    while( displayEvent->evType == VET_NONE &&
            (assumeFirstIsPending || XPending(conn->d) != 0) )
    {
        XNextEvent(conn->d, &xev);
        assumeFirstIsPending = False;
        if( xev.type == conn->xdamage_evbase + XDamageNotify ) {
            XDamageNotifyEvent *ev = (XDamageNotifyEvent*)&xev;
            //log_info("area: x=%-3d y=%-3d %3dx%-3d",
            //    ev->area.x, ev->area.y, ev->area.width, ev->area.height);
            if( conn->curDamage.width == 0 && conn->curDamage.height == 0 ) {
                displayEvent->evType = VET_DAMAGE;
                conn->curDamage.x = ev->area.x;
                conn->curDamage.y = ev->area.y;
                conn->curDamage.width = ev->area.width;
                conn->curDamage.height = ev->area.height;
            }else{
                int endX = conn->curDamage.x + conn->curDamage.width;
                int endY = conn->curDamage.y + conn->curDamage.height;
                if( ev->area.x < conn->curDamage.x )
                    conn->curDamage.x = ev->area.x;
                if( ev->area.y < conn->curDamage.y )
                    conn->curDamage.y = ev->area.y;
                if( ev->area.x + ev->area.width > endX )
                    endX = ev->area.x + ev->area.width;
                if( ev->area.y + ev->area.height > endY )
                    endY = ev->area.y + ev->area.height;
                conn->curDamage.width = endX - conn->curDamage.x;
                conn->curDamage.height = endY - conn->curDamage.y;
            }
        }else if( xev.type == conn->xfixes_evbase + XFixesCursorNotify ) {
            XFree(conn->cursorImg);
            conn->cursorImg = XFixesGetCursorImage(conn->d);
            displayEvent->evType = VET_CURSOR;
        }else
            log_info("unhandled event: %d", xev.type);
    }
}

int srvdisp_nextEvent(DisplayConnection *conn, SockStream *strm,
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

void srvdisp_refreshDamagedImageRegion(DisplayConnection *conn,
        DamageArea *refreshedArea)
{
    if( conn->curDamage.width != 0 && conn->curDamage.height != 0 ) {
        XDamageSubtract(conn->d, conn->damageId, None, None);
        Window win = XDefaultRootWindow(conn->d);
        XImage *img = conn->prevImg;
        conn->prevImg = conn->curImg;
        conn->curImg = img;
        if( conn->shmInfo1.shmaddr != NULL ) {
            XShmGetImage(conn->d, win, conn->curImg, 0, 0, -1);
        }else{
            XGetSubImage(conn->d, win, 0, 0, conn->curImg->width,
                    conn->curImg->height, -1, ZPixmap, conn->curImg, 0, 0);
        }
        *refreshedArea = conn->curDamage;
        conn->curDamage.width = conn->curDamage.height = 0;
        int bytesPerLine = conn->curImg->bytes_per_line;
        int bytespp = (conn->curImg->bits_per_pixel + 7) / 8;
        int dataOff = refreshedArea->y * bytesPerLine
            + refreshedArea->x * bytespp; 
        int dataLen = refreshedArea->width * bytespp;
        while( refreshedArea->height > 0 &&
                !memcmp(conn->prevImg->data + dataOff,
                    conn->curImg->data + dataOff, dataLen) )
        {
            ++refreshedArea->y;
            --refreshedArea->height;
            dataOff += bytesPerLine;
        }
        dataOff = (refreshedArea->y + refreshedArea->height - 1) * bytesPerLine
            + refreshedArea->x * bytespp; 
        while( refreshedArea->height > 0 &&
                !memcmp(conn->prevImg->data + dataOff,
                    conn->curImg->data + dataOff, dataLen) )
        {
            --refreshedArea->height;
            dataOff -= bytesPerLine;
        }
        // TODO: handle depth 16 and 8
        if( bytespp == sizeof(int) ) {
            dataOff = refreshedArea->y * bytesPerLine
                + refreshedArea->x * sizeof(int); 
            while( refreshedArea->width > 0 ) {
                int i, off = dataOff, isColEq = 1;
                for(i = 0; i < refreshedArea->height && isColEq; ++i) {
                    isColEq = *(int*)(conn->prevImg->data + off) ==
                        *(int*)(conn->curImg->data + off);
                    off += bytesPerLine;
                }
                if( ! isColEq )
                    break;
                dataOff += sizeof(int);
                ++refreshedArea->x;
                --refreshedArea->width;
            }
            dataOff = refreshedArea->y * bytesPerLine
                + (refreshedArea->x + refreshedArea->width - 1) * sizeof(int); 
            while( refreshedArea->width > 0 ) {
                int i, off = dataOff, isColEq = 1;
                for(i = 0; i < refreshedArea->height && isColEq; ++i) {
                    isColEq = *(int*)(conn->prevImg->data + off) ==
                        *(int*)(conn->curImg->data + off);
                    off += bytesPerLine;
                }
                if( ! isColEq )
                    break;
                dataOff -= sizeof(int);
                --refreshedArea->width;
            }
        }
    }else{
        refreshedArea->width = refreshedArea->height = 0;
    }
}

void srvdisp_getCursorRegion(DisplayConnection *conn,
        DamageArea *cursorRegion)
{
    cursorRegion->x = conn->lastPointerEv.x - conn->cursorImg->xhot;
    cursorRegion->y = conn->lastPointerEv.y - conn->cursorImg->yhot;
    cursorRegion->width = conn->cursorImg->width;
    cursorRegion->height = conn->cursorImg->height;
    if( cursorRegion->y < 0 ) {
        cursorRegion->height += cursorRegion->y;
        cursorRegion->y = 0;
    }
    if( cursorRegion->y + cursorRegion->height > conn->curImg->height )
        cursorRegion->height = conn->curImg->height - cursorRegion->y;
    if( cursorRegion->x < 0 ) {
        cursorRegion->width += cursorRegion->x;
        cursorRegion->x = 0;
    }
    if( cursorRegion->x + cursorRegion->width > conn->curImg->width )
        cursorRegion->width = conn->curImg->width - cursorRegion->x;
}

void srvdisp_sendRectToSocket(DisplayConnection *conn, SockStream *strm,
        const DamageArea *damage)
{
    int bytesPerLine = conn->curImg->bytes_per_line;
    int bytespp = (conn->curImg->bits_per_pixel + 7) / 8;

    sock_writeRect(strm, conn->curImg->data + damage->y * bytesPerLine +
            damage->x * bytespp, bytesPerLine, damage->width * bytespp,
            damage->height);
}

void srvdisp_sendCursorToSocket(DisplayConnection *conn, SockStream *strm)
{
    int bytesPerLine = conn->curImg->bytes_per_line;
    int bytespp = (conn->curImg->bits_per_pixel + 7) / 8;
    int i, j;

    int curX = conn->lastPointerEv.x - conn->cursorImg->xhot;
    int curY = conn->lastPointerEv.y - conn->cursorImg->yhot;
    int curW = conn->cursorImg->width;
    int curH = conn->cursorImg->height;
    const unsigned long *curData = conn->cursorImg->pixels;
    if( curY < 0 ) {
        curData -= curW * curY;
        curH += curY;
        curY = 0;
    }
    if( curY + curH > conn->curImg->height )
        curH = conn->curImg->height - curY;
    if( curX < 0 ) {
        curData -= curX;
        curW += curX;
        curX = 0;
    }
    if( curX + curW > conn->curImg->width )
        curW = conn->curImg->width - curX;
    char *rectSend = malloc(curH * curW * bytespp), *dest = rectSend;
    for(i = 0; i < curH; ++i) {
        const char *imgData = conn->curImg->data + (curY+i) * bytesPerLine
            + curX * bytespp;
        for(j = 0; j < curW; ++j) {
            // TODO: bytespp != 4
            unsigned alpha = (*curData >> 24) & 0xff;
            if( alpha ) {
                unsigned red = (*curData >> 16) & 0xff;
                unsigned green = (*curData >> 8) & 0xff;
                unsigned blue = *curData & 0xff;
                if( alpha == 255 ) {
                    dest[0] = red;
                    dest[1] = green;
                    dest[2] = blue;
                }else{
                    dest[0] = ((unsigned char)imgData[0] * (255-alpha)) / 255
                        + red;
                    dest[1] = ((unsigned char)imgData[1] * (255-alpha)) / 255
                        + green;
                    dest[2] = ((unsigned char)imgData[2] * (255-alpha)) / 255
                        + blue;
                }
            }else{
                dest[0] = imgData[0];
                dest[1] = imgData[1];
                dest[2] = imgData[2];
                dest[3] = imgData[3];
            }
            ++curData;
            imgData += bytespp;
            dest += bytespp;
        }
        curData += conn->cursorImg->width - curW;
    }
    sock_writeRect(strm, rectSend, curW * bytespp, curW * bytespp, curH);
    free(rectSend);
}

void srvdisp_close(DisplayConnection *conn)
{
    if( conn != NULL ) {
        if( conn->shmInfo1.shmaddr != NULL ) {
            XShmDetach(conn->d, &conn->shmInfo1);
            shmdt(conn->shmInfo1.shmaddr);
            XShmDetach(conn->d, &conn->shmInfo2);
            shmdt(conn->shmInfo2.shmaddr);
        }else{
            XDestroyImage(conn->prevImg);
            XDestroyImage(conn->curImg);
        }
        XDamageDestroy(conn->d, conn->damageId);
        XFree(conn->cursorImg);
        XCloseDisplay(conn->d);
    }
    free(conn);
}

