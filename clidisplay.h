#ifndef CLIDISPLAY_H
#define CLIDISPLAY_H

#include "vnccommon.h"
#include "sockstream.h"

typedef struct DisplayConnection DisplayConnection;

typedef enum {
    VET_NONE,               // no event
    VET_MOUSE,              // change mouse buttons state, mouse movement
    VET_KEY,                // keydown, keyup
    VET_CLOSE               // close connection
} VncEventType;

typedef struct {
    VncEventType evType;
    union {
        VncKeyEvent kev;
        VncPointerEvent pev;
    };
} DisplayEvent;


/* Connects to X server and opens a window to display the remote desktop.
 */
DisplayConnection *clidisp_open(int width, int height, const char *title,
        int argc, char *argv[], int fullScreen);


void clidisp_getPixelFormat(DisplayConnection*, PixelFormat*);


/* Waits until next window event appears in event queue or some data is
 * available for read in socket.
 * Window event is stored in DisplayEvent structure.
 * Returns True when some data is aveilable on socket, False otherwise.
 */
int clidisp_nextEvent(DisplayConnection*, SockStream*, DisplayEvent*, int wait);


/* Stores rectangle image on remote desktop display.
 */
void clidisp_putRectFromSocket(DisplayConnection*, SockStream*, int x, int y,
        int width, int height);


/* Copies rectangle area from one region of remote desktop display to
 * another one
 */
void clidisp_copyRect(DisplayConnection*, int srcX, int srcY,
        int destX, int destY, int width, int height);


void clidisp_fillRect(DisplayConnection*, const char *pixel,
        int x, int y, int width, int height);


void clidisp_decodeWILQ(DisplayConnection *conn, SockStream *strm,
        int x, int y, int width, int height);


/* Flush updates made on display
 */
void clidisp_flush(DisplayConnection*);


/* Closes the window with remote desktop and disconnect from X server.
 */
void clidisp_close(DisplayConnection*);


#endif /* CLIDISPLAY_H */
