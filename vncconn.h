#ifndef VNCCONN_H
#define VNCCONN_H

#include "pixelformat.h"
#include "sockstream.h"

typedef enum {
    VNCVER_3_3,
    VNCVER_3_7,
    VNCVER_3_8
} VncVersion;

VncVersion vncconn_exchangeVersion(SockStream *strm);
void vncconn_exchangeAuth(SockStream *strm, const char *passwdFile,
        VncVersion vncVer);
void vncconn_setEncodings(SockStream *strm, int enableHextile);
void vncconn_setPixelFormat(SockStream *strm, const PixelFormat*);
void vncconn_sendFramebufferUpdateRequest(SockStream *strm, int incremental,
        int x, int y, int width, int height);
void vncconn_sendKeyEvent(SockStream *strm, int isDown, unsigned keysym);
void vncconn_sendPointerEvent(SockStream *strm, unsigned state, int x, int y);


#endif /* VNCCONN_H */
