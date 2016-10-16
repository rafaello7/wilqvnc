#ifndef SOCKSTREAM_H
#define SOCKSTREAM_H

typedef struct SockStream SockStream;

SockStream *sock_connectVNCHost(const char *hostVNC);

SockStream *sock_accept(int vncDisplay);


void sock_read(SockStream*, void *buf, int toRead);
void sock_write(SockStream*, const void *buf, int toWrite);


unsigned sock_readU8(SockStream*);
unsigned sock_readU16(SockStream*);
unsigned sock_readU32(SockStream*);


/* Read data into "two-dimensional" buffer, i.e. into "height" buffer areas,
 * "width" long each and beginning of each area is bytesPerLine from
 * previous one. First area starts at the buffer beginning.
 */
void sock_readRect(SockStream*, char *buf, int bytesPerLine,
        int width, int height);


/* Read data from input and ignore it.
 */
void sock_discard(SockStream*, unsigned bytes);


void sock_writeU8(SockStream*, unsigned);
void sock_writeU16(SockStream*, unsigned);
void sock_writeU32(SockStream*, unsigned);


void sock_writeRect(SockStream *strm, const char *buf, int bytesPerLine,
        int width, int height);


void sock_flush(SockStream*);


/* Returns non-zero when there is some data available for read without
 * reading underlying socket descriptor
 */
int sock_isDataAvail(SockStream*);


/* Returns the socket file descriptor
 */
int sock_fd(SockStream*);

void sock_close(SockStream*);

#endif /* SOCKSTREAM_H */
