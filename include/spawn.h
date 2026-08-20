/* spawn.h - process spawning (POSIX) for CCCC
 *
 * posix_spawnattr_t/posix_spawn_file_actions_t are opaque pointer-width
 * handles on the guest (typedef void *), matching macOS's own definition;
 * on Linux the real objects are structs (336/80 bytes). The wrappers in
 * src/stdlib/posix.c malloc a host-sized object for *_init, store the host
 * pointer through the guest's pointer-sized handle, and free it in
 * *_destroy -- uniform on both platforms since sizeof(posix_spawnattr_t)
 * is evaluated against the host's own type at compile time.
 *
 * POSIX_SPAWN_RESETIDS/SETPGROUP/SETSIGDEF/SETSIGMASK are identical (1/2/
 * 4/8) on both hosts and declared unguarded. POSIX_SPAWN_SETSID's real
 * numeric value genuinely diverges (0x400 on macOS vs 0x80 on Linux,
 * verified against real headers) -- like <sys/resource.h>'s RLIMIT_*, this
 * is guarded per-platform with the host's real value rather than
 * translated at a wrapper boundary, since flags are consumed directly by
 * the host's own posix_spawnattr_setflags(). macOS-only
 * POSIX_SPAWN_SETEXEC/START_SUSPENDED are declared under __APPLE__.
 *
 * posix_spawnattr_setsigdefault/setsigmask take a sigset_t -- CCCC's guest
 * sigset_t is its own 4-byte bitmask (see <signal.h>), translated to a
 * real host sigset_t by the wrappers, the same conversion pselect() uses
 * (guest_sigset_to_host, src/stdlib/posix.c).
 */

#ifndef __SPAWN_H
#define __SPAWN_H

#ifdef _WIN32
#error "<spawn.h> is only available on POSIX targets in CCCC"
#endif

#include "sys/types.h"
#include "signal.h"

typedef void *posix_spawnattr_t;
typedef void *posix_spawn_file_actions_t;

#define POSIX_SPAWN_RESETIDS   1
#define POSIX_SPAWN_SETPGROUP  2
#define POSIX_SPAWN_SETSIGDEF  4
#define POSIX_SPAWN_SETSIGMASK 8

#ifdef __APPLE__
#define POSIX_SPAWN_SETEXEC         0x0040
#define POSIX_SPAWN_START_SUSPENDED 0x0080
#define POSIX_SPAWN_SETSID          0x0400
#else
#define POSIX_SPAWN_SETSID 0x0080
#endif

extern int posix_spawn(pid_t *pid, const char *path,
                       const posix_spawn_file_actions_t *file_actions,
                       const posix_spawnattr_t *attrp, char *const argv[],
                       char *const envp[]);
extern int posix_spawnp(pid_t *pid, const char *file,
                        const posix_spawn_file_actions_t *file_actions,
                        const posix_spawnattr_t *attrp, char *const argv[],
                        char *const envp[]);

extern int posix_spawnattr_init(posix_spawnattr_t *attr);
extern int posix_spawnattr_destroy(posix_spawnattr_t *attr);
extern int posix_spawnattr_getflags(const posix_spawnattr_t *attr,
                                    short                   *flags);
extern int posix_spawnattr_setflags(posix_spawnattr_t *attr, short flags);
extern int posix_spawnattr_getpgroup(const posix_spawnattr_t *attr,
                                     pid_t                   *pgroup);
extern int posix_spawnattr_setpgroup(posix_spawnattr_t *attr, pid_t pgroup);
extern int posix_spawnattr_getsigdefault(const posix_spawnattr_t *attr,
                                         sigset_t                *sigdefault);
extern int posix_spawnattr_setsigdefault(posix_spawnattr_t *attr,
                                         const sigset_t    *sigdefault);
extern int posix_spawnattr_getsigmask(const posix_spawnattr_t *attr,
                                      sigset_t                *sigmask);
extern int posix_spawnattr_setsigmask(posix_spawnattr_t *attr,
                                      const sigset_t    *sigmask);

extern int
posix_spawn_file_actions_init(posix_spawn_file_actions_t *file_actions);
extern int
posix_spawn_file_actions_destroy(posix_spawn_file_actions_t *file_actions);
extern int
posix_spawn_file_actions_addopen(posix_spawn_file_actions_t *file_actions,
                                 int fildes, const char *path, int oflag,
                                 mode_t mode);
extern int
posix_spawn_file_actions_addclose(posix_spawn_file_actions_t *file_actions,
                                  int                         fildes);
extern int
posix_spawn_file_actions_adddup2(posix_spawn_file_actions_t *file_actions,
                                 int fildes, int newfildes);

#endif /* __SPAWN_H */
