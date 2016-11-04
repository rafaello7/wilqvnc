#ifndef CLICONN_H
#define CLICONN_H

#include "vnccommon.h"
#include "clidisplay.h"

typedef struct ClientConnection CliConn;

CliConn *cliconn_open(const char *vncHost, const char *passwdFile);

int cliconn_getWidth(const CliConn*);
int cliconn_getHeight(const CliConn*);
const char *cliconn_getName(const CliConn*);

void cliconn_setEncodings(CliConn*, int enableHextile, int enableZRLE);
void cliconn_setPixelFormat(CliConn*, const PixelFormat*);
void cliconn_sendFramebufferUpdateRequest(CliConn*, int incremental);
void cliconn_sendKeyEvent(CliConn*, const VncKeyEvent*);
void cliconn_sendPointerEvent(CliConn*, const VncPointerEvent*);

int cliconn_nextEvent(CliConn*, DisplayConnection*, DisplayEvent*, int wait);
void cliconn_recvFramebufferUpdate(CliConn*, DisplayConnection*);
void cliconn_recvCutTextMsg(CliConn*);

void cliconn_close(CliConn*);

#endif /* CLICONN_H */
