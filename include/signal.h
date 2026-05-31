/* signal.h - signal handling declarations for JCC */

#ifndef __SIGNAL_H
#define __SIGNAL_H

typedef int sig_atomic_t;

#define SIG_DFL ((void (*)(int))0)
#define SIG_IGN ((void (*)(int))1)
#define SIG_ERR ((void (*)(int))-1)

#define SIGABRT 6
#define SIGFPE 8
#define SIGILL 4
#define SIGINT 2
#define SIGSEGV 11
#define SIGTERM 15

extern void (*signal(int sig, void (*func)(int)))(int);
extern int raise(int sig);

#endif /* __SIGNAL_H */
