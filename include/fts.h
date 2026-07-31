/* fts.h - file tree traversal for CCCC (#811)
 *
 * Not in POSIX.1, but present on both macOS/BSD and glibc, so it's
 * implemented the same way as the rest of CCCC's host-backed POSIX layer:
 * FTS is opaque to the guest (never dereferenced, only passed back to
 * fts_read/fts_children/fts_set/fts_close, same shape as dirent.h's DIR),
 * and FTSENT is declared byte-exact per host so the host's fts_read()/
 * fts_children() can hand a real pointer straight to guest code.
 *
 * FTSENT is 112 bytes on macOS and 120 bytes on both glibc targets --
 * verified against the macOS SDK header and both Linux containers
 * (x86_64 and aarch64). The two glibc layouts differ from each other only
 * in fts_level's offset (96 on x86_64, 92 on aarch64), and that entirely
 * falls out of nlink_t's width (8 bytes on x86_64, 4 on aarch64, per
 * sys/types.h) -- so a single glibc branch written in terms of ino_t/
 * dev_t/nlink_t reproduces both without a third branch.
 *
 * fts_statp is a host `struct stat *` handed straight to the guest --
 * safe because include/sys/stat.h is itself already a byte-exact
 * per-platform pass-through (see its _Static_assert), not a marshalled
 * canonical struct.
 */

#ifndef __FTS_H
#define __FTS_H

#ifdef _WIN32
#error "<fts.h> is only available on POSIX targets in CCCC"
#endif

#include "sys/types.h"
#include "sys/stat.h"
#include "stddef.h" /* offsetof */

typedef struct __cccc_FTS FTS;

typedef struct _ftsent {
    struct _ftsent *fts_cycle;
    struct _ftsent *fts_parent;
    struct _ftsent *fts_link;
    long   fts_number;
    void  *fts_pointer;
    char  *fts_accpath;
    char  *fts_path;
    int    fts_errno;
    int    fts_symfd;
    unsigned short fts_pathlen;
    unsigned short fts_namelen;

    ino_t  fts_ino;
    dev_t  fts_dev;
    nlink_t fts_nlink;

    short  fts_level;

    unsigned short fts_info;
    unsigned short fts_flags;
    unsigned short fts_instr;

    struct stat *fts_statp;
    char   fts_name[1];
} FTSENT;

#ifdef __APPLE__
_Static_assert(sizeof(FTSENT) == 112, "macOS FTSENT layout mismatch");
_Static_assert(offsetof(FTSENT, fts_level) == 86, "macOS FTSENT fts_level offset mismatch");
#else
_Static_assert(sizeof(FTSENT) == 120, "glibc FTSENT layout mismatch");
/* fts_level's offset is the one place the two glibc targets disagree --
   it falls entirely out of nlink_t's width (8 bytes on x86_64, 4 on
   aarch64, per sys/types.h), so the struct definition above stays a
   single shared branch and only this assert is arch-split. */
#if defined(__x86_64__)
_Static_assert(offsetof(FTSENT, fts_level) == 96, "glibc x86_64 FTSENT fts_level offset mismatch");
#else
_Static_assert(offsetof(FTSENT, fts_level) == 92, "glibc aarch64 FTSENT fts_level offset mismatch");
#endif
#endif

/* fts_open() options -- identical bit values on macOS and glibc. */
#define FTS_COMFOLLOW 0x001
#define FTS_LOGICAL   0x002
#define FTS_NOCHDIR   0x004
#define FTS_NOSTAT    0x008
#define FTS_PHYSICAL  0x010
#define FTS_SEEDOT    0x020
#define FTS_XDEV      0x040
#define FTS_WHITEOUT  0x080

/* fts_level values -- identical on both. */
#define FTS_ROOTPARENTLEVEL (-1)
#define FTS_ROOTLEVEL       0

/* fts_info codes -- identical on both. */
#define FTS_D       1
#define FTS_DC      2
#define FTS_DEFAULT 3
#define FTS_DNR     4
#define FTS_DOT     5
#define FTS_DP      6
#define FTS_ERR     7
#define FTS_F       8
#define FTS_INIT    9
#define FTS_NS      10
#define FTS_NSOK    11
#define FTS_SL      12
#define FTS_SLNONE  13
#define FTS_W       14

/* fts_flags -- FTS_DONTCHDIR/FTS_SYMFOLLOW are identical on both; ISW and
   CHDIRFD are macOS-only (whiteout support glibc lacks). */
#define FTS_DONTCHDIR 0x01
#define FTS_SYMFOLLOW 0x02
#ifdef __APPLE__
#define FTS_ISW     0x04
#define FTS_CHDIRFD 0x08
#endif

/* fts_set() instructions -- identical on both. */
#define FTS_AGAIN   1
#define FTS_FOLLOW  2
#define FTS_NOINSTR 3
#define FTS_SKIP    4

/* fts_open()'s comparator callback runs through the same
   cccc_call_guest_callback trampoline as scandir()'s compar/select
   (src/stdlib/posix.c, #738) -- a real guest function pointer is safe to
   pass here, not just NULL. */
extern FTS *fts_open(char *const *path_argv, int options,
                     int (*compar)(const FTSENT **, const FTSENT **));
extern FTSENT *fts_read(FTS *ftsp);
extern FTSENT *fts_children(FTS *ftsp, int options);
extern int fts_set(FTS *ftsp, FTSENT *f, int instr);
extern int fts_close(FTS *ftsp);

#endif /* __FTS_H */
