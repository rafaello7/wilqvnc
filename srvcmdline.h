#ifndef SRVCMDLINE_H
#define SRVCMDLINE_H


typedef enum {
    COMPR_NONE,
    COMPR_ZSTD,
    COMPR_LZ4
} CompressionType;

typedef struct {
    int zstdLevel;
    int lz4Level;
    CompressionType compr;
    const char *passwdFile;
    int logLevel;
    int discoverMouseMovement;
    int discoverVerticalMovement;
    int useDiff;
    int vncDisplayNumber;
    int runOnce;
} CmdLineParams;


/* Returns NULL on success, error message on error
 */
const char *cmdline_parse(int argc, char *argv[]);

const CmdLineParams *cmdline_getParams(void);

void cmdline_initCtl(void);
int cmdline_getCtlFd(void);
void cmdline_recvCtlMsg(void);

void cmdline_sendCtlMsg(int argc, char *argv[]);

#endif /* SRVCMDLINE_H */
