// std_table.c -- CCCC's bundled headers, embedded into the binary.
//
// One row per header: the name it is included by, the file it comes from, and
// the name of its stdlib registration function (0 for none). A new file under
// include/ needs a row here or it is only found when run from the repository
// root.
//
// Rows are sorted by name: get_std_header() binary-searches them. The private
// headers (reflection.h, testing.h, building.h) are listed by bare name for
// internal injection and are not includable by user source.

#include "./embed.h"
#include "./internal.h"
#include <stdlib.h>
#include <string.h>

#define STD_HEADERS(X) \
    X(std_Availability_h, "Availability.h", "include/Availability.h", 0) \
    X(std_aio_h, "aio.h", "include/aio.h", "register_posix_aio_functions") \
    X(std_arpa_inet_h, "arpa/inet.h", "include/arpa/inet.h", "register_posix_net_functions") \
    X(std_assert_h, "assert.h", "include/assert.h", 0) \
    X(std_building_h, "building.h", "include/cccc/building.h", 0) \
    X(std_cccc_kernel_h, "cccc/kernel.h", "include/cccc/kernel.h", 0) \
    X(std_cccc_opencl_h, "cccc/opencl.h", "include/cccc/opencl.h", 0) \
    X(std_complex_h, "complex.h", "include/complex.h", 0) \
    X(std_cpio_h, "cpio.h", "include/cpio.h", 0) \
    X(std_ctype_h, "ctype.h", "include/ctype.h", "register_ctype_functions") \
    X(std_decimal_math_h, "decimal_math.h", "include/decimal_math.h", "register_decimal_math_functions") \
    X(std_dirent_h, "dirent.h", "include/dirent.h", "register_posix_dir_functions") \
    X(std_dlfcn_h, "dlfcn.h", "include/dlfcn.h", "register_posix_functions") \
    X(std_errno_h, "errno.h", "include/errno.h", "register_posix_io_functions") \
    X(std_fcntl_h, "fcntl.h", "include/fcntl.h", "register_posix_io_functions") \
    X(std_fenv_h, "fenv.h", "include/fenv.h", "register_fenv_functions") \
    X(std_float_h, "float.h", "include/float.h", 0) \
    X(std_fnmatch_h, "fnmatch.h", "include/fnmatch.h", "register_posix_io_functions") \
    X(std_fts_h, "fts.h", "include/fts.h", "register_posix_dir_functions") \
    X(std_getopt_h, "getopt.h", "include/getopt.h", "register_posix_io_functions") \
    X(std_glob_h, "glob.h", "include/glob.h", "register_posix_dir_functions") \
    X(std_grp_h, "grp.h", "include/grp.h", "register_posix_io_functions") \
    X(std_iconv_h, "iconv.h", "include/iconv.h", "register_posix_lang_functions") \
    X(std_inttypes_h, "inttypes.h", "include/inttypes.h", 0) \
    X(std_iso646_h, "iso646.h", "include/iso646.h", 0) \
    X(std_langinfo_h, "langinfo.h", "include/langinfo.h", "register_posix_lang_functions") \
    X(std_libgen_h, "libgen.h", "include/libgen.h", "register_posix_io_functions") \
    X(std_limits_h, "limits.h", "include/limits.h", 0) \
    X(std_locale_h, "locale.h", "include/locale.h", "register_locale_functions") \
    X(std_math_h, "math.h", "include/math.h", "register_math_functions") \
    X(std_monetary_h, "monetary.h", "include/monetary.h", "register_posix_lang_functions") \
    X(std_mqueue_h, "mqueue.h", "include/mqueue.h", "register_posix_mqueue_functions") \
    X(std_ndbm_h, "ndbm.h", "include/ndbm.h", "register_posix_ndbm_functions") \
    X(std_net_if_h, "net/if.h", "include/net/if.h", "register_posix_net_functions") \
    X(std_netdb_h, "netdb.h", "include/netdb.h", "register_posix_net_functions") \
    X(std_netinet_in_h, "netinet/in.h", "include/netinet/in.h", "register_posix_functions") \
    X(std_nl_types_h, "nl_types.h", "include/nl_types.h", "register_posix_lang_functions") \
    X(std_omp_h, "omp.h", "include/omp.h", 0) \
    X(std_poll_h, "poll.h", "include/poll.h", "register_posix_poll_functions") \
    X(std_pthread_h, "pthread.h", "include/pthread.h", "register_pthread_functions") \
    X(std_pwd_h, "pwd.h", "include/pwd.h", "register_posix_io_functions") \
    X(std_reflection_h, "reflection.h", "include/cccc/reflection.h", 0) \
    X(std_regex_h, "regex.h", "include/regex.h", "register_posix_dir_functions") \
    X(std_sched_h, "sched.h", "include/sched.h", "register_posix_sched_functions") \
    X(std_search_h, "search.h", "include/search.h", "register_posix_search_functions") \
    X(std_setjmp_h, "setjmp.h", "include/setjmp.h", 0) \
    X(std_signal_h, "signal.h", "include/signal.h", "register_signal_functions") \
    X(std_spawn_h, "spawn.h", "include/spawn.h", "register_posix_spawn_functions") \
    X(std_stdalign_h, "stdalign.h", "include/stdalign.h", 0) \
    X(std_stdarg_h, "stdarg.h", "include/stdarg.h", 0) \
    X(std_stdatomic_h, "stdatomic.h", "include/stdatomic.h", 0) \
    X(std_stdbit_h, "stdbit.h", "include/stdbit.h", 0) \
    X(std_stdbool_h, "stdbool.h", "include/stdbool.h", 0) \
    X(std_stdckdint_h, "stdckdint.h", "include/stdckdint.h", 0) \
    X(std_stddef_h, "stddef.h", "include/stddef.h", 0) \
    X(std_stdint_h, "stdint.h", "include/stdint.h", 0) \
    X(std_stdio_h, "stdio.h", "include/stdio.h", "register_stdio_functions") \
    X(std_stdlib_h, "stdlib.h", "include/stdlib.h", "register_stdlib_functions") \
    X(std_stdnoreturn_h, "stdnoreturn.h", "include/stdnoreturn.h", 0) \
    X(std_string_h, "string.h", "include/string.h", "register_string_functions") \
    X(std_strings_h, "strings.h", "include/strings.h", "register_posix_functions") \
    X(std_sys_cdefs_h, "sys/cdefs.h", "include/sys/cdefs.h", 0) \
    X(std_sys_file_h, "sys/file.h", "include/sys/file.h", "register_posix_io_functions") \
    X(std_sys_ioctl_h, "sys/ioctl.h", "include/sys/ioctl.h", "register_posix_io_functions") \
    X(std_sys_ipc_h, "sys/ipc.h", "include/sys/ipc.h", "register_posix_ipc_functions") \
    X(std_sys_mman_h, "sys/mman.h", "include/sys/mman.h", "register_posix_io_functions") \
    X(std_sys_mount_h, "sys/mount.h", "include/sys/mount.h", "register_posix_statfs_functions") \
    X(std_sys_msg_h, "sys/msg.h", "include/sys/msg.h", "register_posix_ipc_functions") \
    X(std_sys_param_h, "sys/param.h", "include/sys/param.h", 0) \
    X(std_sys_resource_h, "sys/resource.h", "include/sys/resource.h", "register_posix_functions") \
    X(std_sys_select_h, "sys/select.h", "include/sys/select.h", "register_posix_poll_functions") \
    X(std_sys_sem_h, "sys/sem.h", "include/sys/sem.h", "register_posix_ipc_functions") \
    X(std_sys_shm_h, "sys/shm.h", "include/sys/shm.h", "register_posix_ipc_functions") \
    X(std_sys_socket_h, "sys/socket.h", "include/sys/socket.h", "register_posix_net_functions") \
    X(std_sys_stat_h, "sys/stat.h", "include/sys/stat.h", "register_posix_io_functions") \
    X(std_sys_statvfs_h, "sys/statvfs.h", "include/sys/statvfs.h", "register_posix_statfs_functions") \
    X(std_sys_time_h, "sys/time.h", "include/sys/time.h", "register_posix_io_functions") \
    X(std_sys_times_h, "sys/times.h", "include/sys/times.h", "register_posix_io_functions") \
    X(std_sys_types_h, "sys/types.h", "include/sys/types.h", 0) \
    X(std_sys_uio_h, "sys/uio.h", "include/sys/uio.h", "register_posix_io_functions") \
    X(std_sys_un_h, "sys/un.h", "include/sys/un.h", "register_posix_functions") \
    X(std_sys_utsname_h, "sys/utsname.h", "include/sys/utsname.h", "register_posix_io_functions") \
    X(std_sys_wait_h, "sys/wait.h", "include/sys/wait.h", "register_posix_wait_functions") \
    X(std_syslog_h, "syslog.h", "include/syslog.h", "register_posix_lang_functions") \
    X(std_tar_h, "tar.h", "include/tar.h", 0) \
    X(std_termios_h, "termios.h", "include/termios.h", "register_posix_io_functions") \
    X(std_testing_h, "testing.h", "include/cccc/testing.h", 0) \
    X(std_tgmath_h, "tgmath.h", "include/tgmath.h", 0) \
    X(std_threads_h, "threads.h", "include/threads.h", "register_threads_functions") \
    X(std_time_h, "time.h", "include/time.h", "register_time_functions") \
    X(std_uchar_h, "uchar.h", "include/uchar.h", "register_wide_functions") \
    X(std_unistd_h, "unistd.h", "include/unistd.h", "register_posix_functions") \
    X(std_utime_h, "utime.h", "include/utime.h", "register_posix_io_functions") \
    X(std_wchar_h, "wchar.h", "include/wchar.h", "register_wide_functions") \
    X(std_wctype_h, "wctype.h", "include/wctype.h", "register_wide_functions") \
    X(std_wordexp_h, "wordexp.h", "include/wordexp.h", "register_posix_wordexp_functions") \
    X(std_xlocale_h, "xlocale.h", "include/xlocale.h", 0)

#define X(id, name, path, reg) EMBED_DECL(id, path);
STD_HEADERS(X)
#undef X

typedef struct {
    const char *name;
    const char *src;
    const char *reg_fn;
} StdHeader;

static const StdHeader std_headers[] = {
#define X(id, name, path, reg) {name, id, reg},
    STD_HEADERS(X)
#undef X
};

#define STD_HEADER_COUNT (int)(sizeof(std_headers) / sizeof(std_headers[0]))

static int std_header_cmp(const void *key, const void *elem) {
    return strcmp((const char *)key, ((const StdHeader *)elem)->name);
}

static const StdHeader *find_std_header(const char *name) {
    return bsearch(name, std_headers, STD_HEADER_COUNT, sizeof(StdHeader),
                   std_header_cmp);
}

const char *get_std_header(const char *filename) {
    const StdHeader *h = find_std_header(filename);
    return h ? h->src : 0;
}

const char *get_stdlib_reg_fn_name(const char *header) {
    const StdHeader *h = find_std_header(header);
    return h ? h->reg_fn : 0;
}

const char *get_std_header_name(int i) {
    return i >= 0 && i < STD_HEADER_COUNT ? std_headers[i].name : 0;
}
