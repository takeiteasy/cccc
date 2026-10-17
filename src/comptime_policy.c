/*
 CCCC: Comprehensiev C Compensation Compiler

 Copyright (C) 2025 George Watson

 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

// Maps native function names to the capability class comptime needs for them.
// A name no table lists is denied until --comptime-allow=all, so a newly
// registered binding fails closed (tests/host/test_comptime_policy.c checks
// every registered name is listed).

#include "comptime_policy.h"
#include <ctype.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "internal.h"

static const char *const FS_NAMES[] = {
    "__cccc_dbm_delete",
    "__cccc_dbm_fetch",
    "__cccc_dbm_firstkey",
    "__cccc_dbm_nextkey",
    "__cccc_dbm_store",
    "access",
    "aio_cancel",
    "aio_error",
    "aio_fsync",
    "aio_read",
    "aio_return",
    "aio_suspend",
    "aio_write",
    "catclose",
    "catgets",
    "catopen",
    "cfmakeraw",
    "chdir",
    "chmod",
    "chown",
    "clearerr",
    "close",
    "closedir",
    "closelog",
    "creat",
    "dbm_clearerr",
    "dbm_close",
    "dbm_error",
    "dbm_open",
    "dup",
    "dup2",
    "fallocate",
    "fchdir",
    "fchmod",
    "fchmodat",
    "fchown",
    "fclose",
    "fcntl",
    "fdatasync",
    "fdopen",
    "feof",
    "ferror",
    "fgetc",
    "fgetpos",
    "fgets",
    "fileno",
    "flock",
    "flockfile",
    "fopen",
    "fpathconf",
    "fread",
    "freopen",
    "fscanf",
    "fseek",
    "fseeko",
    "fsetpos",
    "fstat",
    "fstatat",
    "fstatfs",
    "fstatvfs",
    "fsync",
    "ftell",
    "ftello",
    "ftruncate",
    "ftrylockfile",
    "fts_children",
    "fts_close",
    "fts_open",
    "fts_read",
    "fts_set",
    "funlockfile",
    "futimes",
    "getc",
    "getc_unlocked",
    "getchar",
    "getchar_unlocked",
    "getcwd",
    "glob",
    "globfree",
    "ioctl",
    "lchown",
    "link",
    "lio_listio",
    "lseek",
    "lstat",
    "lutimes",
    "mkdir",
    "mkdirat",
    "mkdtemp",
    "mkfifo",
    "mknod",
    "mkstemp",
    "mkstemps",
    "mlock",
    "mlockall",
    "mmap",
    "mprotect",
    "mremap",
    "msync",
    "munlock",
    "munlockall",
    "munmap",
    "open",
    "open_memstream",
    "opendir",
    "openlog",
    "pathconf",
    "pipe",
    "poll",
    "posix_madvise",
    "ppoll",
    "pread",
    "preadv",
    "preadv2",
    "pselect",
    "pwrite",
    "pwritev",
    "pwritev2",
    "read",
    "readdir",
    "readdir_r",
    "readlink",
    "readv",
    "realpath",
    "remove",
    "rename",
    "rewind",
    "rewinddir",
    "rmdir",
    "scandir",
    "scanf",
    "seekdir",
    "select",
    "setbuf",
    "setlogmask",
    "setvbuf",
    "splice",
    "stat",
    "statfs",
    "statvfs",
    "symlink",
    "syslog",
    "tcdrain",
    "tcflow",
    "tcflush",
    "tcgetattr",
    "tcsendbreak",
    "tcsetattr",
    "telldir",
    "tmpfile",
    "tmpnam",
    "truncate",
    "ttyname",
    "ttyname_r",
    "umask",
    "ungetc",
    "unlink",
    "utime",
    "utimes",
    "vfscanf",
    "vscanf",
    "vsyslog",
    "write",
    "writev",
};

static const char *const EXEC_NAMES[] = {
    "clock_settime",
    "execl",
    "execle",
    "execlp",
    "execv",
    "execve",
    "execvp",
    "fork",
    "initgroups",
    "kill",
    "killpg",
    "nice",
    "pclose",
    "popen",
    "posix_spawn",
    "posix_spawn_file_actions_addclose",
    "posix_spawn_file_actions_adddup2",
    "posix_spawn_file_actions_addopen",
    "posix_spawn_file_actions_destroy",
    "posix_spawn_file_actions_init",
    "posix_spawnattr_destroy",
    "posix_spawnattr_getflags",
    "posix_spawnattr_getpgroup",
    "posix_spawnattr_getsigdefault",
    "posix_spawnattr_getsigmask",
    "posix_spawnattr_init",
    "posix_spawnattr_setflags",
    "posix_spawnattr_setpgroup",
    "posix_spawnattr_setsigdefault",
    "posix_spawnattr_setsigmask",
    "posix_spawnp",
    "setegid",
    "seteuid",
    "setgid",
    "setgroups",
    "sethostname",
    "setpgid",
    "setpriority",
    "setrlimit",
    "setsid",
    "settimeofday",
    "setuid",
    "system",
    "wait",
    "wait3",
    "wait4",
    "waitid",
    "waitpid",
    "wordexp",
    "wordfree",
};

static const char *const DLOPEN_NAMES[] = {
    "dlclose",
    "dlopen",
    "dlsym",
};

static const char *const NET_NAMES[] = {
    "accept",           "bind",          "connect",
    "endnetent",        "endprotoent",   "endservent",
    "freeaddrinfo",     "getaddrinfo",   "gethostbyaddr",
    "gethostbyaddr_r",  "gethostbyname", "gethostbyname_r",
    "getnameinfo",      "getnetbyaddr",  "getnetbyname",
    "getnetbyname_r",   "getpeername",   "getprotobyname",
    "getprotobynumber", "getservbyname", "getservbyport",
    "getsockname",      "getsockopt",    "if_freenameindex",
    "if_indextoname",   "if_nameindex",  "if_nametoindex",
    "listen",           "recv",          "recvfrom",
    "recvmsg",          "send",          "sendmsg",
    "sendto",           "setnetent",     "setprotoent",
    "setservent",       "setsockopt",    "shutdown",
    "sockatmark",       "socket",        "socketpair",
};

static const char *const ENV_NAMES[] = {
    "getenv",      "getgrgid",           "getgrgid_r", "getgrnam", "getgrnam_r",
    "gethostname", "getlogin",           "getlogin_r", "getpwnam", "getpwnam_r",
    "getpwuid",    "getpwuid_r",         "putenv",     "setenv",   "uname",
    "unsetenv",    "__cccc_environ_ptr",
};

static const char *const THREAD_NAMES[] = {
    "__cccc_call_once",
    "cnd_broadcast",
    "cnd_destroy",
    "cnd_init",
    "cnd_signal",
    "cnd_timedwait",
    "cnd_wait",
    "mtx_destroy",
    "mtx_init",
    "mtx_lock",
    "mtx_timedlock",
    "mtx_trylock",
    "mtx_unlock",
    "pthread_attr_destroy",
    "pthread_attr_getstack",
    "pthread_attr_init",
    "pthread_attr_setstacksize",
    "pthread_cond_broadcast",
    "pthread_cond_destroy",
    "pthread_cond_init",
    "pthread_cond_signal",
    "pthread_cond_timedwait",
    "pthread_cond_wait",
    "pthread_create",
    "pthread_detach",
    "pthread_equal",
    "pthread_exit",
    "pthread_getspecific",
    "pthread_join",
    "pthread_key_create",
    "pthread_key_delete",
    "pthread_mutex_destroy",
    "pthread_mutex_init",
    "pthread_mutex_lock",
    "pthread_mutex_trylock",
    "pthread_mutex_unlock",
    "pthread_mutexattr_destroy",
    "pthread_mutexattr_gettype",
    "pthread_mutexattr_init",
    "pthread_mutexattr_settype",
    "pthread_once",
    "pthread_self",
    "pthread_setspecific",
    "sched_getparam",
    "sched_getscheduler",
    "sched_rr_get_interval",
    "sched_setparam",
    "sched_setscheduler",
    "thrd_create",
    "thrd_current",
    "thrd_detach",
    "thrd_equal",
    "thrd_exit",
    "thrd_join",
    "thrd_sleep",
    "thrd_yield",
    "tss_create",
    "tss_delete",
    "tss_get",
    "tss_set",
};

// Signals, timers and atexit handlers: callbacks that outlive the comptime
// call that registered them.
static const char *const SIGNAL_NAMES[] = {
    "alarm",     "at_quick_exit", "atexit",          "getitimer",
    "pause",     "pthread_kill",  "pthread_sigmask", "setitimer",
    "sigaction", "sigprocmask",   "sigqueue",        "sigwait",
};

static const char *const IPC_NAMES[] = {
    "ftok",       "mq_close", "mq_getattr", "mq_notify",       "mq_open",
    "mq_receive", "mq_send",  "mq_setattr", "mq_timedreceive", "mq_timedsend",
    "mq_unlink",  "msgctl",   "msgget",     "msgrcv",          "msgsnd",
    "semctl",     "semget",   "semop",      "shm_open",        "shm_unlink",
    "shmat",      "shmctl",   "shmdt",      "shmget",
};

static const char *const SAFE_NAMES[] = {
    "_Exit",
    "_exit",
    "abort",
    "abs",
    "aligned_alloc",
    "alphasort",
    "asctime",
    "asctime_r",
    "asprintf",
    "atof",
    "atoi",
    "atol",
    "atoll",
    "basename",
    "bcmp",
    "bcopy",
    "bsearch",
    "btowc",
    "bzero",
    "c16rtomb",
    "c32rtomb",
    "c8rtomb",
    "calloc",
    "cfgetispeed",
    "cfgetospeed",
    "cfsetispeed",
    "cfsetospeed",
    "cfsetspeed",
    "clock",
    "clock_getres",
    "clock_gettime",
    "clock_nanosleep",
    "confstr",
    "ctime",
    "ctime_r",
    "difftime",
    "dirname",
    "div",
    "duplocale",
    "exit",
    "feclearexcept",
    "fegetenv",
    "fegetexceptflag",
    "fegetround",
    "feholdexcept",
    "feraiseexcept",
    "fesetenv",
    "fesetexceptflag",
    "fesetround",
    "fetestexcept",
    "feupdateenv",
    "fflush",
    "fnmatch",
    "fprintf",
    "fputc",
    "fputs",
    "free",
    "free_aligned_sized",
    "free_sized",
    "freelocale",
    "fwrite",
    "getegid",
    "geteuid",
    "getgid",
    "getgroups",
    "getopt",
    "getopt_long",
    "getpagesize",
    "getpgid",
    "getpgrp",
    "getpid",
    "getppid",
    "getpriority",
    "getrlimit",
    "getrusage",
    "getsid",
    "gettimeofday",
    "getuid",
    "gmtime",
    "gmtime_r",
    "hcreate",
    "hdestroy",
    "hsearch",
    "htonl",
    "htons",
    "iconv",
    "iconv_close",
    "iconv_open",
    "index",
    "inet_addr",
    "inet_ntoa",
    "inet_ntop",
    "inet_pton",
    "insque",
    "isatty",
    "iswalnum",
    "iswalpha",
    "iswblank",
    "iswcntrl",
    "iswctype",
    "iswdigit",
    "iswgraph",
    "iswlower",
    "iswprint",
    "iswpunct",
    "iswspace",
    "iswupper",
    "iswxdigit",
    "labs",
    "ldiv",
    "lfind",
    "llabs",
    "lldiv",
    "localeconv",
    "localtime",
    "localtime_r",
    "lsearch",
    "malloc",
    "mblen",
    "mbrlen",
    "mbrtoc16",
    "mbrtoc32",
    "mbrtoc8",
    "mbrtowc",
    "mbsinit",
    "mbsrtowcs",
    "mbstowcs",
    "mbtowc",
    "memalignment",
    "memccpy",
    "memchr",
    "memcmp",
    "memcpy",
    "memmove",
    "memset",
    "memset_explicit",
    "mktime",
    "nanosleep",
    "newlocale",
    "nl_langinfo",
    "ntohl",
    "ntohs",
    "perror",
    "posix_memalign",
    "printf",
    "putc",
    "putc_unlocked",
    "putchar",
    "putchar_unlocked",
    "puts",
    "qsort",
    "quick_exit",
    "rand",
    "realloc",
    "reallocarray",
    "regcomp",
    "regerror",
    "regexec",
    "regfree",
    "remque",
    "rindex",
    "sched_get_priority_max",
    "sched_get_priority_min",
    "sched_yield",
    "setlocale",
    "sigaddset",
    "sigdelset",
    "sigemptyset",
    "sigfillset",
    "sigismember",
    "sleep",
    "snprintf",
    "sprintf",
    "srand",
    "sscanf",
    "strcasecmp",
    "strcat",
    "strchr",
    "strcmp",
    "strcoll",
    "strcpy",
    "strcspn",
    "strdup",
    "strerror",
    "strfmon",
    "strfmon_l",
    "strftime",
    "strlen",
    "strncasecmp",
    "strncat",
    "strncmp",
    "strncpy",
    "strndup",
    "strpbrk",
    "strrchr",
    "strsignal",
    "strspn",
    "strstr",
    "strtod",
    "strtof",
    "strtok",
    "strtok_r",
    "strtol",
    "strtold",
    "strtoll",
    "strtoul",
    "strtoull",
    "strxfrm",
    "sysconf",
    "tdelete",
    "tfind",
    "time",
    "timegm",
    "times",
    "timespec_get",
    "tolower",
    "toupper",
    "towctrans",
    "towlower",
    "towupper",
    "tsearch",
    "twalk",
    "uselocale",
    "usleep",
    "vasprintf",
    "vfprintf",
    "vprintf",
    "vsnprintf",
    "vsprintf",
    "vsscanf",
    "wcrtomb",
    "wcscat",
    "wcschr",
    "wcscmp",
    "wcscpy",
    "wcslen",
    "wcsncat",
    "wcsncmp",
    "wcsncpy",
    "wcsrchr",
    "wcsrtombs",
    "wcsstr",
    "wcstod",
    "wcstof",
    "wcstol",
    "wcstold",
    "wcstoll",
    "wcstombs",
    "wcstoul",
    "wcstoull",
    "wcsxfrm",
    "wctob",
    "wctomb",
    "wctrans",
    "wctype",
};

// <ctype.h> predicates and case mappers. Their `_l` locale variants resolve
// through the same entries.
static const char *const CTYPE_NAMES[] = {
    "isalnum", "isalpha", "isblank", "iscntrl", "isdigit", "isgraph",
    "islower", "isprint", "ispunct", "isspace", "isupper", "isxdigit",
};

// <math.h> bases. An `f`/`l` suffix resolves through the base entry.
static const char *const MATH_BASES[] = {
    "acos",
    "acosh",
    "acospi",
    "asin",
    "asinh",
    "asinpi",
    "atan",
    "atan2",
    "atan2pi",
    "atanh",
    "atanpi",
    "canonicalize",
    "cbrt",
    "ceil",
    "copysign",
    "cos",
    "cosh",
    "cospi",
    "erf",
    "erfc",
    "exp",
    "exp10",
    "exp2",
    "expm1",
    "fabs",
    "fdim",
    "floor",
    "fma",
    "fmax",
    "fmaximum",
    "fmaximum_mag",
    "fmaximum_mag_num",
    "fmaximum_num",
    "fmin",
    "fminimum",
    "fminimum_mag",
    "fminimum_mag_num",
    "fminimum_num",
    "fmod",
    "frexp",
    "fromfp",
    "fromfpx",
    "getpayload",
    "hypot",
    "ilogb",
    "ldexp",
    "lgamma",
    "llogb",
    "llrint",
    "llround",
    "log",
    "log10",
    "log1p",
    "log2",
    "logb",
    "lrint",
    "lround",
    "modf",
    "nan",
    "nearbyint",
    "nextafter",
    "nexttoward",
    "pow",
    "remainder",
    "remquo",
    "rint",
    "round",
    "scalbln",
    "scalbn",
    "setpayload",
    "setpayloadsig",
    "sin",
    "sinh",
    "sinpi",
    "sqrt",
    "tan",
    "tanh",
    "tanpi",
    "tgamma",
    "totalorder",
    "totalordermag",
    "trunc",
    "ufromfp",
    "ufromfpx",
};

// Internal helpers the compiler emits calls to, and the comptime builtins.
static const char *const SAFE_PREFIXES[] = {
    "__builtin_",
    "__cccc_",
    "__sincos",
    "cccc_",
};

static const struct {
    const char *const *names;
    size_t             count;
    ComptimeClass      cls;
} TABLES[] = {
#define T(arr, c) {arr, sizeof(arr) / sizeof(arr[0]), c}
    T(FS_NAMES, CT_FS),         T(EXEC_NAMES, CT_EXEC),
    T(NET_NAMES, CT_NET),       T(ENV_NAMES, CT_ENV),
    T(THREAD_NAMES, CT_THREAD), T(SIGNAL_NAMES, CT_SIGNAL),
    T(IPC_NAMES, CT_IPC),       T(SAFE_NAMES, CT_SAFE),
    T(CTYPE_NAMES, CT_SAFE),    T(DLOPEN_NAMES, CT_DLOPEN),
#undef T
};

static const char *const CLASS_NAMES[CT_CLASS_COUNT] = {
    "safe", "fs", "exec", "net", "env", "dlopen", "thread", "signal", "ipc",
};

static HashMap        by_name;
static HashMap        math_bases;
static pthread_once_t table_once = PTHREAD_ONCE_INIT;

static void build_tables(void) {
    for (size_t t = 0; t < sizeof(TABLES) / sizeof(TABLES[0]); t++)
        for (size_t i = 0; i < TABLES[t].count; i++)
            hashmap_put(&by_name, TABLES[t].names[i],
                        (void *)(uintptr_t)(TABLES[t].cls + 1));
    for (size_t i = 0; i < sizeof(MATH_BASES) / sizeof(MATH_BASES[0]); i++)
        hashmap_put(&math_bases, MATH_BASES[i], (void *)(uintptr_t)1);
}

static int lookup_exact(const char *name, size_t len) {
    void *v = hashmap_get2(&by_name, name, (int)len);
    return v ? (int)(uintptr_t)v - 1 : -1;
}

int cccc_comptime_class_of(const char *name) {
    if (!name)
        return -1;
    pthread_once(&table_once, build_tables);

    size_t len = strlen(name);
    int    cls = lookup_exact(name, len);
    if (cls >= 0)
        return cls;

    // `_l` locale variants and `f`/`l` math variants share their base's class.
    if (len > 2 && name[len - 2] == '_' && name[len - 1] == 'l') {
        cls = lookup_exact(name, len - 2);
        if (cls >= 0)
            return cls;
    }
    if (len > 1 && (name[len - 1] == 'f' || name[len - 1] == 'l') &&
        hashmap_get2(&math_bases, name, (int)len - 1))
        return CT_SAFE;
    if (hashmap_get2(&math_bases, name, (int)len))
        return CT_SAFE;

    // --asm-passthru thunks run arbitrary host assembly.
    if (strncmp(name, "__cccc_asm_passthru_", 20) == 0)
        return -1;

    for (size_t i = 0; i < sizeof(SAFE_PREFIXES) / sizeof(SAFE_PREFIXES[0]);
         i++)
        if (strncmp(name, SAFE_PREFIXES[i], strlen(SAFE_PREFIXES[i])) == 0)
            return CT_SAFE;
    return -1;
}

bool cccc_comptime_is_math_name(const char *name, size_t len) {
    pthread_once(&table_once, build_tables);
    if (hashmap_get2(&math_bases, name, (int)len))
        return true;
    return len > 1 && (name[len - 1] == 'f' || name[len - 1] == 'l') &&
           hashmap_get2(&math_bases, name, (int)len - 1);
}

const char *cccc_comptime_class_name(ComptimeClass cls) {
    return (unsigned)cls < CT_CLASS_COUNT ? CLASS_NAMES[cls] : "?";
}

bool cccc_comptime_parse_classes(const char *list, unsigned *mask, char *bad,
                                 int bad_size) {
    const char *p = list;
    while (p && *p) {
        while (*p == ',' || isspace((unsigned char)*p))
            p++;
        const char *start = p;
        while (*p && *p != ',')
            p++;
        const char *end = p;
        while (end > start && isspace((unsigned char)end[-1]))
            end--;
        size_t len = (size_t)(end - start);
        if (len == 0)
            continue;

        if (len == 3 && strncmp(start, "all", 3) == 0) {
            *mask = COMPTIME_ALLOW_ALL;
            continue;
        }
        bool found = false;
        // "safe" is not grantable: it is always on.
        for (int c = CT_SAFE + 1; c < CT_CLASS_COUNT; c++) {
            if (strlen(CLASS_NAMES[c]) == len &&
                strncmp(start, CLASS_NAMES[c], len) == 0) {
                *mask |= 1u << c;
                found  = true;
                break;
            }
        }
        if (!found) {
            snprintf(bad, (size_t)bad_size, "%.*s", (int)len, start);
            return false;
        }
    }
    return true;
}

bool cccc_comptime_name_allowed(unsigned mask, const char *name, int *cls_out) {
    int cls = cccc_comptime_class_of(name);
    if (cls_out)
        *cls_out = cls;
    if (cls == CT_SAFE)
        return true;
    if (cls < 0)
        return (mask & COMPTIME_ALLOW_UNCLASSIFIED) != 0;
    return (mask & (1u << cls)) != 0;
}

void cccc_comptime_enforce(VirtualMachine *vm, const char *name) {
    if (vm->comptime_depth == 0)
        return;
    int cls;
    if (cccc_comptime_name_allowed(vm->comptime_allow, name, &cls))
        return;
    if (cls < 0)
        error("error: comptime call to '%s' denied (not in any capability "
              "class); "
              "pass --comptime-allow=all",
              name ? name : "<anonymous>");
    error("error: comptime call to '%s' denied (class %s); pass "
          "--comptime-allow=%s",
          name ? name : "<anonymous>", cccc_comptime_class_name(cls),
          cccc_comptime_class_name(cls));
}
