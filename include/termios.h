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
    cc_t c_cc[NCCS];
    speed_t c_ispeed;
    speed_t c_ospeed;
};
#else
typedef unsigned int tcflag_t;
typedef unsigned char cc_t;
typedef unsigned int speed_t;
#define NCCS 32
struct termios {
    tcflag_t c_iflag;
    tcflag_t c_oflag;
    tcflag_t c_cflag;
    tcflag_t c_lflag;
    cc_t c_line;
    cc_t c_cc[NCCS];
    speed_t c_ispeed;
    speed_t c_ospeed;
};
#endif

#define TCSANOW   0
#define TCSADRAIN 1
#define TCSAFLUSH 2

#define VEOF   0
#define VEOL   1
#define VERASE 3
#define VINTR  8
#define VMIN   16
#define VTIME  17

#define BRKINT  0x00000002
#define ICRNL   0x00000100
#define IXON    0x00000200
#define OPOST   0x00000001
#define CS8     0x00000300
#define CREAD   0x00000800
#define ECHO    0x00000008
#define ICANON  0x00000100
#define ISIG    0x00000080

#define B0       0
#define B9600    9600
#define B19200   19200
#define B38400   38400
#define B115200  115200

extern int tcgetattr(int fildes, struct termios *termios_p);
extern int tcsetattr(int fildes, int optional_actions,
                     const struct termios *termios_p);

/* Line control */
#define TCIFLUSH  0
#define TCOFLUSH  1
#define TCIOFLUSH 2

#define TCOOFF 0
#define TCOON  1
#define TCIOFF 2
#define TCION  3

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
