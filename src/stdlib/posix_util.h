// Shared internal declarations for the POSIX stdlib split (#946).
//
// This header carries the system #include block every posix_*.c domain
// file needs (order-sensitive: __APPLE_USE_RFC_3542 before <netinet/in.h>,
// SCHED_BATCH/SCHED_IDLE before <sched.h>, IPC_INFO before <sys/ipc.h>),
// plus the handful of helpers genuinely shared across more than one domain
// file: GIL save/release (used by every blocking wrapper) and guest/host
// sigset translation (used by posix_poll.c's pselect/ppoll and
// posix_spawn.c's sigdefault/sigmask attr accessors). Everything else
// (pollfd marshalling, nss_static_mutex, SIGEV cookie machinery, etc.)
// is only ever used from the one domain file that defines it and stays
// static there -- see the ticket for the full domain map.
#ifndef CCCC_STDLIB_POSIX_UTIL_H
#define CCCC_STDLIB_POSIX_UTIL_H

#include "../cccc.h"
#include "../internal.h"

#if !defined(_WIN32) && !defined(_WIN64)
// Must be defined before <netinet/in.h> pulls in the real system header, or
// the advanced IPV6_* options (#749) -- IPV6_PKTINFO, IPV6_TCLASS, etc. --
// won't be visible on macOS (RFC 3542 options are opt-in there; Linux glibc
// exposes them unconditionally).
#define __APPLE_USE_RFC_3542
#include <aio.h>
#include <arpa/inet.h>
#include <dirent.h>
#include <fts.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <getopt.h>
#include <glob.h>
#include <grp.h>
#include <libgen.h>
#include <net/if.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <pwd.h>
#include <regex.h>
#include <iconv.h>
#include <langinfo.h>
#include <locale.h>
#ifdef __APPLE__
#include <xlocale.h>
#endif
#include <monetary.h>
#include <search.h>
#include <nl_types.h>
#include <sched.h>
#include <spawn.h>
#ifdef __linux__
// SCHED_BATCH/SCHED_IDLE are glibc extensions gated behind __USE_GNU, which
// the host <sched.h> only exposes under _GNU_SOURCE -- same gap class as
// mremap below, so defined locally with their known glibc values rather
// than flipping on _GNU_SOURCE for the whole TU.
#ifndef SCHED_BATCH
#define SCHED_BATCH 3
#endif
#ifndef SCHED_IDLE
#define SCHED_IDLE 5
#endif
#endif
#include <signal.h>
#include <string.h>
#include <strings.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/ipc.h>
#ifdef __linux__
// IPC_INFO is a glibc extension gated behind __USE_GNU, which the host
// <sys/ipc.h> only exposes under _GNU_SOURCE -- same gap class as
// SCHED_BATCH/SCHED_IDLE above and mremap below, so defined locally with
// its known glibc value rather than flipping on _GNU_SOURCE for the whole
// TU.
#ifndef IPC_INFO
#define IPC_INFO 3
#endif
#endif
#include <sys/mman.h>
#include <sys/msg.h>
#include <sys/resource.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/time.h>
#include <sys/times.h>
#include <sys/uio.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <syslog.h>
#include <termios.h>
#include <unistd.h>
#include <utime.h>
#include <wordexp.h>
#ifdef __linux__
#include <sys/vfs.h>
// mqueue.h (#805) -- Linux-only, no macOS equivalent at all (see
// include/mqueue.h). aio_read/aio_write/aio_error/aio_return/aio_cancel/
// aio_suspend/aio_fsync/lio_listio (#804) and mq_open/mq_send/mq_receive/
// mq_timedsend/mq_timedreceive/mq_notify/mq_setattr/mq_getattr all resolve
// straight out of libc.so.6 with no extra link flag needed -- glibc >=
// 2.34 merged librt's symbols into libc itself, verified by linking
// without -lrt in both Linux containers (x86_64 and aarch64).
#include <mqueue.h>
#else
#include <sys/mount.h>
#include <sys/param.h>
#endif
#if defined(__APPLE__) || defined(CCCC_HAS_NDBM)
// ndbm.h (#810, #871) -- macOS/BSD natively; on Linux only when built with
// CCCC_HAS_NDBM=1 against libgdbm-compat (see include/ndbm.h). Guarded
// independently of the __linux__ split above, not folded into either arm
// of it, since Linux can go either way depending on the build knob.
#include <ndbm.h>
#endif

// ---------------------------------------------------------------------------
// GIL helpers (mirrors the pattern in stdlib/pthread.c)
// These are used around blocking POSIX calls so other VM threads can run.
// ---------------------------------------------------------------------------

VirtualMachine *cccc_posix_current_vm(void);
void cccc_posix_save_and_release_gil(VirtualMachine *vm, ExecState *state);
void cccc_posix_acquire_and_restore_gil(VirtualMachine *vm, const ExecState *state);

// pselect()'s/ppoll's/spawn's sigdefault-sigmask sigmask can't be passed
// through: the guest's sigset_t is its own 4-byte bitmask (signals 1..31,
// bit signo-1 -- see signal.c's wrap_sigemptyset/wrap_sigaddset/etc.),
// reimplemented natively rather than aliasing the host's real sigset_t (a
// 128-byte struct on Linux) precisely to avoid an OOB read/write of that
// pointer (#738). So the guest mask is translated into a real host
// sigset_t via the host's own sigemptyset/sigaddset before the call --
// CCCC's SIG* constants already match the host's numbering
// (include/signal.h is #ifdef __APPLE__-guarded per platform), so signo
// translates unchanged.
void cccc_posix_guest_sigset_to_host(unsigned int guest_mask, sigset_t *host_set);

// Defined in posix_aio.c (SIGEV_THREAD cookie machinery, shared with
// posix_mqueue.c -- struct sigevent has the same layout regardless of
// which header pulled it in).
int cccc_posix_sigevent_prepare(struct sigevent *sev);

#endif /* !_WIN32 && !_WIN64 */

#endif /* CCCC_STDLIB_POSIX_UTIL_H */
