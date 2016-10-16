#ifndef SRVCONN_H
#define SRVCONN_H

#include "vnccommon.h"
#include "sockstream.h"

VncVersion srvconn_exchangeVersion(SockStream*);
void srvconn_sendServerInit(SockStream*, int width, int height, PixelFormat*,
        const char *name);
void srvconn_exchangeAuth(SockStream*, VncVersion vncVer,
        const char *passwdFile);
void srvconn_getEncodings(SockStream*);
void srvconn_getPixelFormat(SockStream*, PixelFormat*);
void srvconn_recvFramebufferUpdateRequest(SockStream*,
        FramebufferUpdateRequest*);
void srvconn_recvKeyEvent(SockStream*, VncKeyEvent*);
void srvconn_recvPointerEvent(SockStream*, VncPointerEvent*);
void srvconn_recvCutText(SockStream*);


/* Sends rectangle area of image to socket using encoding method according
 * to command line settings.
 */
void srvconn_sendRectEncoded(SockStream*, const char *prevImg,
        const char *curImg, int bytesPerPixel, int bytesPerLine,
        const RectangleArea*);


#endif /* SRVCONN_H */
