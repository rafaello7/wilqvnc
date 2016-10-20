#ifndef VNCCOMMON_H
#define VNCCOMMON_H

typedef enum {
    VNCVER_3_3,
    VNCVER_3_7,
    VNCVER_3_8
} VncVersion;

typedef enum {
    COMPR_NONE,
    COMPR_LZ4,
    COMPR_ZSTD
} CompressionType;

typedef enum {
    ENC_NONE,
    ENC_DIFF,
    ENC_TRLE,
    ENC_TILA
} EncodingType;

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
