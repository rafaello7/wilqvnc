#ifndef PIXELFORMAT_H
#define PIXELFORMAT_H


typedef struct {
    unsigned bitsPerPixel;
    unsigned depth;
    unsigned bigEndian;
    unsigned trueColor;
    unsigned maxRed, maxGreen, maxBlue;
    unsigned shiftRed, shiftGreen, shiftBlue;
} PixelFormat;


#endif /* PIXELFORMAT_H */
