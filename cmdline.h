#ifndef CMDLINE_H
#define CMDLINE_H

typedef struct {
    const char *host;
    const char *passwdFile;
    int fullScreen;
    int logLevel;
    int enableHextile;
} CmdLineParams;

void cmdline_parse(int argc, char *argv[], CmdLineParams*);

#endif /* CMDLINE_H */
