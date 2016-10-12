#include "sockstream.h"
#include "vnclog.h"
#include "srvvncconn.h"
#include "srvdisplay.h"
#include <stdio.h>
#include <string.h>

static int isRectCoveredBy(const DamageArea *covered,
        const DamageArea *covering)
{
    return covered->x >= covering->x && covered->y >= covering->y &&
         covered->x + covered->width <= covering->x + covering->width &&
         covered->y + covered->height <= covering->y + covering->height;
}

static int isPtOnArea(int x, int y, const DamageArea *area)
{
    return x >= area->x && x < area->x + area->width &&
        y >= area->y && y < area->y + area->height;
}

static int isRectIntersectionEmpty(const DamageArea *area1,
        const DamageArea *area2)
{
    return area1->width == 0 || area1->height == 0 ||
        (!isPtOnArea(area1->x, area1->y, area2) &&
        !isPtOnArea(area1->x + area1->width - 1, area1->y, area2) &&
        !isPtOnArea(area1->x, area1->y + area1->height - 1, area2) &&
        !isPtOnArea(area1->x + area1->width - 1, area1->y + area1->height - 1,
                area2));
}

static int writeUpdate(DisplayConnection *conn, SockStream *strm,
        int isCursorChanged, DamageArea *oldCursorArea)
{
    DamageArea damage, cursorArea;
    int putOldCursor, putCursor, putDamage, sendUpdate;

    srvdisp_refreshDamagedImageRegion(conn, &damage);
    srvdisp_getCursorRegion(conn, &cursorArea);
    if( cursorArea.x != oldCursorArea->x || cursorArea.y != oldCursorArea->y )
        isCursorChanged = 1;
    putCursor = cursorArea.width != 0 && cursorArea.height != 0 &&
        (isCursorChanged || ! isRectIntersectionEmpty(&damage, &cursorArea));
    putDamage = damage.width != 0 && damage.height != 0 &&
        (! putCursor || ! isRectCoveredBy(&damage, &cursorArea));
    putOldCursor = oldCursorArea->width != 0 && oldCursorArea->height != 0 &&
        isCursorChanged &&
        ( !putDamage || !isRectCoveredBy(oldCursorArea, &damage) ) &&
        ( !putCursor || ! isRectCoveredBy(oldCursorArea, &cursorArea) );
    sendUpdate = putDamage || putOldCursor || putCursor;
    if( sendUpdate ) {
        sock_writeU8(strm, 0);      // message type
        sock_writeU8(strm, 0);      // padding
        // number of rectangles
        sock_writeU16(strm, putDamage + putOldCursor + putCursor);

        if( putDamage ) {
            sock_writeU16(strm, damage.x);
            sock_writeU16(strm, damage.y);
            sock_writeU16(strm, damage.width);
            sock_writeU16(strm, damage.height);
            sock_writeU32(strm, 0);     // encoding type
            srvdisp_sendRectToSocket(conn, strm, &damage);
        }

        if( putOldCursor ) {
            sock_writeU16(strm, oldCursorArea->x);
            sock_writeU16(strm, oldCursorArea->y);
            sock_writeU16(strm, oldCursorArea->width);
            sock_writeU16(strm, oldCursorArea->height);
            sock_writeU32(strm, 0);     // encoding type
            srvdisp_sendRectToSocket(conn, strm, oldCursorArea);
        }

        if( putCursor ) {
            sock_writeU16(strm, cursorArea.x);
            sock_writeU16(strm, cursorArea.y);
            sock_writeU16(strm, cursorArea.width);
            sock_writeU16(strm, cursorArea.height);
            sock_writeU32(strm, 0);     // encoding type
            srvdisp_sendCursorToSocket(conn, strm);
        }
        sock_flush(strm);
    }
    *oldCursorArea = cursorArea;
    return sendUpdate;
}

int main(int argc, char *argv[])
{
    int width, height;
    SockStream *strm;
    VncKeyEvent kev;
    VncPointerEvent pev;
    FramebufferUpdateRequest req;
    PixelFormat pixelFormat;
    DamageArea cursorRegion;

    strm = sock_accept();
    DisplayConnection *conn = srvdisp_open();
    VncVersion vncVer = srvconn_exchangeVersion(strm);
    srvconn_exchangeAuth(strm, vncVer, NULL);
    int shared = sock_readU8(strm); // shared-flag
    log_info("shared: %d", shared);
    width = srvdisp_getWidth(conn);
    height = srvdisp_getHeight(conn);
    srvdisp_getPixelFormat(conn, &pixelFormat);
    srvconn_sendServerInit(strm, width, height, &pixelFormat, "ziu");
    int isPendingUpdReq = 0, isDamage = 1, isCursorChanged = 1;
    cursorRegion.x = cursorRegion.y = 0;
    cursorRegion.width = cursorRegion.height = 0;
    while( 1 ) {
        DisplayEvent dispEv;
        if( isPendingUpdReq && isDamage ) {
            if( writeUpdate(conn, strm, isCursorChanged, &cursorRegion) )
                isPendingUpdReq = 0;
            isDamage = isCursorChanged = 0;
        }
        int isEvFd = srvdisp_nextEvent(conn, strm, &dispEv, 1);
        switch( dispEv.evType ) {
        case VET_NONE:
            break;
        case VET_DAMAGE:
            isDamage = 1;
            break;
        case VET_CURSOR:
            isDamage = 1;
            isCursorChanged = 1;
            break;
        }
        if( isEvFd ) {
            int msg = sock_readU8(strm);
            switch( msg ) {
            case 0:     // SetPixelFormat
                srvconn_getPixelFormat(strm, &pixelFormat);
                break;
            case 2:     // SetEncodings
                srvconn_getEncodings(strm);
                break;
            case 3:     // FramebufferUpdateRequest
                //log_info("msg: FramebufferUpdateRequest");
                srvconn_recvFramebufferUpdateRequest(strm, &req);
                isPendingUpdReq = 1;
                break;
            case 4:     // KeyEvent
                srvconn_recvKeyEvent(strm, &kev);
                srvdisp_generateKeyEvent(conn, &kev);
                break;
            case 5:     // PointerEvent
                srvconn_recvPointerEvent(strm, &pev);
                srvdisp_generatePointerEvent(conn, &pev);
                isDamage = 1;
                break;
            case 6:     // ClientCutText
                srvconn_recvCutText(strm);
                break;
            default:
                log_fatal("unknown message from client: %d", msg);
            }
        }
    }
    return 0;
}

