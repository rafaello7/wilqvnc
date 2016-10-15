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
} RectangleArea;

typedef struct {
    VncEventType evType;
} DisplayEvent;

typedef struct {
    RectangleArea rect;
    int srcX, srcY;
} RectangleMotion;


/* Connects to X server and opens a window to display the remote desktop.
 */
DisplayConnection *srvdisp_open(void);


int srvdisp_getWidth(DisplayConnection*);
int srvdisp_getHeight(DisplayConnection*);
void srvdisp_getPixelFormat(DisplayConnection*, PixelFormat*);


/* Waits until next window event appears in event queue or some data is
 * available for read in socket.
 * Window event is stored in DisplayEvent structure.
 * Returns True when some data is available on socket, False otherwise.
 */
int srvdisp_nextEvent(DisplayConnection*, SockStream*, DisplayEvent*, int wait);


/* Emulates key press/relase event using XTEST extension.
 */
void srvdisp_generateKeyEvent(DisplayConnection*, const VncKeyEvent*);


/* Emulates mouse event using XTEST extension.
 */
void srvdisp_generatePointerEvent(DisplayConnection*, const VncPointerEvent*);


/* Moves current image area to previous one and fetches current desktop image.
 * Stores in refreshedArea the area with differences between current
 * and previous image.
 *
 * Note that images may differ outside the refreshedArea, but these differences
 * will be not equated in next call - they will be reported instead as part of
 * refreshedArea.
 */
void srvdisp_refreshDamagedImageRegion(DisplayConnection*,
        RectangleArea *refreshedArea);


int srvdisp_discoverMotion(DisplayConnection *conn, RectangleMotion*);


const char *srvdisp_getPrevImage(const DisplayConnection*);
const char *srvdisp_getCurImage(const DisplayConnection*);

void srvdisp_sendRectToSocket(DisplayConnection*, SockStream*,
        const RectangleArea*);
void srvdisp_sendWILQ(DisplayConnection*, SockStream*, RectangleArea *damage);
void srvdisp_getCursorRegion(DisplayConnection *conn,
        RectangleArea *cursorRegion);
void srvdisp_sendCursorToSocket(DisplayConnection*, SockStream*);


/* Closes the window with remote desktop and disconnect from X server.
 */
void srvdisp_close(DisplayConnection*);


#endif /* SRVDISPLAY_H */
