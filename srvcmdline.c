#include "srvcmdline.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


static CmdLineParams gParams = {
    .zstdLevel = 3,
    .lz4Level  = 1,
    .compr     = COMPR_NONE
};


static void usage(void)
{
    printf("\n"
        "usage: wilqvncsrv [parameters] [:display]\n"
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
        "\n");
    exit(0);
}

const char *cmdline_parse(int argc, char *argv[])
{
    static char resultBuf[60];
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
            usage();
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

