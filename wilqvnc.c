#include <stdio.h>
#include <sys/time.h>
#include <stdlib.h>
#include "clidisplay.h"
#include "cliconn.h"
#include "vnclog.h"
#include "cmdline.h"


static unsigned long long curTimeMs(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000LL + tv.tv_usec / 1000LL;
}

int main(int argc, char *argv[])
{
    int msg, frameCnt = 0;
    unsigned long long lastShowFpTm;
    CmdLineParams params;
    PixelFormat pixelFormat;

    cmdline_parse(argc, argv, &params);
    log_setLevel(params.logLevel);
    CliConn *cliConn = cliconn_open(params.host, params.passwdFile);
    DisplayConnection *dispConn = clidisp_open(cliconn_getWidth(cliConn),
            cliconn_getHeight(cliConn), cliconn_getName(cliConn),
            argc, argv, params.fullScreen);
    clidisp_getPixelFormat(dispConn, &pixelFormat);
    cliconn_setEncodings(cliConn, params.enableHextile, params.enableZRLE);
    cliconn_setPixelFormat(cliConn, &pixelFormat);
    cliconn_sendFramebufferUpdateRequest(cliConn, 0);
    lastShowFpTm = curTimeMs();
    int isPendingUpdReq = 1;
    while( 1 ) {
        DisplayEvent dispEv;
        msg = cliconn_nextEvent(cliConn, dispConn, &dispEv, isPendingUpdReq);
        if( ! isPendingUpdReq && msg == -1 && dispEv.evType == VET_NONE ) {
            cliconn_sendFramebufferUpdateRequest(cliConn, 1);
            isPendingUpdReq = 1;
            msg = cliconn_nextEvent(cliConn, dispConn, &dispEv, 1);
        }
        switch( dispEv.evType ) {
        case VET_NONE:
            break;
        case VET_KEY:
            cliconn_sendKeyEvent(cliConn, &dispEv.kev);
            break;
        case VET_MOUSE:
            cliconn_sendPointerEvent(cliConn, &dispEv.pev);
            break;
        case VET_CLOSE:
            goto end;
        }
        switch( msg ) {
        case -1:    // no message
            break;
        case 0:     // FramebufferUpdate
            if( params.showFrameRate ) {
                ++frameCnt;
                unsigned long long curTm = curTimeMs();
                if( curTm - lastShowFpTm >= 1000 ) {
                    log_info("%.2f fps",
                            1000.0 * frameCnt / (curTm - lastShowFpTm));
                    lastShowFpTm = curTm;
                    frameCnt = 0;
                }
            }
            cliconn_recvFramebufferUpdate(cliConn, dispConn);
            isPendingUpdReq = 0;
            break;
        case 1:     // SetColorMapEntries
            log_fatal("unexpected SetColorMapEntries message");
            break;
        case 2:     // Bell
            break;
        case 3:     // ServerCutText
            cliconn_recvCutTextMsg(cliConn);
            break;
        default:
            log_fatal("unsupported message %d", msg);
            break;
        }
    }
end:
    clidisp_close(dispConn);
    cliconn_close(cliConn);
    return 0;
}

