/* sys/ioctl.h - device control operations for CCCC */

#ifndef __SYS_IOCTL_H
#define __SYS_IOCTL_H

#ifdef _WIN32
#error "<sys/ioctl.h> is only available on POSIX targets in CCCC"
#endif

/*
 * Terminal window size query (the only ioctl request portable code commonly
 * relies on). The numeric request codes are platform-specific.
 */
#ifdef __APPLE__
#define TIOCGWINSZ 0x40087468
#define TIOCSWINSZ 0x80087467
#else
#define TIOCGWINSZ 0x5413
#define TIOCSWINSZ 0x5414
#endif

struct winsize {
    unsigned short ws_row;
    unsigned short ws_col;
    unsigned short ws_xpixel;
    unsigned short ws_ypixel;
};

extern int ioctl(int fd, unsigned long request, ...);

#endif /* __SYS_IOCTL_H */
