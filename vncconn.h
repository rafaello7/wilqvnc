#ifndef VNCCONN_H
#define VNCCONN_H

#include "pixelformat.h"
#include "sockstream.h"

void vncconn_exchangeVersion(SockStream *strm);
void vncconn_exchangeAuth(SockStream *strm, const char *passwdFile);
void vncconn_setEncodings(SockStream *strm, int enableHextile);
void vncconn_setPixelFormat(SockStream *strm, const PixelFormat*);
void vncconn_sendFramebufferUpdateRequest(SockStream *strm, int incremental,
        int x, int y, int width, int height);
void vncconn_sendKeyEvent(SockStream *strm, int isDown, unsigned keysym);
void vncconn_sendPointerEvent(SockStream *strm, unsigned state, int x, int y);


#endif /* VNCCONN_H */
