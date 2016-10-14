#include "sockstream.h"
#include "vnclog.h"
#include "srvvncconn.h"
#include "srvdisplay.h"
#include <stdio.h>
#include <string.h>

enum {
    MHIST_SIZE = 16
};

static int isRectCoveredBy(const RectangleArea *covered,
        const RectangleArea *covering)
{
    return covered->x >= covering->x && covered->y >= covering->y &&
         covered->x + covered->width <= covering->x + covering->width &&
         covered->y + covered->height <= covering->y + covering->height;
}

static int isRectIntersectionEmpty(const RectangleArea *area1,
        const RectangleArea *area2)
{
    return area1->width <= 0 || area1->height <= 0 ||
        area2->width <= 0 || area2->height <= 0 ||
        area1->x >= area2->x + area2->width ||
        area1->y >= area2->y + area2->height ||
        area1->x + area1->width <= area2->x ||
        area1->y + area1->height <= area2->y;
}

static int writeUpdate(DisplayConnection *conn, SockStream *strm,
        int isCursorChanged, RectangleArea *oldCursorArea)
{
    RectangleArea damage, cursorArea, damageSplit[5];
    int putOldCursor, putCursor, putDamage, sendUpdate, splitCnt = 0;
    int sendCopyRect;
    RectangleMotion motion;

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
    if( putDamage ) {
        splitCnt = 1;
        if( (sendCopyRect = srvdisp_discoverMotion(conn, &motion)) != 0 ) {
            damageSplit[0] = motion.rect;
            // above motion
            if( motion.rect.y > damage.y ) {
                damageSplit[splitCnt].x = damage.x;
                damageSplit[splitCnt].y = damage.y;
                damageSplit[splitCnt].width = damage.width;
                damageSplit[splitCnt].height = motion.rect.y - damage.y;
                ++splitCnt;
            }
            // below motion
            if(motion.rect.y + motion.rect.height < damage.y + damage.height) {
                damageSplit[splitCnt].x = damage.x;
                damageSplit[splitCnt].y = motion.rect.y + motion.rect.height;
                damageSplit[splitCnt].width = damage.width;
                damageSplit[splitCnt].height = damage.y + damage.height
                    - (motion.rect.y + motion.rect.height);
                ++splitCnt;
            }
            // to the left of motion
            if( motion.rect.x > damage.x ) {
                damageSplit[splitCnt].x = damage.x;
                damageSplit[splitCnt].y = motion.rect.y;
                damageSplit[splitCnt].width = motion.rect.x - damage.x;
                damageSplit[splitCnt].height = motion.rect.height;
                ++splitCnt;
            }
            // to the right of motion
            if( motion.rect.x + motion.rect.width < damage.x + damage.width ) {
                damageSplit[splitCnt].x = motion.rect.x + motion.rect.width;
                damageSplit[splitCnt].y = motion.rect.y;
                damageSplit[splitCnt].width = damage.x + damage.width
                    - (motion.rect.x + motion.rect.width);
                damageSplit[splitCnt].height = motion.rect.height;
                ++splitCnt;
            }
        }else{
            damageSplit[0] = damage;
        }
    }
    sendUpdate = putDamage || putOldCursor || putCursor;
    if( sendUpdate ) {
        sock_writeU8(strm, 0);      // message type
        sock_writeU8(strm, 0);      // padding
        // number of rectangles
        sock_writeU16(strm, splitCnt + putOldCursor + putCursor);

        if( putDamage ) {
            for(int i = 0; i < splitCnt; ++i) {
                sock_writeU16(strm, damageSplit[i].x);
                sock_writeU16(strm, damageSplit[i].y);
                sock_writeU16(strm, damageSplit[i].width);
                sock_writeU16(strm, damageSplit[i].height);
                if( i == 0 && sendCopyRect ) {
                    sock_writeU32(strm, 1);     // encoding type
                    sock_writeU16(strm, motion.srcX);
                    sock_writeU16(strm, motion.srcY);
                }else{
                    sock_writeU32(strm, 0);     // encoding type
                    srvdisp_sendRectToSocket(conn, strm, damageSplit + i);
                }
            }
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
    int i, width, height;
    SockStream *strm;
    VncKeyEvent kev;
    VncPointerEvent pev;
    FramebufferUpdateRequest req;
    PixelFormat pixelFormat;
    RectangleArea cursorRegion;
    VncPointerEvent pointerEvHist[MHIST_SIZE];

    log_setLevel(2);
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
    pev.x = pev.y = -1;
    pev.buttonMask = 0;
    for(i = 0; i < MHIST_SIZE; ++i)
        pointerEvHist[i] = pev;
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
                for(i = MHIST_SIZE-1; i > 0; --i)
                    pointerEvHist[i] = pointerEvHist[i-1];
                pointerEvHist[0] = pev;
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

