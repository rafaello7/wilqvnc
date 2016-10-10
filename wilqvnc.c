#include <stdio.h>
#include <sys/time.h>
#include "vncdisplay.h"
#include "vncconn.h"
#include "vnclog.h"
#include "cmdline.h"


static void decodeRRE(DisplayConnection *conn, SockStream *strm,
        int x, int y, int width, int height, int bytespp)
{
    char buf[16];
    int i, cnt = sock_readU32(strm); // number of subrectangles

    sock_read(strm, buf, bytespp);
    vncdisp_fillRect(conn, buf, x, y, width, height);
    for(i = 0; i < cnt; ++i) {
        sock_read(strm, buf, bytespp);
        int sx = sock_readU16(strm);
        int sy = sock_readU16(strm);
        int sw = sock_readU16(strm);
        int sh = sock_readU16(strm);
        vncdisp_fillRect(conn, buf, x + sx, y + sy, sw, sh);
    }
}

static void decodeHextile(DisplayConnection *conn, SockStream *strm,
        int x, int y, int width, int height, int bytespp)
{
    int i, j;
    char bg[16], fg[16], *subfg, subfg_buf[16];

    for(i = 0; i < height; i += 16) {
        int th = height - i > 16 ? 16 : height - i;
        for(j = 0; j < width; j += 16) {
            int tw = width - j > 16 ? 16 : width - j;
            unsigned mask = sock_readU8(strm);
            if( mask & 1 ) {    // raw encoding
                vncdisp_putRectFromSocket(conn, strm, x + j, y + i, tw, th);
            }else{
                if( mask & 2 )  // BackgroundSpecified
                    sock_read(strm, bg, bytespp);
                if( mask & 4 )  // ForegroundSpecified
                    sock_read(strm, fg, bytespp);
                vncdisp_fillRect(conn, bg, x + j, y + i, tw, th);
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
                        vncdisp_fillRect(conn, subfg,
                                x + j + (pos >> 4), y + i + (pos&15),
                                (dim >> 4) + 1, (dim & 15) + 1);
                    }
                }
            }
        }
    }
}

static unsigned long long curTimeMs(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000LL + tv.tv_usec / 1000LL;
}

static void recvFramebufferUpdate(DisplayConnection *conn, SockStream *strm,
        int bytespp)
{
    int srcX, srcY, cnt;

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
            vncdisp_putRectFromSocket(conn, strm, x, y, width, height);
            break;
        case 1: // CopyRect encoding
            srcX = sock_readU16(strm);
            srcY = sock_readU16(strm);
            vncdisp_copyRect(conn, srcX, srcY, x, y, width, height);
            break;
        case 2: // RRE encoding
            decodeRRE(conn, strm, x, y, width, height, bytespp);
            break;
        case 5: // Hextile encoding
            decodeHextile(conn, strm, x, y, width, height, bytespp);
            break;
        default:
            vnclog_fatal("unsupported encoding %d", encType);
            break;
        }
        unsigned long long curTm = curTimeMs();
        if( cnt > 0 && curTm - updBegTm > 500 ) {
            vncdisp_flush(conn);
            updBegTm = curTm;
        }
    }
    vncdisp_flush(conn);
}

int main(int argc, char *argv[])
{
    int msg, toRd, width, height, bytespp, frameCnt = 0;
    unsigned long long lastShowFpTm;
    CmdLineParams params;
    PixelFormat pixelFormat;
    char buf[4096];

    cmdline_parse(argc, argv, &params);
    vnclog_setLevel(params.logLevel);
    SockStream *strm = sock_connectVNCHost(params.host);
    VncVersion vncVer = vncconn_exchangeVersion(strm);
    vncconn_exchangeAuth(strm, params.passwdFile, vncVer);
    sock_writeU8(strm, 0); // shared flag
    sock_flush(strm);
    // read ServerInit
    width = sock_readU16(strm);
    height = sock_readU16(strm);
    vnclog_info("desktop size: %dx%d", width, height);
    // PIXEL_FORMAT
    sock_read(strm, buf, 16);
    // name-length
    toRd = sock_readU32(strm);
    // name-string
    sock_read(strm, buf, toRd);
    buf[toRd] = '\0';
    DisplayConnection *conn = vncdisp_open(width, height, buf,
            argc, argv, params.fullScreen);
    vncdisp_getPixelFormat(conn, &pixelFormat);
    bytespp = (pixelFormat.bitsPerPixel+7) / 8;
    vncconn_setEncodings(strm, params.enableHextile);
    vncconn_setPixelFormat(strm, &pixelFormat);
    vncconn_sendFramebufferUpdateRequest(strm, 0, 0, 0, width, height);
    lastShowFpTm = curTimeMs();
    int isPendingUpdReq = 1;
    while( 1 ) {
        DisplayEvent dispEv;
        int isEvFd = vncdisp_nextEvent(conn, strm, &dispEv, isPendingUpdReq);
        if( ! isPendingUpdReq && ! isEvFd && dispEv.evType == VET_NONE ) {
            vncconn_sendFramebufferUpdateRequest(strm, 1, 0, 0, width, height);
            isPendingUpdReq = 1;
            isEvFd = vncdisp_nextEvent(conn, strm, &dispEv, 1);
        }
        switch( dispEv.evType ) {
        case VET_NONE:
            break;
        case VET_KEYDOWN:
            vncconn_sendKeyEvent(strm, 1, dispEv.detail);
            break;
        case VET_KEYUP:
            vncconn_sendKeyEvent(strm, 0, dispEv.detail);
            break;
        case VET_MOUSE:
            vncconn_sendPointerEvent(strm, dispEv.detail, dispEv.x, dispEv.y);
            break;
        case VET_CLOSE:
            goto end;
        }
        if( isEvFd ) {
            msg = sock_readU8(strm);
            switch( msg ) {
            case 0:     // FramebufferUpdate
                if( params.showFrameRate ) {
                    ++frameCnt;
                    unsigned long long curTm = curTimeMs();
                    if( curTm - lastShowFpTm >= 1000 ) {
                        vnclog_info("%.2f fps",
                                1000.0 * frameCnt / (curTm - lastShowFpTm));
                        lastShowFpTm = curTm;
                        frameCnt = 0;
                    }
                }
                recvFramebufferUpdate(conn, strm, bytespp);
                isPendingUpdReq = 0;
                break;
            case 1:     // SetColorMapEntries
                vnclog_fatal("unexpected SetColorMapEntries message");
                break;
            case 2:     // Bell
                break;
            case 3:     // ServerCutText
                sock_read(strm, buf, 3);   // padding
                toRd = sock_readU32(strm); // text length
                while( toRd >= sizeof(buf) ) {
                    sock_read(strm, buf, sizeof(buf));
                    toRd -= sizeof(buf);
                }
                if( toRd > 0 )
                    sock_read(strm, buf, toRd);
                break;
            default:
                vnclog_fatal("unsupported message %d", msg);
                break;
            }
        }
    }
end:
    vncdisp_close(conn);
    sock_close(strm);
    return 0;
}

