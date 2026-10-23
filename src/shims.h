// shims.h -- the -c=native support shims.
//
// Each shim is a chunk of C under src/shims/<group>/<name>.c (never compiled
// by cccc itself), embedded into the binary by src/shim_table.c as
// `CCCC_SHIM_<group>_<name>`: a NUL-terminated string that
// src/serialize_shims.c copies into the generated program when its gating
// condition holds. A new chunk needs a row below.
//
// clang-format off
// Groups:
//   native_accessor  stdin/stdout/errno/optarg, isnan/isinf/signbit/fpclassify,
//                    FLT_ROUNDS, issignaling/iseqsig, MB_CUR_MAX, environ
//   reallocarray     reallocarray (remapped in serialize_expr.c)
//   threads          C11 <threads.h>, ported from src/stdlib/pthread.c
//   uchar            C11/C23 <uchar.h>, ported from src/stdlib/wide.c
//   posix_compat     --posix-emulation: ppoll, sched_*, gethostbyname_r, aio_fsync, in6_pktinfo
//   canonical_const  canonical-to-host constants: nl_langinfo, setlocale, sched_*, sysconf, pathconf
//   dlfcn            <dlfcn.h> over the VM's library registry
//   pool             thread pool and barrier shared by omp and kernel
//   omp              OpenMP runtime on the pool
//   kernel           cccc_* work-item builtins and launcher on the pool
//   checked          --checked-pointers checks (CHKR/CHKNT/CHKAB/CHKDC)
//
// wide_bitint, c23_math, c23_mem and c23_strtol each have one chunk,
// src/stdlib/<group>/runtime.c,
// also compiled into the VM; they are declared by hand below.
// clang-format on

#ifndef CCCC_SHIMS_H
#define CCCC_SHIMS_H

#define SHIMS(X)                                                               \
    X(native_accessor, __cccc_stdin)                                           \
    X(native_accessor, __cccc_stdout)                                          \
    X(native_accessor, __cccc_stderr)                                          \
    X(native_accessor, __cccc_errno_ptr)                                       \
    X(native_accessor, __cccc_optarg_ptr)                                      \
    X(native_accessor, __cccc_optind_ptr)                                      \
    X(native_accessor, __cccc_opterr_ptr)                                      \
    X(native_accessor, __cccc_optopt_ptr)                                      \
    X(native_accessor, __cccc_optreset_ptr)                                    \
    X(native_accessor, __cccc_isnan_f)                                         \
    X(native_accessor, __cccc_isnan_d)                                         \
    X(native_accessor, __cccc_isinf_f)                                         \
    X(native_accessor, __cccc_isinf_d)                                         \
    X(native_accessor, __cccc_signbit_f)                                       \
    X(native_accessor, __cccc_signbit_d)                                       \
    X(native_accessor, __cccc_fpclassify_f)                                    \
    X(native_accessor, __cccc_fpclassify_d)                                    \
    X(native_accessor, __cccc_flt_rounds)                                      \
    X(native_accessor, __cccc_issignaling_f)                                   \
    X(native_accessor, __cccc_issignaling_d)                                   \
    X(native_accessor, __cccc_iseqsig_f)                                       \
    X(native_accessor, __cccc_iseqsig_d)                                       \
    X(native_accessor, __cccc_mb_cur_max__linux)                               \
    X(native_accessor, __cccc_mb_cur_max__other)                               \
    X(native_accessor, __cccc_environ_ptr)                                     \
    X(reallocarray, body)                                                      \
    X(threads, includes)                                                       \
    X(threads, thrd_trampoline)                                                \
    X(threads, ensure_mtx)                                                     \
    X(threads, ensure_cnd)                                                     \
    X(threads, thrd_create)                                                    \
    X(threads, thrd_join)                                                      \
    X(threads, thrd_exit)                                                      \
    X(threads, thrd_detach)                                                    \
    X(threads, thrd_yield)                                                     \
    X(threads, thrd_sleep)                                                     \
    X(threads, thrd_current)                                                   \
    X(threads, thrd_equal)                                                     \
    X(threads, mtx_init)                                                       \
    X(threads, mtx_lock)                                                       \
    X(threads, mtx_trylock)                                                    \
    X(threads, mtx_timedlock)                                                  \
    X(threads, mtx_unlock)                                                     \
    X(threads, mtx_destroy)                                                    \
    X(threads, cnd_init)                                                       \
    X(threads, cnd_wait)                                                       \
    X(threads, cnd_signal)                                                     \
    X(threads, cnd_broadcast)                                                  \
    X(threads, cnd_timedwait)                                                  \
    X(threads, cnd_destroy)                                                    \
    X(threads, tss_create)                                                     \
    X(threads, tss_get)                                                        \
    X(threads, tss_set)                                                        \
    X(threads, tss_delete)                                                     \
    X(threads, call_once)                                                      \
    X(uchar, includes)                                                         \
    X(uchar, guard_16_32_open)                                                 \
    X(uchar, mbrtoc16)                                                         \
    X(uchar, c16rtomb)                                                         \
    X(uchar, mbrtoc32)                                                         \
    X(uchar, c32rtomb)                                                         \
    X(uchar, guard_8_open)                                                     \
    X(uchar, utf8_helpers)                                                     \
    X(uchar, mbrtoc8)                                                          \
    X(uchar, c8rtomb)                                                          \
    X(posix_compat, in6_pktinfo)                                               \
    X(posix_compat, aio_fsync)                                                 \
    X(posix_compat, includes)                                                  \
    X(posix_compat, poll_marshal)                                              \
    X(posix_compat, poll)                                                      \
    X(posix_compat, ppoll)                                                     \
    X(posix_compat, ppoll_linux_decl)                                          \
    X(posix_compat, sched_setparam)                                            \
    X(posix_compat, sched_getparam)                                            \
    X(posix_compat, sched_setscheduler)                                        \
    X(posix_compat, sched_getscheduler)                                        \
    X(posix_compat, sched_rr_get_interval)                                     \
    X(posix_compat, nss_helpers)                                               \
    X(posix_compat, gethostbyname_r)                                           \
    X(posix_compat, gethostbyaddr_r)                                           \
    X(posix_compat, getnetbyname_r)                                            \
    X(posix_compat, gethostbyname)                                             \
    X(posix_compat, gethostbyaddr)                                             \
    X(posix_compat, getnetbyname)                                              \
    X(canonical_const, nl_item_xlate)                                          \
    X(canonical_const, nl_langinfo)                                            \
    X(canonical_const, nl_langinfo_l)                                          \
    X(canonical_const, setlocale)                                              \
    X(canonical_const, newlocale)                                              \
    X(canonical_const, sched_batch_idle_defs)                                  \
    X(canonical_const, sched_policy_xlate)                                     \
    X(canonical_const, sched_get_priority_min)                                 \
    X(canonical_const, sched_get_priority_max)                                 \
    X(canonical_const, sysconf)                                                \
    X(canonical_const, pc_map)                                                 \
    X(canonical_const, pathconf)                                               \
    X(canonical_const, fpathconf)                                              \
    X(canonical_const, confstr)                                                \
    X(dlfcn, registry)                                                         \
    X(dlfcn, dlopen)                                                           \
    X(dlfcn, dlsym)                                                            \
    X(dlfcn, dlclose)                                                          \
    X(dlfcn, dlerror)                                                          \
    X(pool, includes)                                                          \
    X(pool, runtime)                                                           \
    X(omp, includes)                                                           \
    X(omp, runtime)                                                            \
    X(omp, lock_core)                                                          \
    X(omp, locks)                                                              \
    X(omp, nest_locks)                                                         \
    X(omp, schedule)                                                           \
    X(omp, api)                                                                \
    X(kernel, includes)                                                        \
    X(kernel, runtime)                                                         \
    X(checked, includes)                                                       \
    X(checked, runtime)

#define CCCC_SHIM_DECL(group, name)                                            \
    extern const char CCCC_SHIM_##group##_##name[]                             \
        __attribute__((visibility("hidden")));
SHIMS(CCCC_SHIM_DECL)
CCCC_SHIM_DECL(wide_bitint, runtime)
CCCC_SHIM_DECL(c23_math, runtime)
CCCC_SHIM_DECL(c23_mem, runtime)
CCCC_SHIM_DECL(c23_strtol, runtime)
#undef CCCC_SHIM_DECL

#endif
