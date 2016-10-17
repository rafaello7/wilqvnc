#ifndef CLICMDLINE_H
#define CLICMDLINE_H

typedef struct {
    const char *host;
    const char *passwdFile;
    int fullScreen;
    int logLevel;
    int enableHextile;
    int enableZRLE;
    int showFrameRate;
} CmdLineParams;

void cmdline_parse(int argc, char *argv[], CmdLineParams*);

#endif /* CLICMDLINE_H */
