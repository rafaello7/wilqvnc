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
#include <lz4.h>
#include <zstd.h>
#include "srvdisplay.h"
#include "vnclog.h"
#include "srvcmdline.h"

#include <sys/time.h>
 

enum {
    PTREVHIST_SIZE = 16
};

struct DisplayConnection {
    Display *d;
    XShmSegmentInfo shmInfo;
    char *prevImg;
    XImage *curImg;
    Damage damageId;
    int xdamage_evbase;
    int xfixes_evbase;
    fd_set fds;
    VncPointerEvent ptrEvHist[PTREVHIST_SIZE];
    RectangleArea prevDamage;      // area different between curImg and prevImg
    RectangleArea curDamage;       // currently damaged region, not fetched yet
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
    if( ! XFixesQueryExtension(d, &xfixes_evbase, &xfixes_errbase) )
        log_fatal("XFixes extension is not supported by server");
    DisplayConnection *conn = malloc(sizeof(DisplayConnection));
    conn->d = d;
    int defScreenNum = XDefaultScreen(d);
    Visual *defVis = XDefaultVisual(d, defScreenNum);
    int defDepth = XDefaultDepth(d, defScreenNum);
    memset(&conn->shmInfo, 0, sizeof(conn->shmInfo));
    int width = XDisplayWidth(d, defScreenNum);
    int height = XDisplayHeight(d, defScreenNum);
    if( XShmQueryExtension(d) ) {
        conn->curImg = XShmCreateImage(d, defVis, defDepth, ZPixmap, NULL,
                &conn->shmInfo, width, height);
        conn->shmInfo.shmid = shmget(IPC_PRIVATE,
                conn->curImg->bytes_per_line * conn->curImg->height,
                IPC_CREAT|0777);
        log_debug("shm id: %d", conn->shmInfo.shmid);
        conn->shmInfo.shmaddr =
            conn->curImg->data = shmat (conn->shmInfo.shmid, 0, 0);
        conn->shmInfo.readOnly = False;
        Status st = XShmAttach(d, &conn->shmInfo);
        shmctl(conn->shmInfo.shmid, IPC_RMID, NULL);
        if( st == 0 )
            log_fatal("XShmAttach failed");
    }else{
        log_info("shm extension is not available");
        conn->curImg = XCreateImage(d, defVis, defDepth, ZPixmap, 0, NULL,
                width, height, 32, 0);
        conn->curImg->data = malloc(conn->curImg->bytes_per_line * height);
    }
    int bytespp = (conn->curImg->bits_per_pixel + 7) / 8;
    conn->prevImg = malloc(width * height * bytespp);
    memset(conn->prevImg, 0, width * height * bytespp);
    conn->damageId = XDamageCreate(d, XDefaultRootWindow(d),
                XDamageReportDeltaRectangles);
    XFixesSelectCursorInput(d, XDefaultRootWindow(conn->d),
            XFixesDisplayCursorNotifyMask);
    conn->xdamage_evbase = xdamage_evbase;
    conn->xfixes_evbase = xfixes_evbase;
    FD_ZERO(&conn->fds);
    VncPointerEvent ptrEv;
    ptrEv.x = ptrEv.y = -1;
    ptrEv.buttonMask = 0;
    for(int i = 0; i < PTREVHIST_SIZE; ++i)
        conn->ptrEvHist[i] = ptrEv;
    conn->prevDamage.x = conn->prevDamage.y = 0;
    conn->prevDamage.width = conn->prevDamage.height = 0;
    conn->curDamage.x = conn->curDamage.y = 0;
    conn->curDamage.width = width;
    conn->curDamage.height = height;
    conn->cursorImg = XFixesGetCursorImage(d);
    cmdline_initCtl();
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
    else{
        static int isWarned = 0;
        if( ! isWarned )
            log_warn("unknown keycode for keysym %d\nplease load proper "
                 "keyboard layout using setxkbmap or setmodmap", ev->keysym);
    }
}

void srvdisp_generatePointerEvent(DisplayConnection *conn,
        const VncPointerEvent *ev)
{
    unsigned curMask = ev->buttonMask;
    unsigned maskDiff = conn->ptrEvHist[0].buttonMask ^ curMask;

    if( ev->x != conn->ptrEvHist[0].x || ev->y != conn->ptrEvHist[0].y )
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
    for(int i = PTREVHIST_SIZE-1; i > 0; --i)
        conn->ptrEvHist[i] = conn->ptrEvHist[i-1];
    conn->ptrEvHist[0] = *ev;
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
        int ctlFd = cmdline_getCtlFd();
        FD_SET(dispFd, &conn->fds);
        FD_SET(sockFd, &conn->fds);
        FD_SET(ctlFd, &conn->fds);
        int maxFd = dispFd;
        if( sockFd > maxFd )
            maxFd = sockFd;
        if( ctlFd > maxFd )
            maxFd = ctlFd;
        int selCnt = select(maxFd+1, &conn->fds, NULL, NULL,
                wait ? NULL : &tmout);
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
        if( FD_ISSET(ctlFd, &conn->fds) ) {
            FD_CLR(ctlFd, &conn->fds);
            cmdline_recvCtlMsg();
        }
    }
    return isEvFd;
}

void srvdisp_refreshDamagedImageRegion(DisplayConnection *conn,
        RectangleArea *refreshedArea)
{
    int bytesPerLine = conn->curImg->bytes_per_line;
    int bytespp = (conn->curImg->bits_per_pixel + 7) / 8;
    int dataOff = conn->prevDamage.y * bytesPerLine
        + conn->prevDamage.x * bytespp;
    int dataLen = conn->prevDamage.width * bytespp;
    for(int i = 0; i < conn->prevDamage.height; ++i) {
        memcpy(conn->prevImg + dataOff, conn->curImg->data + dataOff,
                dataLen);
        dataOff += bytesPerLine;
    }
    RectangleArea damage = conn->curDamage;
    if( damage.width != 0 && damage.height != 0 ) {
        XDamageSubtract(conn->d, conn->damageId, None, None);
        Window win = XDefaultRootWindow(conn->d);
        if( conn->shmInfo.shmaddr != NULL ) {
            XShmGetImage(conn->d, win, conn->curImg, 0, 0, -1);
        }else{
            XGetSubImage(conn->d, win, 0, 0, conn->curImg->width,
                    conn->curImg->height, -1, ZPixmap, conn->curImg, 0, 0);
        }
        int dataOff = damage.y * bytesPerLine + damage.x * bytespp;
        int dataLen = damage.width * bytespp;
        while( damage.height > 0 && !memcmp(conn->prevImg + dataOff,
                    conn->curImg->data + dataOff, dataLen) )
        {
            ++damage.y;
            --damage.height;
            dataOff += bytesPerLine;
        }
        dataOff = (damage.y + damage.height - 1) * bytesPerLine
            + damage.x * bytespp;
        while( damage.height > 0 && !memcmp(conn->prevImg + dataOff,
                    conn->curImg->data + dataOff, dataLen) )
        {
            --damage.height;
            dataOff -= bytesPerLine;
        }
        // TODO: handle depth 16 and 8
        if( bytespp == sizeof(int) ) {
            dataOff = damage.y * bytesPerLine + damage.x * sizeof(int);
            while( damage.width > 0 ) {
                int i, off = dataOff, isColEq = 1;
                for(i = 0; i < damage.height && isColEq; ++i) {
                    isColEq = *(int*)(conn->prevImg + off) ==
                        *(int*)(conn->curImg->data + off);
                    off += bytesPerLine;
                }
                if( ! isColEq )
                    break;
                dataOff += sizeof(int);
                ++damage.x;
                --damage.width;
            }
            dataOff = damage.y * bytesPerLine
                + (damage.x + damage.width - 1) * sizeof(int);
            while( damage.width > 0 ) {
                int i, off = dataOff, isColEq = 1;
                for(i = 0; i < damage.height && isColEq; ++i) {
                    isColEq = *(int*)(conn->prevImg + off) ==
                        *(int*)(conn->curImg->data + off);
                    off += bytesPerLine;
                }
                if( ! isColEq )
                    break;
                dataOff -= sizeof(int);
                --damage.width;
            }
        }
    }
    *refreshedArea = conn->prevDamage = damage;
    conn->curDamage.width = conn->curDamage.height = 0;
}

static int isColumnEqual(const unsigned *prev, const unsigned *cur,
        int itemsPerLine, int prevX, int prevY, int height,
        int motionX, int motionY)
{
    int offP = prevY * itemsPerLine + prevX;
    int offC = offP + motionY * itemsPerLine + motionX;
    int i;

    for(i = 0; i < height && prev[offP] == cur[offC]; ++i) {
        offP += itemsPerLine;
        offC += itemsPerLine;
    }
    return i == height;
}

static int isRowEqual(const unsigned *prev, const unsigned *cur,
        int itemsPerLine, int prevX, int prevY, int width,
        int motionX, int motionY)
{
    int offP = prevY * itemsPerLine + prevX;
    int offC = offP + motionY * itemsPerLine + motionX;

    return !memcmp(prev + offP, cur + offC, width * sizeof(unsigned));
}

/* Searches for greatest moved rectangle area within damage.
 * The (mx, my) denote upper left corner on previous image of 16x16 pixel
 * rectangle moved vertically by motion value. The rectangle coordinates
 * are relative to upper left corner of damage area.
 */
static void findGreatestModionArea(const RectangleArea *damage,
        const unsigned *prev,
        const unsigned *cur, int itemsPerLine, int midX, int midY, int motion)
{
    int mx = midX, my = midY, mwidth = 16, mheight = 16;

    while( my > 0 && my + motion > 0 && isRowEqual(prev, cur, itemsPerLine,
                damage->x + midX, damage->y + my - 1, 16, 0, motion))
    {
        --my;
        ++mheight;
    }
    while( my + mheight < damage->height &&
            my + mheight + motion < damage->height &&
            isRowEqual(prev, cur, itemsPerLine,
                damage->x + midX, damage->y + my + mheight, 16, 0, motion))
    {
        ++mheight;
    }
    while( mx > 0 && isColumnEqual(prev, cur, itemsPerLine,
                damage->x + mx - 1, damage->y + my, mheight, 0, motion))
    {
        --mx;
        ++mwidth;
    }
    while( mx + mwidth < damage->width && isColumnEqual(prev, cur, itemsPerLine,
                damage->x + mx + mwidth, damage->y + my, mheight, 0, motion))
    {
        ++mwidth;
    }
    log_info("   ** found movement at %d,%d %dx%d by %d", mx, my,
            mwidth, mheight, motion);
}

static unsigned long long curTimeMs(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000LL + tv.tv_usec / 1000LL;
}

static Bool discoverMotionByMouseMove(const VncPointerEvent *ptrEvHist,
        const RectangleArea *damage, const unsigned *prev,
        const unsigned *cur, int itemsPerLine, RectangleMotion *motion)
{
    XPoint checked[PTREVHIST_SIZE * (PTREVHIST_SIZE+1)/2];
    int checkedCount = 0;
    unsigned long long tmBeg = curTimeMs();

    for(int i = 1; i < PTREVHIST_SIZE; ++i) {
        // assume a window is moved when left mouse button is pressed
        if( ! (ptrEvHist[i].buttonMask & 1) )
            continue;
        for(int j = i-1; j >= 0 && (ptrEvHist[j+1].buttonMask & 1); --j) {
            int motionX = ptrEvHist[j].x - ptrEvHist[i].x;
            int motionY = ptrEvHist[j].y - ptrEvHist[i].y;
            if( 0 && motionX == 0 ) {
                continue;   // leave for vertical motion discovery
            }
            int MM = 64;
            if((motionX >= 0 ? motionX : -motionX) + 2*MM > damage->width ||
                (motionY >= 0 ? motionY : -motionY) + 2*MM > damage->height)
                continue;
            int chk;
            for(chk = 0; chk < checkedCount &&
                (checked[chk].x != motionX || checked[chk].y != motionY);
                ++chk)
            {
            }
            if( chk < checkedCount )
                continue;
            checked[checkedCount].x = motionX;
            checked[checkedCount].y = motionY;
            ++checkedCount;
            int xb, yb, xwidth, yheight;
            if( motionX >= 0 ) {
                xb = damage->x + MM;
                xwidth = damage->width - motionX - 2 * MM;
            }else{
                xb = damage->x - motionX + MM;
                xwidth = damage->width - 2 * MM;
            }
            if( motionY >= 0 ) {
                yb = damage->y + MM;
                yheight = damage->height - motionY - 2 * MM;
            }else{
                yb = damage->y - motionY + MM;
                yheight = damage->height - 2 * MM;
            }
            int row;
            int offP = yb * itemsPerLine + xb;
            int offC = offP + motionY * itemsPerLine + motionX;
            for(row = 0; row < yheight && !memcmp(prev + offP, cur + offC,
                        xwidth * sizeof(int)); ++row)
            {
                offP += itemsPerLine;
                offC += itemsPerLine;
            }
            if( row == yheight ) {
                while( MM > 0 ) {
                    if( !isRowEqual(prev, cur, itemsPerLine, xb, yb - 1, xwidth,
                                motionX, motionY) )
                        break;
                    --yb;
                    ++yheight;
                    if( !isRowEqual(prev, cur, itemsPerLine, xb, yb + yheight,
                                xwidth, motionX, motionY) )
                        break;
                    ++yheight;
                    if( !isColumnEqual(prev, cur, itemsPerLine, xb - 1, yb,
                                yheight, motionX, motionY) )
                        break;
                    --xb;
                    ++xwidth;
                    if( !isColumnEqual(prev, cur, itemsPerLine, xb + xwidth, yb,
                                yheight, motionX, motionY) )
                        break;
                    ++xwidth;
                    --MM;
                }
                motion->rect.x = xb + motionX;
                motion->rect.y = yb + motionY;
                motion->rect.width = xwidth;
                motion->rect.height = yheight;
                motion->srcX = xb;
                motion->srcY = yb;
                unsigned long long tmCur = curTimeMs();
                log_info("   ** motion by mouse %dx%d by %d,%d (checked %d)"
                        " %llu ms",
                        xwidth, yheight, motionX, motionY, checkedCount,
                        tmCur - tmBeg);
                return True;
            }
        }
    }
    return False;
}

static int discoverVerticalMotion(const RectangleArea *damage,
        const unsigned *prev, const unsigned *cur, int itemsPerLine)
{
    return False;
    /* Try to discover vertical motion.
     * calculate hash of 16x16 pixels area in the middle of previous image.
     * Compare with hash of each 16x16-pixel area in the same column
     * in current image. If equal, check as movement candidate
     */
    int midX = damage->width / 2 - 8;
    int midY = damage->height / 2 - 8;
    int dataOffPrev = (damage->y+midY) * itemsPerLine + damage->x + midX;
    int dataOffCur = damage->y * itemsPerLine + damage->x + midX;
    unsigned sumPrev = 0, sumCur = 0;
    unsigned hi = 1 << (8 * sizeof(unsigned) - 1);
    unsigned window[16];
    for(int i = 0; i < 16; ++i) {
        unsigned toAdd = prev[dataOffPrev+1] + 3 * prev[dataOffPrev+5] +
            9 * prev[dataOffPrev+9] + 13 * prev[dataOffPrev+13];
        sumPrev = ((sumPrev << 1) | ((sumPrev&hi)!=0)) ^ toAdd;
        toAdd = cur[dataOffCur+1] + 3 * cur[dataOffCur+5] +
            9 * cur[dataOffCur+9] + 13 * cur[dataOffCur+13];
        window[i] = toAdd;
        sumCur = ((sumCur << 1) | ((sumCur&hi)!=0)) ^ toAdd;
        dataOffPrev += itemsPerLine;
        dataOffCur += itemsPerLine;
    }
    for(int i = 16; i < damage->height; ++i) {
        if( sumCur == sumPrev ) {
            log_info("   cand %d", i - 16 - midY);
            int dataOffP = (damage->y+midY) * itemsPerLine + damage->x + midX;
            int dataOffC = (damage->y+i-16) * itemsPerLine + damage->x + midX;
            int j;
            for(j = 0; j < 16 && ! memcmp(prev+dataOffP,
                        cur+dataOffC, 16 * sizeof(int)); ++j)
            {
                dataOffP += itemsPerLine;
                dataOffC += itemsPerLine;
            }
            if( j == 16 ) {
                findGreatestModionArea(damage, prev, cur, itemsPerLine,
                        midX, midY, i - 16 - midY);
                return True;
            }
        }
        unsigned itemSub = window[i % 16];
        unsigned toAdd = cur[dataOffCur+1] + 3 * cur[dataOffCur+5] +
            9 * cur[dataOffCur+9] + 13 * cur[dataOffCur+13];
        window[i%16] = toAdd;
        sumCur = ((sumCur << 1) | ((sumCur&hi)!=0)) ^ toAdd;
        sumCur ^= itemSub << 16 | itemSub >> (8*sizeof(int)-16);
        dataOffCur += itemsPerLine;
    }
    return False;
}

int srvdisp_discoverMotion(DisplayConnection *conn, RectangleMotion *motion)
{
    RectangleArea *damage = &conn->prevDamage;
    int bytespp = (conn->curImg->bits_per_pixel + 7) / 8;
    int bytesPerLine = conn->curImg->bytes_per_line;

    if( damage->width >= 32 && damage->height >= 32 && bytespp == sizeof(int)
            && bytesPerLine % sizeof(int) == 0)
    {
        static int nn = 0;
        log_info("%3d %dx%d", ++nn, damage->width, damage->height);
        const unsigned *prev = (const unsigned*)conn->prevImg;
        const unsigned *cur = (const unsigned*)conn->curImg->data;
        if( discoverMotionByMouseMove(conn->ptrEvHist, damage, prev, cur,
                bytesPerLine / sizeof(int), motion) )
            return True;
        if( discoverVerticalMotion(damage, prev, cur,
                    bytesPerLine / sizeof(int)) )
            return True;
    }
    return False;
}

static void sendDiff(DisplayConnection *conn, SockStream *strm,
        RectangleArea *damage)
{
    int bytespp = (conn->curImg->bits_per_pixel + 7) / 8;
    int bytesPerLine = conn->curImg->bytes_per_line;

    if( damage->width > 0 && damage->height > 0 && bytespp == sizeof(int)
            && bytesPerLine % sizeof(int) == 0)
    {
        unsigned long long tmBeg = curTimeMs();
        const unsigned *prev = (const unsigned*)conn->prevImg;
        const unsigned *cur = (const unsigned*)conn->curImg->data;
        int itemsPerLine = bytesPerLine / sizeof(int);
        int dataOff = damage->y * itemsPerLine + damage->x;
        unsigned *buf = malloc(damage->width * damage->height * sizeof(int));
        int bufp = 0;

        unsigned curPixel;
        unsigned prevDist = 255;
        for(int i = 0; i < damage->height; ++i) {
            for(int j = 0; j < damage->width; ++j) {
                if( ++prevDist == 256 || cur[dataOff+j] != prev[dataOff+j] ) {
                    if( i || j ) {
                        //log_info("%d %d->(%d,%d)", bufp, prevDist, j, i);
                        buf[bufp++] = (curPixel&0xffffff) | (prevDist-1) << 24;
                    }
                    curPixel = cur[dataOff + j];
                    prevDist = 0;
                }
            }
            dataOff += itemsPerLine;
        }
        buf[bufp++] = (curPixel&0xffffff) | prevDist << 24;
        unsigned long long tmCur = curTimeMs();
        static int nn = 0;
        log_info("%3d %dx%d  %d -> %d, %d%%  %llu ms", ++nn, damage->width,
                damage->height, damage->width * damage->height,
                bufp, 100 * bufp / (damage->width * damage->height),
                tmCur - tmBeg);
        sock_writeU32(strm, bufp);
        sock_write(strm, buf, bufp * sizeof(int));
        free(buf);
    }else{
        sock_writeU32(strm, 0);
        sock_writeRect(strm, conn->curImg->data + damage->y * bytesPerLine +
                damage->x * bytespp, bytesPerLine, damage->width * bytespp,
                damage->height);
    }
}

static void sendLZ4(DisplayConnection *conn, SockStream *strm,
        RectangleArea *damage)
{
    int bytespp = (conn->curImg->bits_per_pixel + 7) / 8;
    int bytesPerLine = conn->curImg->bytes_per_line;

    unsigned long long tmBeg = curTimeMs();
    int dataOff = damage->y * bytesPerLine + damage->x * bytespp;
    int lineBytes = damage->width * bytespp;
    int imgBytes = lineBytes * damage->height;
    char *bufsrc = malloc(imgBytes);
    int srcOff = 0;

    for(int i = 0; i < damage->height; ++i) {
        memcpy(bufsrc + srcOff, conn->curImg->data + dataOff, lineBytes);
        dataOff += bytesPerLine;
        srcOff += lineBytes;
    }
    int destBytes = LZ4_compressBound(imgBytes);
    char *bufdest = malloc(destBytes);
    int comp = LZ4_compress_fast(bufsrc, bufdest, imgBytes, destBytes,
            cmdline_getParams()->lz4Level);
    unsigned long long tmCur = curTimeMs();
    static int nn = 0;
    log_info("%3d %dx%d  %d -> %d/%d, %d%%  %llu ms", ++nn, damage->width,
            damage->height, imgBytes,
            comp, destBytes, 100 * comp / imgBytes, tmCur - tmBeg);
    sock_writeU32(strm, comp);
    sock_write(strm, bufdest, comp);
    free(bufsrc);
    free(bufdest);
}

static void sendZStd(DisplayConnection *conn, SockStream *strm,
        RectangleArea *damage)
{
    int bytespp = (conn->curImg->bits_per_pixel + 7) / 8;
    int bytesPerLine = conn->curImg->bytes_per_line;

    unsigned long long tmBeg = curTimeMs();
    int dataOff = damage->y * bytesPerLine + damage->x * bytespp;
    int lineBytes = damage->width * bytespp;
    int imgBytes = lineBytes * damage->height;
    char *bufsrc = malloc(imgBytes);
    int srcOff = 0;

    for(int i = 0; i < damage->height; ++i) {
        memcpy(bufsrc + srcOff, conn->curImg->data + dataOff, lineBytes);
        dataOff += bytesPerLine;
        srcOff += lineBytes;
    }
    int destBytes = ZSTD_compressBound(imgBytes);
    char *bufdest = malloc(destBytes);
    int comp = ZSTD_compress(bufdest, destBytes, bufsrc, imgBytes, 
            cmdline_getParams()->zstdLevel);
    unsigned long long tmCur = curTimeMs();
    static int nn = 0;
    log_info("%3d %dx%d  %d -> %d/%d, %d%%  %llu ms", ++nn, damage->width,
            damage->height, imgBytes,
            comp, destBytes, 100 * comp / imgBytes, tmCur - tmBeg);
    sock_writeU32(strm, comp);
    sock_write(strm, bufdest, comp);
    free(bufsrc);
    free(bufdest);
}

static void sendRaw(DisplayConnection *conn, SockStream *strm,
        RectangleArea *damage)
{
    int bytespp = (conn->curImg->bits_per_pixel + 7) / 8;
    int bytesPerLine = conn->curImg->bytes_per_line;

    unsigned long long tmBeg = curTimeMs();
    int dataOff = damage->y * bytesPerLine + damage->x * bytespp;
    int lineBytes = damage->width * bytespp;
    int imgBytes = lineBytes * damage->height;
    char *bufsrc = malloc(imgBytes);
    int srcOff = 0;

    for(int i = 0; i < damage->height; ++i) {
        memcpy(bufsrc + srcOff, conn->curImg->data + dataOff, lineBytes);
        dataOff += bytesPerLine;
        srcOff += lineBytes;
    }
    int destBytes = imgBytes;
    char *bufdest = malloc(destBytes);
    memcpy(bufdest, bufsrc, imgBytes);
    int comp = imgBytes;
    unsigned long long tmCur = curTimeMs();
    static int nn = 0;
    log_info("%3d %dx%d  %d -> %d/%d, %d%%  %llu ms", ++nn, damage->width,
            damage->height, imgBytes,
            comp, destBytes, 100 * comp / imgBytes, tmCur - tmBeg);
    sock_writeU32(strm, comp);
    sock_write(strm, bufdest, comp);
    free(bufsrc);
    free(bufdest);
}

void srvdisp_sendWILQ(DisplayConnection *conn, SockStream *strm,
        RectangleArea *damage)
{
    int method = 2;
    const CmdLineParams *params = cmdline_getParams();

    if( params->useDiff )
        method = 0;
    else{
        switch( params->compr ) {
        case COMPR_ZSTD:
            method = 2;
            break;
        case COMPR_LZ4:
            method = 1;
            break;
        default:
            method = 3;
            break;
        }
    }
    sock_writeU32(strm, 0x514c4957);
    sock_writeU32(strm, method);
    switch( method ) {
    case 0:
        sendDiff(conn, strm, damage);
        break;
    case 1:
        sendLZ4(conn, strm, damage);
        break;
    case 2:
        sendZStd(conn, strm, damage);
        break;
    case 3:
        sendRaw(conn, strm, damage);
        break;
    }
}

const char *srvdisp_getPrevImage(const DisplayConnection *conn)
{
    return conn->prevImg;
}

const char *srvdisp_getCurImage(const DisplayConnection *conn)
{
    return conn->curImg->data;
}

void srvdisp_getCursorRegion(DisplayConnection *conn,
        RectangleArea *cursorRegion)
{
    cursorRegion->x = conn->ptrEvHist[0].x - conn->cursorImg->xhot;
    cursorRegion->y = conn->ptrEvHist[0].y - conn->cursorImg->yhot;
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
    if( cursorRegion->width <= 0 || cursorRegion->height <= 0 )
        cursorRegion->width = cursorRegion->height = 0;
}

void srvdisp_sendRectToSocket(DisplayConnection *conn, SockStream *strm,
        const RectangleArea *damage)
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

    int curX = conn->ptrEvHist[0].x - conn->cursorImg->xhot;
    int curY = conn->ptrEvHist[0].y - conn->cursorImg->yhot;
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
        free(conn->prevImg);
        if( conn->shmInfo.shmaddr != NULL ) {
            XShmDetach(conn->d, &conn->shmInfo);
            shmdt(conn->shmInfo.shmaddr);
        }
        XDestroyImage(conn->curImg);
        XDamageDestroy(conn->d, conn->damageId);
        XFree(conn->cursorImg);
        XCloseDisplay(conn->d);
    }
    free(conn);
}

