/* langinfo.h - locale information (POSIX) for CCCC
 *
 * nl_item values diverge wildly between hosts: macOS uses a flat 0-56
 * sequence, while glibc packs `(category << 16) | index` (e.g. D_T_FMT is
 * 1 on macOS but 131112 -- LC_TIME(2)<<16 | 40 -- on Linux, verified
 * against real headers). The constants below use CCCC's own canonical
 * numbering, which happens to equal macOS's real numbering (so no
 * translation is needed there); wrap_nl_langinfo (src/stdlib/posix.c)
 * translates to the host's real nl_item on Linux via range arithmetic
 * for the DAY_/ABDAY_/MON_/ABMON_ families (each is a contiguous run in
 * both numbering schemes) plus a small table for the rest, returning ""
 * for anything it doesn't recognize.
 *
 * nl_item itself is declared in <sys/types.h>, not redeclared here.
 */

#ifndef __LANGINFO_H
#define __LANGINFO_H

#ifdef _WIN32
#error "<langinfo.h> is only available on POSIX targets in CCCC"
#endif

#include "sys/types.h"

#define CODESET     0
#define D_T_FMT     1
#define D_FMT       2
#define T_FMT       3
#define T_FMT_AMPM  4
#define AM_STR      5
#define PM_STR      6

#define DAY_1  7
#define DAY_2  8
#define DAY_3  9
#define DAY_4  10
#define DAY_5  11
#define DAY_6  12
#define DAY_7  13

#define ABDAY_1  14
#define ABDAY_2  15
#define ABDAY_3  16
#define ABDAY_4  17
#define ABDAY_5  18
#define ABDAY_6  19
#define ABDAY_7  20

#define MON_1  21
#define MON_2  22
#define MON_3  23
#define MON_4  24
#define MON_5  25
#define MON_6  26
#define MON_7  27
#define MON_8  28
#define MON_9  29
#define MON_10 30
#define MON_11 31
#define MON_12 32

#define ABMON_1  33
#define ABMON_2  34
#define ABMON_3  35
#define ABMON_4  36
#define ABMON_5  37
#define ABMON_6  38
#define ABMON_7  39
#define ABMON_8  40
#define ABMON_9  41
#define ABMON_10 42
#define ABMON_11 43
#define ABMON_12 44

#define RADIXCHAR 50
#define THOUSEP   51

#define YESEXPR 52
#define NOEXPR  53

#define CRNCYSTR 56

extern char *nl_langinfo(nl_item item);

#endif /* __LANGINFO_H */
