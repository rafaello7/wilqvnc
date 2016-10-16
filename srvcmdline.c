#include "srvcmdline.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <unistd.h>
#include "vnclog.h"


static CmdLineParams gParams = {
    .zstdLevel = 3,
    .lz4Level  = 1,
    .compr     = COMPR_NONE
};


static const char *usage(const char *progname, char *buf, int buflen)
{
    const char *p = strrchr(progname, '/');

    if( p != NULL )
        progname = p + 1;
    snprintf(buf, buflen,
        "usage: %s [parameters] [:display]\n"
        "\n"
        "parameters:\n"
        "  -zs|-zstd           <level> - set compression level on zstd\n"
        "  -lz|-lz4            <level> - set compression level on lz4\n"
        "  -c |-compr    <compression> - compression to use\n"
        "                                one of: zstd, lz4, none\n"
        "  -p |-passwd         <fname> - password file for authentication\n"
        "  -v |-verbose                - print some debug info\n"
        "  -q |-quiet                  - print less\n"
        "  -h |-help                   - print this help\n"
        "  -[no]mm                     - CopyRect discovery by mouse move\n"
        "  -[no]vm                     - CopyRect vertical move discovery\n"
        "  -[no]diff                   - pixel-by-pixel difference\n"
        "  -once                       - run once (no fork)\n",
        progname);
    return buf;
}

const char *cmdline_parse(int argc, char *argv[])
{
    static char resultBuf[4096];
    int i = 1;

    while( i < argc ) {
        if( !strcmp(argv[i], "-zs") || !strcmp(argv[i], "-zstd") ) {
            if( i == argc - 1 )
                goto noparam_err;
            gParams.zstdLevel = atoi(argv[++i]);
        }else if( !strcmp(argv[i], "-lz") || !strcmp(argv[i], "-lz4") ) {
            if( i == argc - 1 )
                goto noparam_err;
            gParams.lz4Level = atoi(argv[++i]);
        }else if( !strcmp(argv[i], "-c") || !strcmp(argv[i], "-compr") ) {
            if( i == argc - 1 )
                goto noparam_err;
            switch( argv[++i][0] ) {
            case 'z':
                gParams.compr = COMPR_ZSTD;
                break;
            case 'l':
                gParams.compr = COMPR_LZ4;
                break;
            case 'n':
                gParams.compr = COMPR_NONE;
                break;
            default:
                snprintf(resultBuf, sizeof(resultBuf),
                        "unrecognized compression type %s", argv[i]);
                return resultBuf;
            }
        }else if( !strcmp(argv[i], "-p") || !strcmp(argv[i], "-passwd") )
            gParams.passwdFile = argv[++i];
        else if( !strcmp(argv[i], "-v") || !strcmp(argv[i], "-verbose") )
            ++gParams.logLevel;
        else if( !strcmp(argv[i], "-q") || !strcmp(argv[i], "-quiet") )
            --gParams.logLevel;
        else if( !strcmp(argv[i], "-h") ||  !strcmp(argv[i], "-help") )
            return usage(argv[0], resultBuf, sizeof(resultBuf));
        else if( !strcmp(argv[i], "-mm") )
            gParams.discoverMouseMovement = 1;
        else if( !strcmp(argv[i], "-nomm") )
            gParams.discoverMouseMovement = 0;
        else if( !strcmp(argv[i], "-vm") )
            gParams.discoverVerticalMovement = 1;
        else if( !strcmp(argv[i], "-novm") )
            gParams.discoverVerticalMovement = 0;
        else if( !strcmp(argv[i], "-diff") )
            gParams.useDiff = 1;
        else if( !strcmp(argv[i], "-nodiff") )
            gParams.useDiff = 0;
        else if( !strcmp(argv[i], "-once") )
            gParams.runOnce = 1;
        else if( argv[i][0] == '-' ) {
            snprintf(resultBuf, sizeof(resultBuf),
                    "unrecognized option -- %s", argv[i]);
            return resultBuf;
        }else
            gParams.vncDisplayNumber = atoi(argv[i]);
        ++i;
    }
    return NULL;
noparam_err:
    snprintf(resultBuf, sizeof(resultBuf), "option %s needs a parameter",
            argv[i]);
    return resultBuf;
}

const CmdLineParams *cmdline_getParams(void)
{
    return &gParams;
}

static int gCtlFd = -1;


static void getCtlSockName(char *fnameBuf, int createDir)
{
    const char *runtimeDir;

    if( (runtimeDir = getenv("XDG_RUNTIME_DIR")) != NULL ) {
        sprintf(fnameBuf, "%s/wilqvncsrv", runtimeDir);
    }else{
        if( (runtimeDir = getenv("TMPDIR")) == NULL )
            runtimeDir = "/tmp";
        sprintf(fnameBuf, "%s/wilqvncsrv-%d", runtimeDir, getpid());
    }
    if( createDir )
        mkdir(fnameBuf, 0755);
    strcat(fnameBuf, "/ctl");
}

void cmdline_initCtl(void)
{
    struct sockaddr_un sun;

    getCtlSockName(sun.sun_path, 1);
    unlink(sun.sun_path);
    gCtlFd = socket(AF_UNIX, SOCK_DGRAM, 0);
    sun.sun_family = AF_UNIX;
    if( bind(gCtlFd, (struct sockaddr*)&sun, sizeof(sun)) != 0 )
        log_fatal_errno("unable to bind to control socket");
}

int cmdline_getCtlFd(void)
{
    return gCtlFd;
}

void cmdline_recvCtlMsg(void)
{
    static const char boolStr[][4] = { "No", "Yes" };
    char buf[4097], *argv[20];
    const char *err = NULL;
    int argc = 0, off = 0, len;
    struct sockaddr_un sun;
    socklen_t addrlen = sizeof(sun);


    len = recvfrom(gCtlFd, buf, sizeof(buf) - 1, 0,
            (struct sockaddr*)&sun, &addrlen);
    if( len < 0 ) {
        log_error_errno("control socket receive fail");
        return;
    }
    buf[len] = '\0';
    while( argc < 20 && off < len ) {
        argv[argc++] = buf + off;
        off += strlen(buf + off) + 1;
    }
    if( argc == 20 )
        err = "error: too many arguments";
    else if( argc > 1 ) {
        argv[argc] = NULL;
        err = cmdline_parse(argc, argv);
    }
    if( err == NULL ) {
        sprintf(buf,
                "    zstd compression level (-zs):         %d\n"
                "    lz4  compression level (-lz):         %d\n"
                "    compression used (-c zstd/lz4/none):  %s\n"
                "    discover movement by mouse (-[no]mm): %s\n"
                "    discover vertical movement (-[no]vm): %s\n"
                "    use diff (-[no]diff):                 %s\n"
                "    log level (-v/-q):                    %d\n",
                gParams.zstdLevel, gParams.lz4Level,
                gParams.compr == COMPR_ZSTD ? "zstd" :
                gParams.compr == COMPR_LZ4  ? "lz4" :
                gParams.compr == COMPR_NONE ? "none" : "unknown",
                boolStr[gParams.discoverMouseMovement],
                boolStr[gParams.discoverVerticalMovement],
                boolStr[gParams.useDiff],
                gParams.logLevel);
        err = buf;
    }
    if( sendto(gCtlFd, err, strlen(err), 0,
            (struct sockaddr*)&sun, addrlen) < 0 )
        log_error_errno("control socket send fail");
    log_setLevel(gParams.logLevel);
}

void cmdline_sendCtlMsg(int argc, char *argv[])
{
    struct sockaddr_un sunbind, sunsend;
    char buf[4096];
    int i, len, buflen = 0;

    for(i = 0; i < argc; ++i) {
        len = strlen(argv[i]);
        if( buflen + len > sizeof(buf) ) {
            log_error("command line too long");
            return;
        }
        memcpy(buf + buflen, argv[i], len+1);
        buflen += len + 1;
    }
    int fd = socket(AF_UNIX, SOCK_DGRAM, 0);
    sunsend.sun_family = AF_UNIX;
    getCtlSockName(sunsend.sun_path, 0);
    sunbind = sunsend;
    strcat(sunbind.sun_path, "c");
    unlink(sunbind.sun_path);
    if( bind(fd, (struct sockaddr*)&sunbind, sizeof(sunbind)) != 0 )
        log_fatal_errno("bind");
    if( sendto(fd, buf, buflen, 0,
            (struct sockaddr*)&sunsend, sizeof(sunsend)) < 0 )
        log_error_errno("sendto");
    else{
        if( (len = recv(fd, buf, sizeof(buf), 0)) < 0 )
            log_error_errno("recv");
        else
            printf("\n%.*s\n", len, buf);
    }
    unlink(sunbind.sun_path);
}

