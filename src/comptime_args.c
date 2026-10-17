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

// Comptime pointer validation at the native-call boundary. While comptime code
// runs, each safe-class libc call has its pointer arguments checked against VM
// memory, so a forged address becomes a compile error instead of a host fault.
// Host pointers libc hands back are either copied into the VM heap (strings,
// structs) or remembered as opaque handles (FILE *, locale_t, ...).
//
// A name's spec is a space-separated token per argument, optionally followed by
// "> <result>":
//
//   -        not a pointer
//   s        NUL-terminated string
//   n<size>  string bounded by <size> bytes
//   W        NUL-terminated wchar_t string
//   r<size>  readable buffer         w<size>  writable buffer
//   h        opaque host handle      c / C    strcat / strncat destination
//   f / F    printf / scanf format; the arguments it consumes are checked too
//   V        va_list pointer, consumed by the preceding format
//   suffix ? accepts NULL            suffix ! accepts (void *)-1
//
// <size> is a product of terms joined by '*': @i (argument i), @@i (the
// size_t argument i points at), $i (strlen of argument i + 1), =N or =name
// (constant). Results: S copy a host string into the VM heap, D the same and
// free the original, B=<size> copy a struct, h remember a handle, A copy the
// string written through argument 0 (asprintf).
//
// A pointer is checked for the bytes the call touches; pointers stored inside
// a checked buffer are not followed.
// tests/host/test_comptime_policy.c requires a spec for every safe-class name.

#include "comptime_policy.h"
#include <pthread.h>
#include <regex.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <sys/times.h>
#include <termios.h>
#include <time.h>
#include <wchar.h>

#include "internal.h"

#include <fenv.h>

static const struct {
    const char *name;
    const char *spec;
} SPECS[] = {
    {"_Exit", ""},
    {"_exit", ""},
    {"abort", ""},
    {"abs", ""},
    {"aligned_alloc", ""},
    {"alphasort", "r=8 r=8"},
    {"asctime", "r=tm > S"},
    {"asctime_r", "r=tm w=26"},
    {"asprintf", "w=8 f > A"},
    {"atof", "s"},
    {"atoi", "s"},
    {"atol", "s"},
    {"atoll", "s"},
    {"basename", "s? > S"},
    {"bcmp", "r@2 r@2 -"},
    {"bcopy", "r@2 w@2 -"},
    {"bsearch", "r@3 r@2*@3 - - -"},
    {"btowc", ""},
    {"bzero", "w@1 -"},
    {"c16rtomb", "w=16? - w=mbstate?"},
    {"c32rtomb", "w=16? - w=mbstate?"},
    {"c8rtomb", "w=16? - w=mbstate?"},
    {"calloc", ""},
    {"cfgetispeed", "r=termios"},
    {"cfgetospeed", "r=termios"},
    {"cfsetispeed", "w=termios -"},
    {"cfsetospeed", "w=termios -"},
    {"cfsetspeed", "w=termios -"},
    {"clock", ""},
    {"clock_getres", "- w=ts?"},
    {"clock_gettime", "- w=ts"},
    {"clock_nanosleep", "- - r=ts w=ts?"},
    {"confstr", "- w@2? -"},
    {"ctime", "r=8 > S"},
    {"ctime_r", "r=8 w=26"},
    {"difftime", ""},
    {"dirname", "s? > S"},
    {"div", ""},
    {"duplocale", "h > h"},
    {"exit", ""},
    {"feclearexcept", ""},
    {"fegetenv", "w=fenv"},
    {"fegetexceptflag", "w=fexcept -"},
    {"fegetround", ""},
    {"feholdexcept", "w=fenv"},
    {"feraiseexcept", ""},
    {"fesetenv", "r=fenv!"},
    {"fesetexceptflag", "r=fexcept -"},
    {"fesetround", ""},
    {"fetestexcept", ""},
    {"feupdateenv", "r=fenv!"},
    {"fflush", "h?"},
    {"fnmatch", "s s -"},
    {"fprintf", "h f"},
    {"fputc", "- h"},
    {"fputs", "s h"},
    {"free", ""},
    {"free_aligned_sized", ""},
    {"free_sized", ""},
    {"freelocale", "h"},
    {"fwrite", "r@1*@2 - - h"},
    {"getegid", ""},
    {"geteuid", ""},
    {"getgid", ""},
    {"getgroups", "- w@0*=4?"},
    {"getopt", "- r@0*=8 s"},
    {"getopt_long", "- r@0*=8 s r=8 w=4?"},
    {"getpagesize", ""},
    {"getpgid", ""},
    {"getpgrp", ""},
    {"getpid", ""},
    {"getppid", ""},
    {"getpriority", ""},
    {"getrlimit", "- w=rlimit"},
    {"getrusage", "- w=rusage"},
    {"getsid", ""},
    {"gettimeofday", "w=tv? w=8?"},
    {"getuid", ""},
    {"gmtime", "r=8 > B=tm"},
    {"gmtime_r", "r=8 w=tm"},
    {"hcreate", ""},
    {"hdestroy", ""},
    {"hsearch", ""},
    {"htonl", ""},
    {"htons", ""},
    {"iconv", "h w=8? w=8? w=8? w=8?"},
    {"iconv_close", "h"},
    {"iconv_open", "s s > h"},
    {"index", "s -"},
    {"inet_addr", "s"},
    {"inet_ntoa", "> S"},
    {"inet_ntop", "- r=4 w@3 -"},
    {"inet_pton", "- s w=4"},
    {"insque", "w=16 w=16?"},
    {"isatty", ""},
    {"iswalnum", ""},
    {"iswalpha", ""},
    {"iswblank", ""},
    {"iswcntrl", ""},
    {"iswctype", ""},
    {"iswdigit", ""},
    {"iswgraph", ""},
    {"iswlower", ""},
    {"iswprint", ""},
    {"iswpunct", ""},
    {"iswspace", ""},
    {"iswupper", ""},
    {"iswxdigit", ""},
    {"labs", ""},
    {"ldiv", ""},
    {"lfind", "r@3 r@@2*@3 r=8 - -"},
    {"llabs", ""},
    {"lldiv", ""},
    {"localeconv", ""},
    {"localtime", "r=8 > B=tm"},
    {"localtime_r", "r=8 w=tm"},
    {"lsearch", "r@3 w@@2*@3 w=8 - -"},
    {"malloc", ""},
    {"mblen", "r@1? -"},
    {"mbrlen", "r@1? - w=mbstate?"},
    {"mbrtoc16", "w=2? r@2? - w=mbstate?"},
    {"mbrtoc32", "w=4? r@2? - w=mbstate?"},
    {"mbrtoc8", "w=1? r@2? - w=mbstate?"},
    {"mbrtowc", "w=4? r@2? - w=mbstate?"},
    {"mbsinit", "r=mbstate?"},
    {"mbsrtowcs", "w@2*=4? w=8 - w=mbstate?"},
    {"mbstowcs", "w@2*=4? s -"},
    {"mbtowc", "w=4? r@2? -"},
    {"memalignment", ""},
    {"memccpy", "w@3 r@3 - -"},
    {"memchr", "r@2 - -"},
    {"memcmp", "r@2 r@2 -"},
    {"memcpy", "w@2 r@2 -"},
    {"memmove", "w@2 r@2 -"},
    {"memset", "w@2 - -"},
    {"memset_explicit", "w@2 - -"},
    {"mktime", "w=tm"},
    {"nanosleep", "r=ts w=ts?"},
    {"newlocale", "- s h? > h"},
    {"nl_langinfo", "- > S"},
    {"ntohl", ""},
    {"ntohs", ""},
    {"perror", "s?"},
    {"posix_memalign", "w=8 - -"},
    {"printf", "f"},
    {"putc", "- h"},
    {"putc_unlocked", "- h"},
    {"putchar", ""},
    {"putchar_unlocked", ""},
    {"puts", "s"},
    {"qsort", "w@1*@2 - - -"},
    {"quick_exit", ""},
    {"rand", ""},
    {"realloc", ""},
    {"reallocarray", ""},
    {"regcomp", "w=regex s -"},
    {"regerror", "- r=regex w@3? -"},
    {"regexec", "r=regex s - w@2*=regmatch? -"},
    {"regfree", "w=regex"},
    {"remque", "w=16"},
    {"rindex", "s -"},
    {"sched_get_priority_max", ""},
    {"sched_get_priority_min", ""},
    {"sched_yield", ""},
    {"setlocale", "- s? > S"},
    {"sigaddset", "w=sigset -"},
    {"sigdelset", "w=sigset -"},
    {"sigemptyset", "w=sigset"},
    {"sigfillset", "w=sigset"},
    {"sigismember", "r=sigset -"},
    {"sleep", ""},
    {"snprintf", "w@1? - f"},
    {"sprintf", "w=1 f"},
    {"srand", ""},
    {"sscanf", "s F"},
    {"strcasecmp", "s s"},
    {"strcat", "c s"},
    {"strchr", "s -"},
    {"strcmp", "s s"},
    {"strcoll", "s s"},
    {"strcpy", "w$1 s"},
    {"strcspn", "s s"},
    {"strdup", "s > D"},
    {"strerror", "- > S"},
    {"strfmon", "w@1 - s"},
    {"strfmon_l", "w@1 - h! s"},
    {"strftime", "w@1 - s r=tm"},
    {"strlen", "s"},
    {"strncasecmp", "n@2 n@2 -"},
    {"strncat", "C n@2 -"},
    {"strncmp", "n@2 n@2 -"},
    {"strncpy", "w@2 n@2 -"},
    {"strndup", "n@1 - > D"},
    {"strpbrk", "s s"},
    {"strrchr", "s -"},
    {"strsignal", "- > S"},
    {"strspn", "s s"},
    {"strstr", "s s"},
    {"strtod", "s w=8?"},
    {"strtof", "s w=8?"},
    {"strtok", "s? s"},
    {"strtok_r", "s? s w=8"},
    {"strtol", "s w=8? -"},
    {"strtold", "s w=8?"},
    {"strtoll", "s w=8? -"},
    {"strtoul", "s w=8? -"},
    {"strtoull", "s w=8? -"},
    {"strxfrm", "w@2? s -"},
    {"sysconf", ""},
    {"tdelete", "- w=8 -"},
    {"tfind", "- w=8 -"},
    {"time", "w=8?"},
    {"timegm", "w=tm"},
    {"times", "w=tms"},
    {"timespec_get", "w=ts -"},
    {"tolower", ""},
    {"toupper", ""},
    {"towctrans", ""},
    {"towlower", ""},
    {"towupper", ""},
    {"tsearch", "- w=8 -"},
    {"twalk", ""},
    {"uselocale", "h?! > h"},
    {"usleep", ""},
    {"vasprintf", "w=8 f V > A"},
    {"vfprintf", "h f V"},
    {"vprintf", "f V"},
    {"vsnprintf", "w@1? - f V"},
    {"vsprintf", "w=1 f V"},
    {"vsscanf", "s F V"},
    {"wcrtomb", "w=16? - w=mbstate?"},
    {"wcscat", "w=4 W"},
    {"wcschr", "W -"},
    {"wcscmp", "W W"},
    {"wcscpy", "w=4 W"},
    {"wcslen", "W"},
    {"wcsncat", "w=4 W -"},
    {"wcsncmp", "W W -"},
    {"wcsncpy", "w@2*=4 W -"},
    {"wcsrchr", "W -"},
    {"wcsrtombs", "w@2? w=8 - w=mbstate?"},
    {"wcsstr", "W W"},
    {"wcstod", "W w=8?"},
    {"wcstof", "W w=8?"},
    {"wcstol", "W w=8? -"},
    {"wcstold", "W w=8?"},
    {"wcstoll", "W w=8? -"},
    {"wcstombs", "w@2? W -"},
    {"wcstoul", "W w=8? -"},
    {"wcstoull", "W w=8? -"},
    {"wcsxfrm", "w@2*=4? W -"},
    {"wctob", ""},
    {"wctomb", "w=16?"},
    {"wctrans", "s"},
    {"wctype", "s"},
    // <ctype.h> predicates and case mappers.
    {"isalnum", ""},
    {"isalpha", ""},
    {"isblank", ""},
    {"iscntrl", ""},
    {"isdigit", ""},
    {"isgraph", ""},
    {"islower", ""},
    {"isprint", ""},
    {"ispunct", ""},
    {"isspace", ""},
    {"isupper", ""},
    {"isxdigit", ""},
    // <math.h> bases with pointer arguments; the rest need no spec.
    {"frexp", "- w=4"},
    {"getpayload", "r=8"},
    {"modf", "- w=4"},
    {"nan", "s"},
    {"remquo", "- - w=4"},
    {"setpayload", "w=8 -"},
    {"setpayloadsig", "w=8 -"},
    // The standard streams are host handles the guest never dereferences.
    {"__cccc_stderr", "> h"},
    {"__cccc_stdin", "> h"},
    {"__cccc_stdout", "> h"},
};

static const char *const HOST_PTR_RESULTS[] = {
    "fdopen",         "fdopendir", "fopen", "freopen",  "getcwd",
    "open_memstream", "opendir",   "popen", "realpath", "tmpfile",
};

static HashMap         specs;
static HashMap         host_ptrs;
static pthread_mutex_t host_lock  = PTHREAD_MUTEX_INITIALIZER;
static pthread_once_t  specs_once = PTHREAD_ONCE_INIT;

static void build_specs(void) {
    for (size_t i = 0; i < sizeof(SPECS) / sizeof(SPECS[0]); i++)
        hashmap_put(&specs, SPECS[i].name, (void *)SPECS[i].spec);
}

static bool has_suffix_l(const char *name, size_t len) {
    return len > 2 && name[len - 2] == '_' && name[len - 1] == 'l';
}

static const char *lookup_spec(const char *name, size_t len, bool *locale_tail,
                               bool *is_alias) {
    void *hit = hashmap_get2(&specs, name, (int)len);
    if (hit)
        return hit;
    if (has_suffix_l(name, len) &&
        (hit = hashmap_get2(&specs, name, (int)len - 2))) {
        *locale_tail = true;
        return hit;
    }
    if (len > 1 && (name[len - 1] == 'f' || name[len - 1] == 'l') &&
        (hit = hashmap_get2(&specs, name, (int)len - 1))) {
        *is_alias = true;
        return hit;
    }
    return NULL;
}

const char *cccc_comptime_arg_spec(const char *name) {
    if (!name)
        return NULL;
    pthread_once(&specs_once, build_specs);
    size_t      len         = strlen(name);
    bool        locale_tail = false, alias = false;
    const char *spec = lookup_spec(name, len, &locale_tail, &alias);
    if (spec)
        return spec;
    // Math functions and internal helpers that need no argument checks.
    if (cccc_comptime_class_of(name) == CT_SAFE &&
        (cccc_comptime_is_math_name(name, len) ||
         strncmp(name, "__builtin_", 10) == 0 ||
         strncmp(name, "__cccc_", 7) == 0 ||
         strncmp(name, "__sincos", 8) == 0 || strncmp(name, "cccc_", 5) == 0))
        return "";
    return NULL;
}

// --- Host pointer registry ---------------------------------------------

void cccc_comptime_host_ptr_add(void *p) {
    if (!p)
        return;
    pthread_mutex_lock(&host_lock);
    hashmap_put_int(&host_ptrs, (long long)(uintptr_t)p, (void *)1);
    pthread_mutex_unlock(&host_lock);
}

static bool host_ptr_known(const void *p) {
    pthread_mutex_lock(&host_lock);
    bool hit = hashmap_get_int(&host_ptrs, (long long)(uintptr_t)p) != NULL;
    pthread_mutex_unlock(&host_lock);
    return hit;
}

bool cccc_comptime_host_ptr_take(void *p) {
    pthread_mutex_lock(&host_lock);
    bool hit = hashmap_get_int(&host_ptrs, (long long)(uintptr_t)p) != NULL;
    if (hit)
        hashmap_delete_int(&host_ptrs, (long long)(uintptr_t)p);
    pthread_mutex_unlock(&host_lock);
    return hit;
}

// --- Spec evaluation ---------------------------------------------------------

typedef struct {
    VirtualMachine *vm;
    const char     *name;
    long long      *args;
    int             nargs;
} CallCtx;

static int fault(CallCtx *c, int arg, const void *p, size_t n, bool write) {
    char what[96];
    snprintf(what, sizeof what, "call to '%s' argument %d", c->name, arg);
    return cccc_comptime_fault(c->vm, what, p, n, write);
}

static int fault_msg(CallCtx *c, const char *msg) {
    char buf[160];
    snprintf(buf, sizeof buf, "call to '%s': %s", c->name, msg);
    return cccc_comptime_abort(c->vm, buf);
}

static bool constant_size(const char *sym, size_t len, size_t *out) {
    static const struct {
        const char *name;
        size_t      size;
    } consts[] = {
        {"tm", sizeof(struct tm)},           {"ts", sizeof(struct timespec)},
        {"tv", sizeof(struct timeval)},      {"tms", sizeof(struct tms)},
        {"rlimit", sizeof(struct rlimit)},   {"rusage", sizeof(struct rusage)},
        {"regex", sizeof(regex_t)},          {"regmatch", sizeof(regmatch_t)},
        {"sigset", sizeof(sigset_t)},        {"fenv", sizeof(fenv_t)},
        {"fexcept", sizeof(fexcept_t)},      {"mbstate", sizeof(mbstate_t)},
        {"termios", sizeof(struct termios)},
    };
    for (size_t i = 0; i < sizeof(consts) / sizeof(consts[0]); i++)
        if (strlen(consts[i].name) == len &&
            strncmp(consts[i].name, sym, len) == 0) {
            *out = consts[i].size;
            return true;
        }
    return false;
}

// Evaluates a <size> expression at *p. Returns false when it cannot be
// evaluated (unreadable size pointer, overflow); *p advances past it.
static bool eval_size(CallCtx *c, const char **p, size_t *out) {
    size_t total = 1;
    for (;;) {
        size_t term = 0;
        if (**p == '@' && (*p)[1] == '@') {
            int i  = (*p)[2] - '0';
            *p    += 3;
            if (i >= c->nargs ||
                !cccc_comptime_addr_ok(c->vm, (void *)c->args[i],
                                       sizeof(size_t), false))
                return false;
            memcpy(&term, (void *)c->args[i], sizeof term);
        } else if (**p == '@') {
            int i  = (*p)[1] - '0';
            *p    += 2;
            if (i >= c->nargs)
                return false;
            term = (size_t)c->args[i];
        } else if (**p == '$') {
            int i  = (*p)[1] - '0';
            *p    += 2;
            if (i >= c->nargs)
                return false;
            long long len =
                cccc_comptime_cstr_len(c->vm, (void *)c->args[i], SIZE_MAX);
            if (len < 0)
                return false;
            term = (size_t)len + 1;
        } else if (**p == '=') {
            (*p)++;
            if (**p >= '0' && **p <= '9') {
                term = (size_t)strtoull(*p, (char **)p, 10);
            } else {
                const char *start = *p;
                while ((**p >= 'a' && **p <= 'z'))
                    (*p)++;
                if (!constant_size(start, (size_t)(*p - start), &term))
                    return false;
            }
        } else {
            return false;
        }
        if (__builtin_mul_overflow(total, term, &total))
            return false;
        if (**p != '*')
            break;
        (*p)++;
    }
    *out = total;
    return true;
}

static bool wide_str_ok(VirtualMachine *vm, const void *p) {
    const char *b = p;
    for (size_t i = 0; i < ((size_t)1 << 24); i++, b += sizeof(wchar_t)) {
        if (!cccc_comptime_addr_ok(vm, b, sizeof(wchar_t), false))
            return false;
        wchar_t w;
        memcpy(&w, b, sizeof w);
        if (w == 0)
            return true;
    }
    return false;
}

// Argument slots a format consumes: direct variadic arguments, or a guest
// va_list snapshot.
typedef struct {
    CallCtx *c;
    int      next; // next args[] index (direct mode)
    bool     va_mode;
    char    *reg_ptr, *stack_ptr;
    int      reg_count;
    bool     bad;
} Slots;

static bool slot_take(Slots *s, long long *out) {
    if (!s->va_mode) {
        if (s->next >= s->c->nargs)
            return false;
        *out = s->c->args[s->next++];
        return true;
    }
    char *at;
    if (s->reg_count > 0) {
        s->reg_count--;
        s->reg_ptr -= 8;
        at          = s->reg_ptr + 8;
    } else {
        at            = s->stack_ptr;
        s->stack_ptr += 8;
    }
    if (!cccc_comptime_addr_ok(s->c->vm, at, 8, false))
        return false;
    memcpy(out, at, 8);
    return true;
}

static int check_printf(CallCtx *c, Slots *s, const char *fmt) {
    const char *p = fmt;
    long long   v;
    for (; *p; p++) {
        if (*p != '%')
            continue;
        p++;
        if (*p == '%')
            continue;
        while (*p == '-' || *p == '+' || *p == ' ' || *p == '#' || *p == '0' ||
               *p == '\'')
            p++;
        if (*p == '*') {
            slot_take(s, &v);
            p++;
        } else {
            while (*p >= '0' && *p <= '9')
                p++;
        }
        if (*p == '$')
            return fault_msg(c, "positional format arguments are not allowed "
                                "at comptime");
        long prec = -1;
        if (*p == '.') {
            p++;
            if (*p == '*') {
                slot_take(s, &v);
                p++;
            } else {
                prec = 0;
                while (*p >= '0' && *p <= '9')
                    prec = prec * 10 + (*p++ - '0');
            }
        }
        bool wide = false;
        while (*p == 'h' || *p == 'l' || *p == 'L' || *p == 'q' || *p == 'j' ||
               *p == 'z' || *p == 't' || *p == 'H' || *p == 'D') {
            wide |= *p == 'l';
            p++;
        }
        if (!*p)
            break;
        switch (*p) {
            case 'n':
                return fault_msg(c, "%n is not allowed at comptime");
            case 's':
            case 'S': {
                if (!slot_take(s, &v))
                    return 0;
                if (!v)
                    break;
                bool is_wide = *p == 'S' || wide;
                bool ok = is_wide
                              ? wide_str_ok(c->vm, (void *)v)
                              : (cccc_comptime_cstr_len(c->vm, (void *)v,
                                                        SIZE_MAX) >= 0 ||
                                 (prec >= 0 &&
                                  cccc_comptime_addr_ok(c->vm, (void *)v,
                                                        (size_t)prec, false)));
                if (!ok)
                    return fault(c, s->va_mode ? -1 : s->next - 1, (void *)v, 1,
                                 false);
                break;
            }
            default:
                slot_take(s, &v);
        }
    }
    return 0;
}

static int check_scanf(CallCtx *c, Slots *s, const char *fmt) {
    long long v;
    for (const char *p = fmt; *p; p++) {
        if (*p != '%')
            continue;
        p++;
        if (*p == '%')
            continue;
        bool suppress = false;
        if (*p == '*') {
            suppress = true;
            p++;
        }
        size_t width = 0;
        while (*p >= '0' && *p <= '9')
            width = width * 10 + (size_t)(*p++ - '0');
        int  longs = 0, shorts = 0;
        bool big = false;
        for (;; p++) {
            if (*p == 'l')
                longs++;
            else if (*p == 'h')
                shorts++;
            else if (*p == 'L')
                big = true;
            else if (*p == 'j' || *p == 'z' || *p == 't')
                longs = 1;
            else
                break;
        }
        char   conv = *p;
        size_t n;
        switch (conv) {
            case 0:
                return 0;
            case '[':
                if (*++p == '^')
                    p++;
                if (*p == ']')
                    p++;
                while (*p && *p != ']')
                    p++;
                if (!*p)
                    return 0;
                // fallthrough
            case 's':
            case 'c':
                n = width ? width + (conv == 'c' ? 0 : 1) : 1;
                break;
            case 'e':
            case 'f':
            case 'g':
            case 'E':
            case 'G':
            case 'a':
            case 'A':
                n = longs ? 8 : big ? 8 : 4;
                break;
            case 'p':
                n = 8;
                break;
            default: // d i u o x X b B n
                n = longs ? 8 : shorts >= 2 ? 1 : shorts ? 2 : 4;
        }
        if (suppress && conv != 'n')
            continue;
        if (!slot_take(s, &v))
            return 0;
        if (!cccc_comptime_addr_ok(c->vm, (void *)v, n, true))
            return fault(c, s->va_mode ? -1 : s->next - 1, (void *)v, n, true);
    }
    return 0;
}

static int check_format(CallCtx *c, Slots *s, bool scan, const char *fmt) {
    return scan ? check_scanf(c, s, fmt) : check_printf(c, s, fmt);
}

int cccc_comptime_check_args(VirtualMachine *vm, const char *name,
                             long long *args, int nargs) {
    const char *spec = cccc_comptime_arg_spec(name);
    if (!spec)
        return 0;
    size_t len         = strlen(name);
    bool   locale_tail = false, alias = false;
    lookup_spec(name, len, &locale_tail, &alias);
    if (!*spec && !locale_tail)
        return 0;

    CallCtx     c        = {vm, name, args, nargs};
    const char *p        = spec;
    int         i        = 0;
    int         fmt_idx  = -1;
    bool        fmt_scan = false;
    int         ntokens  = 0;

    for (; *p && *p != '>'; i++) {
        while (*p == ' ')
            p++;
        if (!*p || *p == '>')
            break;
        ntokens++;
        char   kind  = *p++;
        size_t n     = 0;
        bool   sized = kind == 'r' || kind == 'w' || kind == 'n';
        if (sized && !eval_size(&c, &p, &n)) {
            if (i < nargs)
                return fault(&c, i, (void *)args[i], 1, kind == 'w');
        }
        bool opt = false, minus1 = false;
        for (;; p++) {
            if (*p == '?')
                opt = true;
            else if (*p == '!')
                minus1 = true;
            else
                break;
        }
        if (kind == '-' || i >= nargs)
            continue;
        void *ptr = (void *)args[i];
        if (minus1 && ptr == (void *)-1)
            continue;
        if (!ptr && opt)
            continue;
        switch (kind) {
            case 'r':
            case 'w':
                if (n && !cccc_comptime_addr_ok(vm, ptr, n, kind == 'w'))
                    return fault(&c, i, ptr, n, kind == 'w');
                break;
            case 's':
            case 'f':
            case 'F':
                if (cccc_comptime_cstr_len(vm, ptr, SIZE_MAX) < 0)
                    return fault(&c, i, ptr, 1, false);
                if (kind != 's') {
                    fmt_idx  = i;
                    fmt_scan = kind == 'F';
                }
                break;
            case 'n':
                if (cccc_comptime_cstr_len(vm, ptr, n) < 0 &&
                    !cccc_comptime_addr_ok(vm, ptr, n, false))
                    return fault(&c, i, ptr, n, false);
                break;
            case 'W':
                if (!wide_str_ok(vm, ptr))
                    return fault(&c, i, ptr, sizeof(wchar_t), false);
                break;
            case 'h':
                if (!host_ptr_known(ptr))
                    return fault(&c, i, ptr, 1, false);
                break;
            case 'c':
            case 'C': {
                long long dl = cccc_comptime_cstr_len(vm, ptr, SIZE_MAX);
                long long sl =
                    i + 1 < nargs
                        ? cccc_comptime_cstr_len(vm, (void *)args[i + 1],
                                                 kind == 'C' && i + 2 < nargs
                                                     ? (size_t)args[i + 2]
                                                     : SIZE_MAX)
                        : 0;
                if (dl < 0)
                    return fault(&c, i, ptr, 1, true);
                if (sl < 0)
                    sl = kind == 'C' && i + 2 < nargs ? args[i + 2] : 0;
                if (!cccc_comptime_addr_ok(vm, ptr, (size_t)(dl + sl + 1),
                                           true))
                    return fault(&c, i, ptr, (size_t)(dl + sl + 1), true);
                break;
            }
            case 'V': {
                struct {
                    char *reg_ptr, *stack_ptr;
                    int   reg_count;
                } *va = ptr;
                if (!cccc_comptime_addr_ok(vm, ptr, 20, false))
                    return fault(&c, i, ptr, 20, false);
                if (fmt_idx >= 0) {
                    Slots s  = {&c,
                                0,
                                true,
                                va->reg_ptr,
                                va->stack_ptr,
                                va->reg_count,
                                false};
                    int   rc = check_format(&c, &s, fmt_scan,
                                            (const char *)args[fmt_idx]);
                    if (rc)
                        return rc;
                }
                fmt_idx = -1;
                break;
            }
        }
    }

    if (locale_tail && nargs > 0) {
        void *loc = (void *)args[nargs - 1];
        if (loc != (void *)-1 && !host_ptr_known(loc))
            return fault(&c, nargs - 1, loc, 1, false);
    }

    // Direct variadic arguments follow the format string.
    if (fmt_idx >= 0) {
        Slots s = {&c, ntokens, false, NULL, NULL, 0, false};
        return check_format(&c, &s, fmt_scan, (const char *)args[fmt_idx]);
    }
    return 0;
}

// --- Results
// -------------------------------------------------------------------

static void *vm_copy(VirtualMachine *vm, const void *src, size_t n) {
    void *copy = cccc_vm_heap_malloc(vm, (long long)n);
    if (copy)
        memcpy(copy, src, n);
    return copy;
}

void cccc_comptime_after_call(VirtualMachine *vm, const char *name,
                              long long *args, int nargs, long long *result) {
    const char *spec = cccc_comptime_arg_spec(name);
    const char *res  = spec ? strchr(spec, '>') : NULL;

    if (res) {
        res++;
        while (*res == ' ')
            res++;
        void *p = (void *)*result;
        switch (*res) {
            case 'S':
            case 'D':
                if (p && !cccc_comptime_addr_ok(vm, p, 1, false)) {
                    void *copy = vm_copy(vm, p, strlen(p) + 1);
                    if (*res == 'D')
                        free(p);
                    *result = (long long)copy;
                }
                break;
            case 'B':
                if (p && !cccc_comptime_addr_ok(vm, p, 1, false)) {
                    CallCtx     c = {vm, name, args, nargs};
                    const char *q = res + 1;
                    size_t      n = 0;
                    if (eval_size(&c, &q, &n))
                        *result = (long long)vm_copy(vm, p, n);
                }
                break;
            case 'h':
                if (p && !cccc_comptime_addr_ok(vm, p, 1, false))
                    cccc_comptime_host_ptr_add(p);
                break;
            case 'A': {
                char **out = nargs > 0 ? (char **)args[0] : NULL;
                if (out && *result >= 0 &&
                    cccc_comptime_addr_ok(vm, out, sizeof *out, true) && *out &&
                    !cccc_comptime_addr_ok(vm, *out, 1, false)) {
                    char *host = *out;
                    *out       = vm_copy(vm, host, strlen(host) + 1);
                    free(host);
                }
                break;
            }
        }
        return;
    }

    for (size_t i = 0;
         i < sizeof(HOST_PTR_RESULTS) / sizeof(HOST_PTR_RESULTS[0]); i++)
        if (strcmp(name, HOST_PTR_RESULTS[i]) == 0) {
            void *p = (void *)*result;
            if (p && !cccc_comptime_addr_ok(vm, p, 1, false))
                cccc_comptime_host_ptr_add(p);
            return;
        }

    // getline/getdelim allocate the buffer they store through argument 0.
    if ((strcmp(name, "getline") == 0 || strcmp(name, "getdelim") == 0) &&
        nargs > 0 &&
        cccc_comptime_addr_ok(vm, (void *)args[0], sizeof(char *), false)) {
        void *buf = *(void **)args[0];
        if (buf && !cccc_comptime_addr_ok(vm, buf, 1, false))
            cccc_comptime_host_ptr_add(buf);
    }
}
