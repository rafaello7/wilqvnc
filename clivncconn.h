#ifndef CLIVNCCONN_H
#define CLIVNCCONN_H

#include "vnccommon.h"
#include "sockstream.h"

VncVersion cliconn_exchangeVersion(SockStream*);
void cliconn_exchangeAuth(SockStream*, const char *passwdFile,
        VncVersion vncVer);
void cliconn_readPixelFormat(SockStream*, PixelFormat*);
void cliconn_setEncodings(SockStream*, int enableHextile);
void cliconn_setPixelFormat(SockStream*, const PixelFormat*);
void cliconn_sendFramebufferUpdateRequest(SockStream*, int incremental,
        int x, int y, int width, int height);
void cliconn_sendKeyEvent(SockStream*, const VncKeyEvent*);
void cliconn_sendPointerEvent(SockStream*, const VncPointerEvent*);


#endif /* CLIVNCCONN_H */
