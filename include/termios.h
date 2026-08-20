/* termios.h - terminal I/O declarations for CCCC */

#ifndef __TERMIOS_H
#define __TERMIOS_H

#ifdef _WIN32
#error "<termios.h> is only available on POSIX targets in CCCC"
#endif

#ifdef __APPLE__
typedef unsigned long tcflag_t;
typedef unsigned char cc_t;
typedef unsigned long speed_t;
#define NCCS 20
struct termios {
    tcflag_t c_iflag;
    tcflag_t c_oflag;
    tcflag_t c_cflag;
    tcflag_t c_lflag;
    cc_t     c_cc[NCCS];
    speed_t  c_ispeed;
    speed_t  c_ospeed;
};
#else
typedef unsigned int  tcflag_t;
typedef unsigned char cc_t;
typedef unsigned int  speed_t;
#define NCCS 32
struct termios {
    tcflag_t c_iflag;
    tcflag_t c_oflag;
    tcflag_t c_cflag;
    tcflag_t c_lflag;
    cc_t     c_line;
    cc_t     c_cc[NCCS];
    speed_t  c_ispeed;
    speed_t  c_ospeed;
};
#endif

#define TCSANOW   0
#define TCSADRAIN 1
#define TCSAFLUSH 2

/* c_iflag/c_oflag/c_cflag/c_lflag bits and c_cc[] indices differ between
   macOS/BSD (<sys/termios.h>) and Linux (<asm-generic/termbits.h> via glibc
   <bits/termios.h>). Verified against real headers on macOS/x86_64,
   Linux/x86_64, and Linux/aarch64 -- the Linux values are identical across
   x86_64 and aarch64 (termios bits are arch-invariant on Linux except a
   handful of exotic ports not targeted by CCCC). */
#ifdef __APPLE__
#define VEOF     0
#define VEOL     1
#define VEOL2    2
#define VERASE   3
#define VWERASE  4
#define VKILL    5
#define VREPRINT 6
#define VINTR    8
#define VQUIT    9
#define VSUSP    10
#define VDSUSP   11
#define VSTART   12
#define VSTOP    13
#define VLNEXT   14
#define VDISCARD 15
#define VMIN     16
#define VTIME    17

#define IGNBRK   0x00000001
#define BRKINT   0x00000002
#define IGNPAR   0x00000004
#define PARMRK   0x00000008
#define INPCK    0x00000010
#define ISTRIP   0x00000020
#define INLCR    0x00000040
#define IGNCR    0x00000080
#define ICRNL    0x00000100
#define IXON     0x00000200
#define IXOFF    0x00000400
#define IXANY    0x00000800

#define OPOST    0x00000001
#define ONLCR    0x00000002
#define OCRNL    0x00000010
#define ONOCR    0x00000020
#define ONLRET   0x00000040

#define CS5      0x00000000
#define CS6      0x00000100
#define CS7      0x00000200
#define CS8      0x00000300
#define CSTOPB   0x00000400
#define CREAD    0x00000800
#define PARENB   0x00001000
#define PARODD   0x00002000
#define HUPCL    0x00004000
#define CLOCAL   0x00008000

#define ECHO     0x00000008
#define ECHOE    0x00000002
#define ECHOK    0x00000004
#define ECHONL   0x00000010
#define ICANON   0x00000100
#define IEXTEN   0x00000400
#define ISIG     0x00000080
#define NOFLSH   0x80000000
#define TOSTOP   0x00400000

#define B0       0
#define B9600    9600
#define B19200   19200
#define B38400   38400
#define B57600   57600
#define B115200  115200
#define B230400  230400
#else
#define VEOF     4
#define VEOL     11
#define VEOL2    16
#define VERASE   2
#define VWERASE  14
#define VKILL    3
#define VREPRINT 12
#define VINTR    0
#define VQUIT    1
#define VSUSP    10
#define VSTART   8
#define VSTOP    9
#define VLNEXT   15
#define VDISCARD 13
#define VMIN     6
#define VTIME    5

#define IGNBRK   0000001
#define BRKINT   0000002
#define IGNPAR   0000004
#define PARMRK   0000010
#define INPCK    0000020
#define ISTRIP   0000040
#define INLCR    0000100
#define IGNCR    0000200
#define ICRNL    0000400
#define IXON     0002000
#define IXOFF    0010000
#define IXANY    0004000

#define OPOST    0000001
#define ONLCR    0000004
#define OCRNL    0000010
#define ONOCR    0000020
#define ONLRET   0000040

#define CS5      0000000
#define CS6      0000020
#define CS7      0000040
#define CS8      0000060
#define CSTOPB   0000100
#define CREAD    0000200
#define PARENB   0000400
#define PARODD   0001000
#define HUPCL    0002000
#define CLOCAL   0004000

#define ECHO     0000010
#define ECHOE    0000020
#define ECHOK    0000040
#define ECHONL   0000100
#define ICANON   0000002
#define IEXTEN   0100000
#define ISIG     0000001
#define NOFLSH   0000200
#define TOSTOP   0000400

#define B0       0000000
#define B9600    0000015
#define B19200   0000016
#define B38400   0000017
#define B57600   0010001
#define B115200  0010002
#define B230400  0010003
#endif

extern int tcgetattr(int fildes, struct termios *termios_p);
extern int tcsetattr(int fildes, int optional_actions,
                     const struct termios *termios_p);

/* Line control */
#define TCIFLUSH  0
#define TCOFLUSH  1
#define TCIOFLUSH 2

#define TCOOFF    0
#define TCOON     1
#define TCIOFF    2
#define TCION     3

extern speed_t cfgetispeed(const struct termios *termios_p);
extern speed_t cfgetospeed(const struct termios *termios_p);
extern int cfsetispeed(struct termios *termios_p, speed_t speed);
extern int cfsetospeed(struct termios *termios_p, speed_t speed);
extern int cfsetspeed(struct termios *termios_p, speed_t speed);
extern void cfmakeraw(struct termios *termios_p);
extern int tcdrain(int fildes);
extern int tcflow(int fildes, int action);
extern int tcflush(int fildes, int queue_selector);
extern int tcsendbreak(int fildes, int duration);

#endif /* __TERMIOS_H */
