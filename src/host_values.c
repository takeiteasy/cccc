// host_values.c -- injects the real host's values for every macro name that
// src/host_shadow_macros.h restores under self-hosting (#1315).
//
// Same transitive-injection pattern as init_errno_macros()/init_time_macros()
// (src/preprocess.c, #779/#813/#1282): this TU is compiled by the real host
// compiler against the real system headers, so a plain `#ifdef`/reference to
// one of these names here already sees the host's real value. That value is
// defined as a `__CCCC_HOSTV_<NAME>__` macro visible to CCCC's own guest
// parser, which src/host_shadow_macros.h uses to restore the name when it has
// been shadowed by CCCC's bundled, canonically-numbered header.
//
// This file #includes host_shadow_macros.h itself (after the system headers,
// before defining anything) so the chain is a fixed point: under
// self-hosting this TU's own `#include <unistd.h>` et al. is bundled too, so
// without re-applying the shim here, a *second* self-hosting generation
// would reinject the canonical (wrong) values instead of the real ones.
#include "cccc.h"
#include "internal.h"

#include <locale.h>
#include <poll.h>
#include <sched.h>
#include <unistd.h>

#include "host_shadow_macros.h"

void cccc_init_host_shadow_macros(VirtualMachine *vm) {
#define H(name)                                                                \
    define_macro(vm, "__CCCC_HOSTV_" #name "__", arena_format(vm, "%d", name))

#ifdef _SC_ARG_MAX
    H(_SC_ARG_MAX);
#endif
#ifdef _SC_CHILD_MAX
    H(_SC_CHILD_MAX);
#endif
#ifdef _SC_CLK_TCK
    H(_SC_CLK_TCK);
#endif
#ifdef _SC_NGROUPS_MAX
    H(_SC_NGROUPS_MAX);
#endif
#ifdef _SC_OPEN_MAX
    H(_SC_OPEN_MAX);
#endif
#ifdef _SC_STREAM_MAX
    H(_SC_STREAM_MAX);
#endif
#ifdef _SC_TZNAME_MAX
    H(_SC_TZNAME_MAX);
#endif
#ifdef _SC_JOB_CONTROL
    H(_SC_JOB_CONTROL);
#endif
#ifdef _SC_SAVED_IDS
    H(_SC_SAVED_IDS);
#endif
#ifdef _SC_VERSION
    H(_SC_VERSION);
#endif
#ifdef _SC_PAGESIZE
    H(_SC_PAGESIZE);
#endif
#ifdef _SC_NPROCESSORS_CONF
    H(_SC_NPROCESSORS_CONF);
#endif
#ifdef _SC_NPROCESSORS_ONLN
    H(_SC_NPROCESSORS_ONLN);
#endif
#ifdef _SC_PHYS_PAGES
    H(_SC_PHYS_PAGES);
#endif
#ifdef _SC_LINE_MAX
    H(_SC_LINE_MAX);
#endif
#ifdef _SC_RE_DUP_MAX
    H(_SC_RE_DUP_MAX);
#endif
#ifdef _SC_2_VERSION
    H(_SC_2_VERSION);
#endif
#ifdef _SC_XOPEN_VERSION
    H(_SC_XOPEN_VERSION);
#endif
#ifdef _SC_HOST_NAME_MAX
    H(_SC_HOST_NAME_MAX);
#endif
#ifdef _SC_LOGIN_NAME_MAX
    H(_SC_LOGIN_NAME_MAX);
#endif
#ifdef _SC_TTY_NAME_MAX
    H(_SC_TTY_NAME_MAX);
#endif
#ifdef _SC_SYMLOOP_MAX
    H(_SC_SYMLOOP_MAX);
#endif
#ifdef _SC_ATEXIT_MAX
    H(_SC_ATEXIT_MAX);
#endif
#ifdef _SC_IOV_MAX
    H(_SC_IOV_MAX);
#endif
#ifdef _SC_GETPW_R_SIZE_MAX
    H(_SC_GETPW_R_SIZE_MAX);
#endif
#ifdef _SC_GETGR_R_SIZE_MAX
    H(_SC_GETGR_R_SIZE_MAX);
#endif
#ifdef _SC_MONOTONIC_CLOCK
    H(_SC_MONOTONIC_CLOCK);
#endif

#ifdef _PC_LINK_MAX
    H(_PC_LINK_MAX);
#endif
#ifdef _PC_MAX_CANON
    H(_PC_MAX_CANON);
#endif
#ifdef _PC_MAX_INPUT
    H(_PC_MAX_INPUT);
#endif
#ifdef _PC_NAME_MAX
    H(_PC_NAME_MAX);
#endif
#ifdef _PC_PATH_MAX
    H(_PC_PATH_MAX);
#endif
#ifdef _PC_PIPE_BUF
    H(_PC_PIPE_BUF);
#endif
#ifdef _PC_CHOWN_RESTRICTED
    H(_PC_CHOWN_RESTRICTED);
#endif
#ifdef _PC_NO_TRUNC
    H(_PC_NO_TRUNC);
#endif
#ifdef _PC_VDISABLE
    H(_PC_VDISABLE);
#endif

#ifdef _CS_PATH
    H(_CS_PATH);
#endif

#ifdef SCHED_OTHER
    H(SCHED_OTHER);
#endif
#ifdef SCHED_FIFO
    H(SCHED_FIFO);
#endif
#ifdef SCHED_RR
    H(SCHED_RR);
#endif

#ifdef POLLIN
    H(POLLIN);
#endif
#ifdef POLLPRI
    H(POLLPRI);
#endif
#ifdef POLLOUT
    H(POLLOUT);
#endif
#ifdef POLLERR
    H(POLLERR);
#endif
#ifdef POLLHUP
    H(POLLHUP);
#endif
#ifdef POLLNVAL
    H(POLLNVAL);
#endif
#ifdef POLLRDNORM
    H(POLLRDNORM);
#endif
#ifdef POLLRDBAND
    H(POLLRDBAND);
#endif
#ifdef POLLWRNORM
    H(POLLWRNORM);
#endif
#ifdef POLLWRBAND
    H(POLLWRBAND);
#endif

#ifdef LC_ALL
    H(LC_ALL);
#endif
#ifdef LC_COLLATE
    H(LC_COLLATE);
#endif
#ifdef LC_CTYPE
    H(LC_CTYPE);
#endif
#ifdef LC_MONETARY
    H(LC_MONETARY);
#endif
#ifdef LC_NUMERIC
    H(LC_NUMERIC);
#endif
#ifdef LC_TIME
    H(LC_TIME);
#endif
#ifdef LC_MESSAGES
    H(LC_MESSAGES);
#endif
#ifdef LC_COLLATE_MASK
    H(LC_COLLATE_MASK);
#endif
#ifdef LC_CTYPE_MASK
    H(LC_CTYPE_MASK);
#endif
#ifdef LC_MESSAGES_MASK
    H(LC_MESSAGES_MASK);
#endif
#ifdef LC_MONETARY_MASK
    H(LC_MONETARY_MASK);
#endif
#ifdef LC_NUMERIC_MASK
    H(LC_NUMERIC_MASK);
#endif
#ifdef LC_TIME_MASK
    H(LC_TIME_MASK);
#endif
#ifdef LC_ALL_MASK
    H(LC_ALL_MASK);
#endif

#undef H
}
