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
        "\n"
        "   -FullScreen     - full screen mode\n"
        "   -passwd         - password file for authentication\n"
        "   -verbose        - print some debug info\n"
        "   -enableHextile  - enable Hextile encoding\n"
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
    while( i < argc ) {
        if( !strcmp(argv[i], "-FullScreen") )
            params->fullScreen = 1;
        else if( !strcmp(argv[i], "-passwd") )
            params->passwdFile = argv[++i];
        else if( !strcmp(argv[i], "-verbose") )
            ++params->logLevel;
        else if( !strcmp(argv[i], "-enableHextile") )
            params->enableHextile = 1;
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
        fprintf(stderr, "\nerror: host name not provided\n");
        usage();
    }
}

