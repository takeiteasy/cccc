/* sys/ioctl.h - device control operations for CCCC */

#ifndef __SYS_IOCTL_H
#define __SYS_IOCTL_H

#ifdef _WIN32
#error "<sys/ioctl.h> is only available on POSIX targets in CCCC"
#endif

/*
 * Request codes below are the ones CCCC's ioctl() registration allowlists
 * (src/stdlib/posix.c wrap_ioctl, #795): a request is only forwarded to the
 * host ioctl() by default if the guest/host argument layout has been
 * verified for it. Anything else fails with -1/EINVAL unless the caller
 * opted into raw passthrough via --posix-emulation (matches the ppoll/
 * sched_* policy, #824). The numeric request codes themselves are
 * platform-specific and were read off real macOS and Linux headers (both
 * x86_64 and aarch64 agree on the Linux side).
 */
#ifdef __APPLE__
#define TIOCGWINSZ 0x40087468
#define TIOCSWINSZ 0x80087467
#define FIONREAD   0x4004667f
#define FIONBIO    0x8004667e
#define TIOCSCTTY  0x20007461
#define TIOCNOTTY  0x20007471
#else
#define TIOCGWINSZ 0x5413
#define TIOCSWINSZ 0x5414
#define FIONREAD   0x541b
#define FIONBIO    0x5421
#define TIOCSCTTY  0x540e
#define TIOCNOTTY  0x5422
#endif

struct winsize {
    unsigned short ws_row;
    unsigned short ws_col;
    unsigned short ws_xpixel;
    unsigned short ws_ypixel;
};

extern int ioctl(int fd, unsigned long request, ...);

#endif /* __SYS_IOCTL_H */
