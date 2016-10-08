#ifndef VNCDISPLAY_H
#define VNCDISPLAY_H

#include "pixelformat.h"
#include "sockstream.h"

typedef struct DisplayConnection DisplayConnection;

typedef enum {
    VET_NONE,               // no event
    VET_MOUSE,              // change mouse buttons state, mouse movement
    VET_KEYDOWN,
    VET_KEYUP,
    VET_CLOSE               // close connection
} VncEventType;

typedef struct {
    VncEventType evType;
    int x, y;               // mouse position on mouse event
    unsigned detail;        // button state on mouse event, 
                            // KeySym on key event
} DisplayEvent;


/* Connects to X server and opens a window to display the remote desktop.
 */
DisplayConnection *vncdisp_open(int width, int height, const char *title,
        int argc, char *argv[], int fullScreen);


void vncdisp_getPixelFormat(DisplayConnection*, PixelFormat*);


/* Waits until next window event appears in event queue or some data is
 * available for read in socket.
 * Window event is stored in DisplayEvent structure.
 * Returns True when some data is aveilable on socket, False otherwise.
 */
int vncdisp_nextEvent(DisplayConnection*, SockStream*, DisplayEvent*, int wait);


/* Stores rectangle image on remote desktop display.
 */
void vncdisp_putRectFromSocket(DisplayConnection*, SockStream*, int x, int y,
        int width, int height);


/* Copies rectangle area from one region of remote desktop display to
 * another one
 */
void vncdisp_copyRect(DisplayConnection*, int srcX, int srcY,
        int destX, int destY, int width, int height);


void vncdisp_fillRect(DisplayConnection*, const char *pixel,
        int x, int y, int width, int height);


/* Flush updates made on display
 */
void vncdisp_flush(DisplayConnection*);


/* Closes the window with remote desktop and disconnect from X server.
 */
void vncdisp_close(DisplayConnection*);


#endif /* VNCDISPLAY_H */
