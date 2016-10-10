#include "cmdline.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


static void usage(void)
{
    printf("\n"
        "usage: wilqvnc [parameters] host[:display]\n"
        "\n"
        "parameters:\n"
        "  -fs|-fullscreen         - full screen mode\n"
        "  -p |-passwd     <fname> - password file for authentication\n"
        "  -v |-verbose            - print some debug info\n"
        "  -x |-hextile            - enable Hextile encoding\n"
        "  -fp|-freqperiod         - print refresh frequency periodically\n"
        "  -h |-help               - print this help\n"
        "\n");
    exit(0);
}

void cmdline_parse(int argc, char *argv[], CmdLineParams *params)
{
    int i = 1;

    params->host = NULL;
    params->passwdFile = NULL;
    params->fullScreen = 0;
    params->logLevel = 0;
    params->enableHextile = 0;
    params->showFrameRate = 0;
    while( i < argc ) {
        if( !strcmp(argv[i], "-fs") || !strcmp(argv[i], "-fullscreen") )
            params->fullScreen = 1;
        else if( !strcmp(argv[i], "-p") || !strcmp(argv[i], "-passwd") )
            params->passwdFile = argv[++i];
        else if( !strcmp(argv[i], "-v") || !strcmp(argv[i], "-verbose") )
            ++params->logLevel;
        else if( !strcmp(argv[i], "-x") || !strcmp(argv[i], "-hextile") )
            params->enableHextile = 1;
        else if( !strcmp(argv[i], "-fp") || !strcmp(argv[i], "-freqperiod") )
            params->showFrameRate = 1;
        else if( !strcmp(argv[i], "-h") ||  !strcmp(argv[i], "-help") )
            usage();
        else if( argv[i][0] == '-' ) {
            fprintf(stderr, "error: unrecognized option -- %s\n\n", argv[i]);
            exit(1);
        }else
            params->host = argv[i];
        ++i;
    }
    if( params->host == NULL ) {
        if( argc > 1 )
            fprintf(stderr, "\nerror: host name not provided\n");
        usage();
    }
}

