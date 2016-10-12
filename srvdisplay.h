#ifndef SRVDISPLAY_H
#define SRVDISPLAY_H

#include "vnccommon.h"
#include "sockstream.h"

typedef struct DisplayConnection DisplayConnection;

typedef enum {
    VET_NONE,               // no event
    VET_DAMAGE,
    VET_CURSOR              // cursor change
} VncEventType;

typedef struct {
    int x, y, width, height;
} DamageArea;

typedef struct {
    VncEventType evType;
} DisplayEvent;


/* Connects to X server and opens a window to display the remote desktop.
 */
DisplayConnection *srvdisp_open(void);


int srvdisp_getWidth(DisplayConnection*);
int srvdisp_getHeight(DisplayConnection*);
void srvdisp_getPixelFormat(DisplayConnection*, PixelFormat*);


/* Waits until next window event appears in event queue or some data is
 * available for read in socket.
 * Window event is stored in DisplayEvent structure.
 * Returns True when some data is aveilable on socket, False otherwise.
 */
int srvdisp_nextEvent(DisplayConnection*, SockStream*, DisplayEvent*, int wait);


void srvdisp_generateKeyEvent(DisplayConnection*, const VncKeyEvent*);
void srvdisp_generatePointerEvent(DisplayConnection*, const VncPointerEvent*);


void srvdisp_refreshDamagedImageRegion(DisplayConnection*,
        DamageArea *refreshedArea);


void srvdisp_sendRectToSocket(DisplayConnection*, SockStream*,
        const DamageArea*);
void srvdisp_getCursorRegion(DisplayConnection *conn,
        DamageArea *cursorRegion);
void srvdisp_sendCursorToSocket(DisplayConnection*, SockStream*);


/* Closes the window with remote desktop and disconnect from X server.
 */
void srvdisp_close(DisplayConnection*);


#endif /* SRVDISPLAY_H */
