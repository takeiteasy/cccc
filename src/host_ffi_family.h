// host_ffi_family.h -- natives whose registered wrapper translates CCCC's
// canonical numbering or struct layout (bundled headers) to the host's.
//
// A program that compiles against the real libc headers
// (--use-system-headers) already holds the host's numbers and layouts, so its
// calls bind to the plain host passthrough registered as
// `__cccc_host_<name>` (ffi_name_for_callee, src/codegen_regalloc.c). Each
// stdlib file that owns a translator registers its passthroughs beside it;
// tests/host/test_host_ffi_family.c checks every row has one.

#ifndef CCCC_HOST_FFI_FAMILY_H
#define CCCC_HOST_FFI_FAMILY_H

#define CCCC_HOST_FFI_PREFIX     "__cccc_host_"
#define CCCC_HOST_FFI_PREFIX_LEN 12

// clang-format off
#define CCCC_HOST_FFI_FAMILY(X)                                                \
    X(sysconf) X(pathconf) X(fpathconf) X(confstr)                             \
    X(setlocale) X(newlocale)                                                  \
    X(nl_langinfo) X(nl_langinfo_l)                                            \
    X(poll)                                                                    \
    X(sched_get_priority_min) X(sched_get_priority_max)                        \
    X(statfs) X(fstatfs) X(statvfs) X(fstatvfs)                                \
    X(shmctl) X(semctl) X(msgctl)
// clang-format on

// Linux-only members (macOS declares no process-scheduling API, and its
// ppoll is a non-atomic emulation that exists only under --posix-emulation).
#ifdef __linux__
#define CCCC_HOST_FFI_FAMILY_LINUX(X)                                          \
    X(ppoll)                                                                   \
    X(sched_setparam)                                                          \
    X(sched_getparam) X(sched_setscheduler) X(sched_getscheduler)              \
        X(sched_rr_get_interval)
#else
#define CCCC_HOST_FFI_FAMILY_LINUX(X)
#endif

#endif
