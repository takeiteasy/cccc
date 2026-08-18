/* syslog.h - system logger interface for CCCC
 *
 * All LOG_* priority, option, and facility values are identical on macOS
 * and glibc/Linux (verified via probe on macOS arm64 and Linux x86_64/
 * aarch64) -- unguarded constants, #803.
 */

#ifndef __SYSLOG_H
#define __SYSLOG_H

#ifdef _WIN32
#error "<syslog.h> is only available on POSIX targets in CCCC"
#endif

#include <stdarg.h> // #1070: angle-bracket for a correct #include_next hand-off under real GCC

/* Priorities */
#define LOG_EMERG   0
#define LOG_ALERT   1
#define LOG_CRIT    2
#define LOG_ERR     3
#define LOG_WARNING 4
#define LOG_NOTICE  5
#define LOG_INFO    6
#define LOG_DEBUG   7

#define LOG_PRIMASK 0x07
#define LOG_PRI(p)  ((p) & LOG_PRIMASK)
#define LOG_MAKEPRI(fac, pri) ((fac) | (pri))

/* Facilities */
#define LOG_KERN     (0  << 3)
#define LOG_USER     (1  << 3)
#define LOG_MAIL     (2  << 3)
#define LOG_DAEMON   (3  << 3)
#define LOG_AUTH     (4  << 3)
#define LOG_SYSLOG   (5  << 3)
#define LOG_LPR      (6  << 3)
#define LOG_NEWS     (7  << 3)
#define LOG_UUCP     (8  << 3)
#define LOG_CRON     (9  << 3)
#define LOG_AUTHPRIV (10 << 3)
#define LOG_FTP      (11 << 3)
#define LOG_LOCAL0   (16 << 3)
#define LOG_LOCAL1   (17 << 3)
#define LOG_LOCAL2   (18 << 3)
#define LOG_LOCAL3   (19 << 3)
#define LOG_LOCAL4   (20 << 3)
#define LOG_LOCAL5   (21 << 3)
#define LOG_LOCAL6   (22 << 3)
#define LOG_LOCAL7   (23 << 3)

#define LOG_FACMASK  0x03f8
#define LOG_FAC(p)   (((p) & LOG_FACMASK) >> 3)

#define LOG_MASK(pri)   (1 << (pri))
#define LOG_UPTO(pri)   ((1 << ((pri) + 1)) - 1)

/* openlog() options */
#define LOG_PID    0x01
#define LOG_CONS   0x02
#define LOG_ODELAY 0x04
#define LOG_NDELAY 0x08
#define LOG_NOWAIT 0x10
#define LOG_PERROR 0x20

extern void openlog(const char *ident, int option, int facility);
extern void closelog(void);
extern void syslog(int priority, const char *format, ...);
extern void vsyslog(int priority, const char *format, va_list ap);
extern int setlogmask(int mask);

#endif /* __SYSLOG_H */
