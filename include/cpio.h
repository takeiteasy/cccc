/* cpio.h - extended cpio archive format constants for CCCC */

#ifndef __CPIO_H
#define __CPIO_H

/* Values are fixed by POSIX.1 itself, not by any host libc -- identical on
   every platform, nothing to verify. Header-only, no wrapper functions. */

/* POSIX mandates this exact unprefixed name. It is a generic identifier
   that will collide with any guest program's own `MAGIC` macro/enumerator
   -- inherent to the standard, not something this header can namespace
   away. */
#define MAGIC    "070707"

#define C_IRUSR  000400
#define C_IWUSR  000200
#define C_IXUSR  000100
#define C_IRGRP  000040
#define C_IWGRP  000020
#define C_IXGRP  000010
#define C_IROTH  000004
#define C_IWOTH  000002
#define C_IXOTH  000001

#define C_ISUID  004000
#define C_ISGID  002000
#define C_ISVTX  001000

#define C_ISBLK  060000
#define C_ISCHR  020000
#define C_ISDIR  040000
#define C_ISFIFO 010000
#define C_ISSOCK 0140000
#define C_ISLNK  0120000
#define C_ISCTG  0110000
#define C_ISREG  0100000

#endif /* __CPIO_H */
