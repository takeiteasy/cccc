// host_shadow_macros.h -- undo header shadowing for cccc's own host-facing
// FFI-translation source when cccc compiles cccc (#1315).
//
// A handful of files under src/stdlib/ (posix_sched.c's wrap_sysconf/
// wrap_pathconf/wrap_fpathconf/wrap_confstr, posix_poll.c's guest_to_host_
// pollev/host_to_guest_pollev, locale.c's guest_to_host_lc/guest_to_host_
// lc_mask) exist purely to translate CCCC's canonical guest numbering to
// whatever the *real* host's libc actually expects, via `#ifdef REAL_MACRO`
// tables that need the real macro's real value. Under a normal `make`
// build this is automatic: cccc never processes its own source, so their
// `#include <unistd.h>`/`<poll.h>`/`<sched.h>`/`<locale.h>` always resolves
// to the real system header. Under self-hosting (`cccc -c=native src/*.c
// ...`), those same files are guest input, and search_include_paths()'s
// force_cccc policy (src/preprocess.c) resolves every standard header CCCC
// knows about to CCCC's own bundled, canonically-numbered copy instead --
// so the translation tables collapse to identity and an untranslated
// canonical constant reaches the real host libc.
//
// This header is a no-op under a real host compiler (the real headers
// already gave every name below its real value there). Under __CCCC__ it
// restores the real value the *compiling* cccc injected via
// cccc_init_host_shadow_macros() (src/host_values.c) -- which obtained it
// the same way, one generation down -- the same transitive-injection
// pattern init_errno_macros()/init_time_macros() already use for errno
// codes (#779/#813) and CLOCK_* ids (#1282).
//
// A name this header doesn't cover stays exactly as the bundled header
// left it -- in particular, a name genuinely absent on the host that built
// cccc stays undefined (#824 no-lossy-emulation policy), so every existing
// `#ifdef _SC_FOO` guard in the translating wrappers keeps working
// unchanged.
//
// NOT covered here: SCHED_BATCH/SCHED_IDLE. src/stdlib/posix_util.h
// already defines those locally (#ifndef-guarded) when the real <sched.h>
// doesn't have them (glibc gates them behind _GNU_SOURCE; macOS lacks them
// entirely) -- shadowing and restoring them here would just race that
// guard. Also not covered: _SC_PAGE_SIZE, a pure alias of _SC_PAGESIZE
// (include/unistd.h) -- restoring _SC_PAGESIZE below is sufficient.
// Also not covered: PATH_MAX, _XOPEN_VERSION -- deliberately-divergent
// VM-model constants, not part of any translation table (see
// tools/audit_host_macro_shadow.py's allowlist).

#ifndef CCCC_HOST_SHADOW_MACROS_H
#define CCCC_HOST_SHADOW_MACROS_H

#ifdef __CCCC__

// clang-format off
#undef _SC_ARG_MAX
#ifdef __CCCC_HOSTV__SC_ARG_MAX__
#define _SC_ARG_MAX __CCCC_HOSTV__SC_ARG_MAX__
#endif
#undef _SC_CHILD_MAX
#ifdef __CCCC_HOSTV__SC_CHILD_MAX__
#define _SC_CHILD_MAX __CCCC_HOSTV__SC_CHILD_MAX__
#endif
#undef _SC_CLK_TCK
#ifdef __CCCC_HOSTV__SC_CLK_TCK__
#define _SC_CLK_TCK __CCCC_HOSTV__SC_CLK_TCK__
#endif
#undef _SC_NGROUPS_MAX
#ifdef __CCCC_HOSTV__SC_NGROUPS_MAX__
#define _SC_NGROUPS_MAX __CCCC_HOSTV__SC_NGROUPS_MAX__
#endif
#undef _SC_OPEN_MAX
#ifdef __CCCC_HOSTV__SC_OPEN_MAX__
#define _SC_OPEN_MAX __CCCC_HOSTV__SC_OPEN_MAX__
#endif
#undef _SC_STREAM_MAX
#ifdef __CCCC_HOSTV__SC_STREAM_MAX__
#define _SC_STREAM_MAX __CCCC_HOSTV__SC_STREAM_MAX__
#endif
#undef _SC_TZNAME_MAX
#ifdef __CCCC_HOSTV__SC_TZNAME_MAX__
#define _SC_TZNAME_MAX __CCCC_HOSTV__SC_TZNAME_MAX__
#endif
#undef _SC_JOB_CONTROL
#ifdef __CCCC_HOSTV__SC_JOB_CONTROL__
#define _SC_JOB_CONTROL __CCCC_HOSTV__SC_JOB_CONTROL__
#endif
#undef _SC_SAVED_IDS
#ifdef __CCCC_HOSTV__SC_SAVED_IDS__
#define _SC_SAVED_IDS __CCCC_HOSTV__SC_SAVED_IDS__
#endif
#undef _SC_VERSION
#ifdef __CCCC_HOSTV__SC_VERSION__
#define _SC_VERSION __CCCC_HOSTV__SC_VERSION__
#endif
#undef _SC_PAGESIZE
#ifdef __CCCC_HOSTV__SC_PAGESIZE__
#define _SC_PAGESIZE __CCCC_HOSTV__SC_PAGESIZE__
#endif
#undef _SC_NPROCESSORS_CONF
#ifdef __CCCC_HOSTV__SC_NPROCESSORS_CONF__
#define _SC_NPROCESSORS_CONF __CCCC_HOSTV__SC_NPROCESSORS_CONF__
#endif
#undef _SC_NPROCESSORS_ONLN
#ifdef __CCCC_HOSTV__SC_NPROCESSORS_ONLN__
#define _SC_NPROCESSORS_ONLN __CCCC_HOSTV__SC_NPROCESSORS_ONLN__
#endif
#undef _SC_PHYS_PAGES
#ifdef __CCCC_HOSTV__SC_PHYS_PAGES__
#define _SC_PHYS_PAGES __CCCC_HOSTV__SC_PHYS_PAGES__
#endif
#undef _SC_LINE_MAX
#ifdef __CCCC_HOSTV__SC_LINE_MAX__
#define _SC_LINE_MAX __CCCC_HOSTV__SC_LINE_MAX__
#endif
#undef _SC_RE_DUP_MAX
#ifdef __CCCC_HOSTV__SC_RE_DUP_MAX__
#define _SC_RE_DUP_MAX __CCCC_HOSTV__SC_RE_DUP_MAX__
#endif
#undef _SC_2_VERSION
#ifdef __CCCC_HOSTV__SC_2_VERSION__
#define _SC_2_VERSION __CCCC_HOSTV__SC_2_VERSION__
#endif
#undef _SC_XOPEN_VERSION
#ifdef __CCCC_HOSTV__SC_XOPEN_VERSION__
#define _SC_XOPEN_VERSION __CCCC_HOSTV__SC_XOPEN_VERSION__
#endif
#undef _SC_HOST_NAME_MAX
#ifdef __CCCC_HOSTV__SC_HOST_NAME_MAX__
#define _SC_HOST_NAME_MAX __CCCC_HOSTV__SC_HOST_NAME_MAX__
#endif
#undef _SC_LOGIN_NAME_MAX
#ifdef __CCCC_HOSTV__SC_LOGIN_NAME_MAX__
#define _SC_LOGIN_NAME_MAX __CCCC_HOSTV__SC_LOGIN_NAME_MAX__
#endif
#undef _SC_TTY_NAME_MAX
#ifdef __CCCC_HOSTV__SC_TTY_NAME_MAX__
#define _SC_TTY_NAME_MAX __CCCC_HOSTV__SC_TTY_NAME_MAX__
#endif
#undef _SC_SYMLOOP_MAX
#ifdef __CCCC_HOSTV__SC_SYMLOOP_MAX__
#define _SC_SYMLOOP_MAX __CCCC_HOSTV__SC_SYMLOOP_MAX__
#endif
#undef _SC_ATEXIT_MAX
#ifdef __CCCC_HOSTV__SC_ATEXIT_MAX__
#define _SC_ATEXIT_MAX __CCCC_HOSTV__SC_ATEXIT_MAX__
#endif
#undef _SC_IOV_MAX
#ifdef __CCCC_HOSTV__SC_IOV_MAX__
#define _SC_IOV_MAX __CCCC_HOSTV__SC_IOV_MAX__
#endif
#undef _SC_GETPW_R_SIZE_MAX
#ifdef __CCCC_HOSTV__SC_GETPW_R_SIZE_MAX__
#define _SC_GETPW_R_SIZE_MAX __CCCC_HOSTV__SC_GETPW_R_SIZE_MAX__
#endif
#undef _SC_GETGR_R_SIZE_MAX
#ifdef __CCCC_HOSTV__SC_GETGR_R_SIZE_MAX__
#define _SC_GETGR_R_SIZE_MAX __CCCC_HOSTV__SC_GETGR_R_SIZE_MAX__
#endif
#undef _SC_MONOTONIC_CLOCK
#ifdef __CCCC_HOSTV__SC_MONOTONIC_CLOCK__
#define _SC_MONOTONIC_CLOCK __CCCC_HOSTV__SC_MONOTONIC_CLOCK__
#endif

#undef _PC_LINK_MAX
#ifdef __CCCC_HOSTV__PC_LINK_MAX__
#define _PC_LINK_MAX __CCCC_HOSTV__PC_LINK_MAX__
#endif
#undef _PC_MAX_CANON
#ifdef __CCCC_HOSTV__PC_MAX_CANON__
#define _PC_MAX_CANON __CCCC_HOSTV__PC_MAX_CANON__
#endif
#undef _PC_MAX_INPUT
#ifdef __CCCC_HOSTV__PC_MAX_INPUT__
#define _PC_MAX_INPUT __CCCC_HOSTV__PC_MAX_INPUT__
#endif
#undef _PC_NAME_MAX
#ifdef __CCCC_HOSTV__PC_NAME_MAX__
#define _PC_NAME_MAX __CCCC_HOSTV__PC_NAME_MAX__
#endif
#undef _PC_PATH_MAX
#ifdef __CCCC_HOSTV__PC_PATH_MAX__
#define _PC_PATH_MAX __CCCC_HOSTV__PC_PATH_MAX__
#endif
#undef _PC_PIPE_BUF
#ifdef __CCCC_HOSTV__PC_PIPE_BUF__
#define _PC_PIPE_BUF __CCCC_HOSTV__PC_PIPE_BUF__
#endif
#undef _PC_CHOWN_RESTRICTED
#ifdef __CCCC_HOSTV__PC_CHOWN_RESTRICTED__
#define _PC_CHOWN_RESTRICTED __CCCC_HOSTV__PC_CHOWN_RESTRICTED__
#endif
#undef _PC_NO_TRUNC
#ifdef __CCCC_HOSTV__PC_NO_TRUNC__
#define _PC_NO_TRUNC __CCCC_HOSTV__PC_NO_TRUNC__
#endif
#undef _PC_VDISABLE
#ifdef __CCCC_HOSTV__PC_VDISABLE__
#define _PC_VDISABLE __CCCC_HOSTV__PC_VDISABLE__
#endif

#undef _CS_PATH
#ifdef __CCCC_HOSTV__CS_PATH__
#define _CS_PATH __CCCC_HOSTV__CS_PATH__
#endif

#undef SCHED_OTHER
#ifdef __CCCC_HOSTV_SCHED_OTHER__
#define SCHED_OTHER __CCCC_HOSTV_SCHED_OTHER__
#endif
#undef SCHED_FIFO
#ifdef __CCCC_HOSTV_SCHED_FIFO__
#define SCHED_FIFO __CCCC_HOSTV_SCHED_FIFO__
#endif
#undef SCHED_RR
#ifdef __CCCC_HOSTV_SCHED_RR__
#define SCHED_RR __CCCC_HOSTV_SCHED_RR__
#endif

#undef POLLIN
#ifdef __CCCC_HOSTV_POLLIN__
#define POLLIN __CCCC_HOSTV_POLLIN__
#endif
#undef POLLPRI
#ifdef __CCCC_HOSTV_POLLPRI__
#define POLLPRI __CCCC_HOSTV_POLLPRI__
#endif
#undef POLLOUT
#ifdef __CCCC_HOSTV_POLLOUT__
#define POLLOUT __CCCC_HOSTV_POLLOUT__
#endif
#undef POLLERR
#ifdef __CCCC_HOSTV_POLLERR__
#define POLLERR __CCCC_HOSTV_POLLERR__
#endif
#undef POLLHUP
#ifdef __CCCC_HOSTV_POLLHUP__
#define POLLHUP __CCCC_HOSTV_POLLHUP__
#endif
#undef POLLNVAL
#ifdef __CCCC_HOSTV_POLLNVAL__
#define POLLNVAL __CCCC_HOSTV_POLLNVAL__
#endif
#undef POLLRDNORM
#ifdef __CCCC_HOSTV_POLLRDNORM__
#define POLLRDNORM __CCCC_HOSTV_POLLRDNORM__
#endif
#undef POLLRDBAND
#ifdef __CCCC_HOSTV_POLLRDBAND__
#define POLLRDBAND __CCCC_HOSTV_POLLRDBAND__
#endif
#undef POLLWRNORM
#ifdef __CCCC_HOSTV_POLLWRNORM__
#define POLLWRNORM __CCCC_HOSTV_POLLWRNORM__
#endif
#undef POLLWRBAND
#ifdef __CCCC_HOSTV_POLLWRBAND__
#define POLLWRBAND __CCCC_HOSTV_POLLWRBAND__
#endif

#undef LC_ALL
#ifdef __CCCC_HOSTV_LC_ALL__
#define LC_ALL __CCCC_HOSTV_LC_ALL__
#endif
#undef LC_COLLATE
#ifdef __CCCC_HOSTV_LC_COLLATE__
#define LC_COLLATE __CCCC_HOSTV_LC_COLLATE__
#endif
#undef LC_CTYPE
#ifdef __CCCC_HOSTV_LC_CTYPE__
#define LC_CTYPE __CCCC_HOSTV_LC_CTYPE__
#endif
#undef LC_MONETARY
#ifdef __CCCC_HOSTV_LC_MONETARY__
#define LC_MONETARY __CCCC_HOSTV_LC_MONETARY__
#endif
#undef LC_NUMERIC
#ifdef __CCCC_HOSTV_LC_NUMERIC__
#define LC_NUMERIC __CCCC_HOSTV_LC_NUMERIC__
#endif
#undef LC_TIME
#ifdef __CCCC_HOSTV_LC_TIME__
#define LC_TIME __CCCC_HOSTV_LC_TIME__
#endif
#undef LC_MESSAGES
#ifdef __CCCC_HOSTV_LC_MESSAGES__
#define LC_MESSAGES __CCCC_HOSTV_LC_MESSAGES__
#endif
#undef LC_COLLATE_MASK
#ifdef __CCCC_HOSTV_LC_COLLATE_MASK__
#define LC_COLLATE_MASK __CCCC_HOSTV_LC_COLLATE_MASK__
#endif
#undef LC_CTYPE_MASK
#ifdef __CCCC_HOSTV_LC_CTYPE_MASK__
#define LC_CTYPE_MASK __CCCC_HOSTV_LC_CTYPE_MASK__
#endif
#undef LC_MESSAGES_MASK
#ifdef __CCCC_HOSTV_LC_MESSAGES_MASK__
#define LC_MESSAGES_MASK __CCCC_HOSTV_LC_MESSAGES_MASK__
#endif
#undef LC_MONETARY_MASK
#ifdef __CCCC_HOSTV_LC_MONETARY_MASK__
#define LC_MONETARY_MASK __CCCC_HOSTV_LC_MONETARY_MASK__
#endif
#undef LC_NUMERIC_MASK
#ifdef __CCCC_HOSTV_LC_NUMERIC_MASK__
#define LC_NUMERIC_MASK __CCCC_HOSTV_LC_NUMERIC_MASK__
#endif
#undef LC_TIME_MASK
#ifdef __CCCC_HOSTV_LC_TIME_MASK__
#define LC_TIME_MASK __CCCC_HOSTV_LC_TIME_MASK__
#endif
#undef LC_ALL_MASK
#ifdef __CCCC_HOSTV_LC_ALL_MASK__
#define LC_ALL_MASK __CCCC_HOSTV_LC_ALL_MASK__
#endif
// clang-format on

#endif /* __CCCC__ */

#endif /* CCCC_HOST_SHADOW_MACROS_H */
