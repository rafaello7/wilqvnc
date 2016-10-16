#ifndef VNCCOMMON_H
#define VNCCOMMON_H

typedef enum {
    VNCVER_3_3,
    VNCVER_3_7,
    VNCVER_3_8
} VncVersion;

typedef struct {
    unsigned bitsPerPixel;
    unsigned depth;
    unsigned bigEndian;
    unsigned trueColor;
    unsigned maxRed, maxGreen, maxBlue;
    unsigned shiftRed, shiftGreen, shiftBlue;
} PixelFormat;

typedef struct {
    int incremental;
    int x, y;
    int width, height;
} FramebufferUpdateRequest;

typedef struct {
    int isDown;
    unsigned keysym;
} VncKeyEvent;

typedef struct {
    unsigned buttonMask;
    int x, y;
} VncPointerEvent;

typedef struct {
    int x, y, width, height;
} RectangleArea;


#endif /* VNCCOMMON_H */
