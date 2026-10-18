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
#include <dirent.h>
#include <fts.h>
#include <glob.h>
#include <grp.h>
#include <locale.h>
#include <net/if.h>
#include <netdb.h>
#include <pthread.h>
#include <pwd.h>
#include <regex.h>
#include <search.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/times.h>
#include <termios.h>
#include <time.h>
#include <wchar.h>
#include <wordexp.h>

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
    {"getdelim", "w=8 w=8 - h"},
    {"getline", "w=8 w=8 h"},
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

// Handles libc returns that comptime code passes back to it.
static const char *const HOST_PTR_RESULTS[] = {
    "fdopen",         "fdopendir", "fopen", "freopen",
    "open_memstream", "opendir",   "popen", "tmpfile",
};

static HashMap        specs;
static pthread_once_t specs_once = PTHREAD_ONCE_INIT;

// --- Per-VM registries -----------------------------------------------------
// Compiler handles and host pointers a VM has handed to comptime code. One
// lock covers both maps; the VM frees them in cc_destroy.

typedef struct {
    HashMap         handles;
    HashMap         host_ptrs;
    pthread_mutex_t lock;
} Registry;

void cccc_comptime_registry_init(VirtualMachine *vm) {
    Registry *r = calloc(1, sizeof *r);
    if (!r)
        error("could not allocate the comptime registry");
    pthread_mutex_init(&r->lock, NULL);
    vm->comptime_registry = r;
}

void cccc_comptime_registry_free(VirtualMachine *vm) {
    Registry *r = vm->comptime_registry;
    if (!r)
        return;
    hashmap_deinit(&r->handles);
    hashmap_deinit(&r->host_ptrs);
    pthread_mutex_destroy(&r->lock);
    free(r);
    vm->comptime_registry = NULL;
}

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

void cccc_comptime_host_ptr_add(VirtualMachine *vm, void *p) {
    Registry *r = vm->comptime_registry;
    if (!p || !r)
        return;
    pthread_mutex_lock(&r->lock);
    hashmap_put_int(&r->host_ptrs, (long long)(uintptr_t)p, (void *)1);
    pthread_mutex_unlock(&r->lock);
}

static bool host_ptr_known(VirtualMachine *vm, const void *p) {
    Registry *r = vm->comptime_registry;
    if (!r)
        return false;
    pthread_mutex_lock(&r->lock);
    bool hit = hashmap_get_int(&r->host_ptrs, (long long)(uintptr_t)p) != NULL;
    pthread_mutex_unlock(&r->lock);
    return hit;
}

bool cccc_comptime_host_ptr_take(VirtualMachine *vm, void *p) {
    Registry *r = vm->comptime_registry;
    if (!r)
        return false;
    pthread_mutex_lock(&r->lock);
    bool hit = hashmap_get_int(&r->host_ptrs, (long long)(uintptr_t)p) != NULL;
    if (hit)
        hashmap_delete_int(&r->host_ptrs, (long long)(uintptr_t)p);
    pthread_mutex_unlock(&r->lock);
    return hit;
}

// --- Compiler handles ----------------------------------------------------
// Node *, Type *, ... are host pointers comptime code holds as integers. The
// compiler records each one it hands out; a builtin rejects any other value.

static const struct {
    const char *tag;
    int         kind;
} HANDLE_TAGS[] = {
    {"Node", CC_H_NODE},
    {"Type", CC_H_TYPE},
    {"Member", CC_H_MEMBER},
    {"Obj", CC_H_OBJ},
    {"Token", CC_H_TOKEN},
    {"EnumConstant", CC_H_ENUM_CONSTANT},
    {"AttrTarget", CC_H_ATTR_TARGET},
};

static const char *handle_kind_name(int kind) {
    for (size_t i = 0; i < sizeof HANDLE_TAGS / sizeof HANDLE_TAGS[0]; i++)
        if (HANDLE_TAGS[i].kind == kind)
            return HANDLE_TAGS[i].tag;
    return "?";
}

void cccc_comptime_handle_add(VirtualMachine *vm, const void *p, int kind) {
    Registry *r = vm->comptime_registry;
    if (!p || !r || kind == CC_H_NONE)
        return;
    pthread_mutex_lock(&r->lock);
    hashmap_put_int(&r->handles, (long long)(uintptr_t)p,
                    (void *)(uintptr_t)kind);
    pthread_mutex_unlock(&r->lock);
}

bool cccc_comptime_handle_ok(VirtualMachine *vm, const void *p, int kind) {
    Registry *r = vm->comptime_registry;
    if (!p)
        return true;
    if (!r)
        return false;
    pthread_mutex_lock(&r->lock);
    bool hit = (int)(uintptr_t)hashmap_get_int(&r->handles,
                                               (long long)(uintptr_t)p) == kind;
    pthread_mutex_unlock(&r->lock);
    return hit;
}

void cccc_comptime_handle_add_nodes(VirtualMachine *vm, Node *first) {
    for (Node *n = first; n; n = n->next)
        cccc_comptime_handle_add(vm, n, CC_H_NODE);
}

// The handle kind a pointer parameter or result type declares, or CC_H_NONE.
static int handle_kind_of_type(const Type *ty) {
    if (!ty || ty->kind != TY_PTR || !ty->base)
        return CC_H_NONE;
    int array = 0;
    if (ty->base->kind == TY_PTR) {
        ty    = ty->base;
        array = CC_H_ARRAY;
    }
    const Type *pointee = ty->base;
    if (!pointee || (pointee->kind != TY_STRUCT && pointee->kind != TY_UNION))
        return CC_H_NONE;
    const Token *tag =
        pointee->struct_tag ? pointee->struct_tag : pointee->name;
    if (!tag)
        return CC_H_NONE;
    const char *text = tag->loc;
    int         len  = tag->len;
    if (len > 7 && strncmp(text, "__cccc_", 7) == 0) {
        text += 7;
        len  -= 7;
    }
    for (size_t i = 0; i < sizeof HANDLE_TAGS / sizeof HANDLE_TAGS[0]; i++)
        if ((int)strlen(HANDLE_TAGS[i].tag) == len &&
            strncmp(text, HANDLE_TAGS[i].tag, (size_t)len) == 0)
            return HANDLE_TAGS[i].kind | array;
    return CC_H_NONE;
}

static CcBuiltin *builtin_by_name(const char *name) {
    for (CcBuiltin *b = cc_builtins; b; b = b->next)
        if (strcmp(b->name, name) == 0)
            return b;
    return NULL;
}

void cccc_builtin_note_signature(const char *name, const Type *fn_ty) {
    if (!name || !fn_ty || strncmp(name, "__builtin_", 10) != 0)
        return;
    CcBuiltin *b = builtin_by_name(name);
    if (!b || b->sig_ready)
        return;
    b->ret_kind = (uint8_t)handle_kind_of_type(fn_ty->return_ty);
    int i       = 0;
    for (const Type *p = fn_ty->params; p && i < CC_BUILTIN_MAX_PARAMS;
         p             = p->next, i++) {
        b->arg_kind[i]  = (uint8_t)handle_kind_of_type(p);
        b->arg_count[i] = -1;
    }
    // An array's length is the first integer parameter after it.
    int j = 0;
    for (const Type *p = fn_ty->params; p && j < CC_BUILTIN_MAX_PARAMS;
         p             = p->next, j++) {
        if (!(b->arg_kind[j] & CC_H_ARRAY))
            continue;
        int k = j + 1;
        for (const Type *q = p->next; q && k < CC_BUILTIN_MAX_PARAMS;
             q             = q->next, k++)
            if (is_integer((Type *)q)) {
                b->arg_count[j] = (int8_t)k;
                break;
            }
    }
    b->sig_ready = true;
}

static int forged_handle(VirtualMachine *vm, const CcBuiltin *b, int arg,
                         int kind) {
    char msg[160];
    snprintf(msg, sizeof msg, "call to '%s' argument %d: not a %s handle",
             b->name, arg, handle_kind_name(kind));
    return cccc_comptime_abort(vm, msg);
}

int cccc_comptime_check_builtin(VirtualMachine *vm, const CcBuiltin *b,
                                long long *args, int nargs) {
    if (!b->sig_ready)
        return 0;
    // TODO(#1678): arguments in the variadic tail are not checked.
    for (int i = 0; i < nargs && i < CC_BUILTIN_MAX_PARAMS; i++) {
        int kind = b->arg_kind[i];
        if (kind == CC_H_NONE)
            continue;
        if (!(kind & CC_H_ARRAY)) {
            if (!cccc_comptime_handle_ok(vm, (void *)args[i], kind))
                return forged_handle(vm, b, i, kind);
            continue;
        }
        kind       &= CC_H_KIND_MASK;
        Node **arr  = (Node **)args[i];
        int    ci   = b->arg_count[i];
        if (!arr || ci < 0 || ci >= nargs)
            continue;
        long long count = args[ci];
        if (count <= 0)
            continue;
        if ((size_t)count > SIZE_MAX / sizeof *arr ||
            !cccc_comptime_addr_ok(vm, arr, (size_t)count * sizeof *arr, false))
            return cccc_comptime_fault(vm, b->name, arr, 1, false);
        for (long long e = 0; e < count; e++)
            if (!cccc_comptime_handle_ok(vm, arr[e], kind))
                return forged_handle(vm, b, i, kind);
    }
    return 0;
}

void cccc_comptime_note_builtin_result(VirtualMachine *vm, const CcBuiltin *b,
                                       long long result) {
    if (!result || !b->sig_ready)
        return;
    int kind = b->ret_kind;
    if (kind & CC_H_ARRAY)
        return; // the elements are noted where the VM copy is made
    cccc_comptime_handle_add(vm, (void *)result, kind);
}

// --- Host memory ranges ------------------------------------------------------
// Memory libc keeps owning (glob paths, directory entries, search nodes) stays
// in host memory; comptime reads it through disjoint ranges that the matching
// release call (globfree, closedir, ...) drops by owner.

typedef struct {
    uintptr_t   base;
    size_t      len;
    const void *owner;
    bool        writable;
} HostRange;

// TODO(#1676): sorted array with O(n) insert and owner scans; use an interval
// tree if results with many thousands of entries show up.
static HostRange      *host_ranges;
static size_t          host_range_count, host_range_cap;
static pthread_mutex_t range_lock = PTHREAD_MUTEX_INITIALIZER;

// Index of the first range whose end is above `a`.
static size_t range_lower_bound(uintptr_t a) {
    size_t lo = 0, hi = host_range_count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (host_ranges[mid].base + host_ranges[mid].len <= a)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo;
}

void cccc_comptime_host_range_add(const void *base, size_t len,
                                  const void *owner, bool writable) {
    if (!base || !len)
        return;
    uintptr_t a = (uintptr_t)base;
    pthread_mutex_lock(&range_lock);
    // A new range replaces whatever it overlaps (a reused dirent buffer).
    size_t i = range_lower_bound(a);
    size_t j = i;
    while (j < host_range_count && host_ranges[j].base < a + len)
        j++;
    if (j > i) {
        memmove(&host_ranges[i], &host_ranges[j],
                (host_range_count - j) * sizeof *host_ranges);
        host_range_count -= j - i;
    }
    if (host_range_count == host_range_cap) {
        size_t     cap = host_range_cap ? host_range_cap * 2 : 64;
        HostRange *n   = realloc(host_ranges, cap * sizeof *n);
        if (!n) {
            pthread_mutex_unlock(&range_lock);
            return;
        }
        host_ranges    = n;
        host_range_cap = cap;
    }
    memmove(&host_ranges[i + 1], &host_ranges[i],
            (host_range_count - i) * sizeof *host_ranges);
    host_ranges[i] = (HostRange){a, len, owner, writable};
    host_range_count++;
    pthread_mutex_unlock(&range_lock);
}

void cccc_comptime_host_range_drop(const void *owner) {
    if (!owner)
        return;
    pthread_mutex_lock(&range_lock);
    size_t keep = 0;
    for (size_t i = 0; i < host_range_count; i++)
        if (host_ranges[i].owner != owner)
            host_ranges[keep++] = host_ranges[i];
    host_range_count = keep;
    pthread_mutex_unlock(&range_lock);
}

size_t cccc_comptime_host_range_left(const void *p, bool write) {
    uintptr_t a    = (uintptr_t)p;
    size_t    left = 0;
    pthread_mutex_lock(&range_lock);
    size_t i = range_lower_bound(a);
    if (i < host_range_count && host_ranges[i].base <= a &&
        (!write || host_ranges[i].writable))
        left = host_ranges[i].base + host_ranges[i].len - a;
    pthread_mutex_unlock(&range_lock);
    return left;
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
                if (!host_ptr_known(vm, ptr))
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
        if (loc != (void *)-1 && !host_ptr_known(vm, loc))
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

// --- Results of granted-class calls ------------------------------------------
// By lifetime: libc's static buffers are copied (the next call may free them),
// caller-owned allocations are copied and freed, and memory libc keeps owning
// becomes a host range dropped by its release call.

typedef void (*ResultFn)(VirtualMachine *vm, long long *args, int nargs,
                         long long *result);

static bool is_host(VirtualMachine *vm, const void *p) {
    return p && !cccc_comptime_in_vm(vm, p);
}

static char *dup_str(VirtualMachine *vm, const char *s) {
    return is_host(vm, s) ? vm_copy(vm, s, strlen(s) + 1) : (char *)s;
}

// NULL-terminated vector of host strings, or of `elem_len`-byte blobs.
static char **dup_vec(VirtualMachine *vm, char **v, size_t elem_len) {
    if (!is_host(vm, v))
        return v;
    size_t n = 0;
    while (v[n])
        n++;
    char **copy = vm_copy(vm, v, (n + 1) * sizeof *v);
    for (size_t i = 0; copy && i < n; i++)
        copy[i] = elem_len ? vm_copy(vm, v[i], elem_len) : dup_str(vm, v[i]);
    return copy;
}

static void r_string(VirtualMachine *vm, long long *args, int nargs,
                     long long *result) {
    (void)args, (void)nargs;
    *result = (long long)dup_str(vm, (char *)*result);
}

static void r_string_free(VirtualMachine *vm, long long *args, int nargs,
                          long long *result) {
    (void)args, (void)nargs;
    char *host = (char *)*result;
    if (!is_host(vm, host))
        return;
    char *copy = dup_str(vm, host);
    if (copy) {
        free(host);
        *result = (long long)copy;
    }
}

static void r_passwd(VirtualMachine *vm, long long *args, int nargs,
                     long long *result) {
    (void)args, (void)nargs;
    struct passwd *src = (struct passwd *)*result;
    if (!is_host(vm, src))
        return;
    struct passwd *c = vm_copy(vm, src, sizeof *src);
    if (!c)
        return;
    c->pw_name   = dup_str(vm, src->pw_name);
    c->pw_passwd = dup_str(vm, src->pw_passwd);
    c->pw_dir    = dup_str(vm, src->pw_dir);
    c->pw_shell  = dup_str(vm, src->pw_shell);
#ifndef __ANDROID__
    c->pw_gecos = dup_str(vm, src->pw_gecos);
#endif
#ifdef __APPLE__
    c->pw_class = dup_str(vm, src->pw_class);
#endif
    *result = (long long)c;
}

static void r_group(VirtualMachine *vm, long long *args, int nargs,
                    long long *result) {
    (void)args, (void)nargs;
    struct group *src = (struct group *)*result;
    if (!is_host(vm, src))
        return;
    struct group *c = vm_copy(vm, src, sizeof *src);
    if (!c)
        return;
    c->gr_name   = dup_str(vm, src->gr_name);
    c->gr_passwd = dup_str(vm, src->gr_passwd);
    c->gr_mem    = dup_vec(vm, src->gr_mem, 0);
    *result      = (long long)c;
}

static void r_lconv(VirtualMachine *vm, long long *args, int nargs,
                    long long *result) {
    (void)args, (void)nargs;
    struct lconv *src = (struct lconv *)*result;
    if (!is_host(vm, src))
        return;
    struct lconv *c = vm_copy(vm, src, sizeof *src);
    if (!c)
        return;
    char **fields[] = {
        &c->decimal_point,     &c->thousands_sep,   &c->grouping,
        &c->int_curr_symbol,   &c->currency_symbol, &c->mon_decimal_point,
        &c->mon_thousands_sep, &c->mon_grouping,    &c->positive_sign,
        &c->negative_sign,
    };
    for (size_t i = 0; i < sizeof(fields) / sizeof(fields[0]); i++)
        *fields[i] = dup_str(vm, *fields[i]);
    *result = (long long)c;
}

static void r_hostent(VirtualMachine *vm, long long *args, int nargs,
                      long long *result) {
    (void)args, (void)nargs;
    struct hostent *src = (struct hostent *)*result;
    if (!is_host(vm, src))
        return;
    struct hostent *c = vm_copy(vm, src, sizeof *src);
    if (!c)
        return;
    c->h_name      = dup_str(vm, src->h_name);
    c->h_aliases   = dup_vec(vm, src->h_aliases, 0);
    c->h_addr_list = dup_vec(vm, src->h_addr_list, (size_t)src->h_length);
    *result        = (long long)c;
}

static void r_netent(VirtualMachine *vm, long long *args, int nargs,
                     long long *result) {
    (void)args, (void)nargs;
    struct netent *src = (struct netent *)*result;
    if (!is_host(vm, src))
        return;
    struct netent *c = vm_copy(vm, src, sizeof *src);
    if (!c)
        return;
    c->n_name    = dup_str(vm, src->n_name);
    c->n_aliases = dup_vec(vm, src->n_aliases, 0);
    *result      = (long long)c;
}

static void r_servent(VirtualMachine *vm, long long *args, int nargs,
                      long long *result) {
    (void)args, (void)nargs;
    struct servent *src = (struct servent *)*result;
    if (!is_host(vm, src))
        return;
    struct servent *c = vm_copy(vm, src, sizeof *src);
    if (!c)
        return;
    c->s_name    = dup_str(vm, src->s_name);
    c->s_aliases = dup_vec(vm, src->s_aliases, 0);
    c->s_proto   = dup_str(vm, src->s_proto);
    *result      = (long long)c;
}

static void r_protoent(VirtualMachine *vm, long long *args, int nargs,
                       long long *result) {
    (void)args, (void)nargs;
    struct protoent *src = (struct protoent *)*result;
    if (!is_host(vm, src))
        return;
    struct protoent *c = vm_copy(vm, src, sizeof *src);
    if (!c)
        return;
    c->p_name    = dup_str(vm, src->p_name);
    c->p_aliases = dup_vec(vm, src->p_aliases, 0);
    *result      = (long long)c;
}

// `char ***` to the environment: the guest gets a snapshot of the vector.
static void r_environ(VirtualMachine *vm, long long *args, int nargs,
                      long long *result) {
    (void)args, (void)nargs;
    char ***src = (char ***)*result;
    if (!is_host(vm, src))
        return;
    char ***cell = vm_copy(vm, src, sizeof *src);
    if (cell)
        *cell = dup_vec(vm, *src, 0);
    *result = (long long)cell;
}

static size_t dirent_len(const struct dirent *e) {
    return offsetof(struct dirent, d_name) + strlen(e->d_name) + 1;
}

void cccc_comptime_host_dirent(const void *e, const void *owner) {
    if (e)
        cccc_comptime_host_range_add(e, dirent_len(e), owner, false);
}

void cccc_comptime_host_ftsent(const void *p, const void *owner) {
    const FTSENT *e = p;
    if (e)
        cccc_comptime_host_range_add(e, sizeof *e + e->fts_namelen + 1, owner,
                                     false);
}

void cccc_comptime_host_string(const void *s, const void *owner) {
    if (s)
        cccc_comptime_host_range_add(s, strlen(s) + 1, owner, false);
}

static void host_strv(char *const *v, size_t first, size_t count,
                      const void *owner) {
    if (!v)
        return;
    cccc_comptime_host_range_add(v, (first + count + 1) * sizeof *v, owner,
                                 false);
    for (size_t i = first; i < first + count; i++)
        cccc_comptime_host_string(v[i], owner);
}

static void r_scandir(VirtualMachine *vm, long long *args, int nargs,
                      long long *result) {
    int n = (int)*result;
    if (n < 0 || nargs < 2)
        return;
    struct dirent ***out = (struct dirent ***)args[1];
    if (!cccc_comptime_addr_ok(vm, out, sizeof *out, true) ||
        !is_host(vm, *out))
        return;
    struct dirent **host = *out;
    struct dirent **copy = cccc_vm_heap_malloc(
        vm, (long long)((n ? (size_t)n : 1) * sizeof *copy));
    if (!copy)
        return;
    for (int i = 0; i < n; i++) {
        copy[i] = vm_copy(vm, host[i], dirent_len(host[i]));
        free(host[i]);
    }
    free(host);
    *out = copy;
}

static void r_glob(VirtualMachine *vm, long long *args, int nargs,
                   long long *result) {
    if (*result != 0 || nargs < 4 ||
        !cccc_comptime_addr_ok(vm, (void *)args[3], sizeof(glob_t), false))
        return;
    glob_t *g = (glob_t *)args[3];
    cccc_comptime_host_range_drop(g);
    host_strv(g->gl_pathv, g->gl_offs, g->gl_pathc, g);
}

static void r_release(VirtualMachine *vm, long long *args, int nargs,
                      long long *result) {
    (void)vm, (void)result;
    if (nargs > 0)
        cccc_comptime_host_range_drop((void *)args[0]);
}

static void r_release_arg1(VirtualMachine *vm, long long *args, int nargs,
                           long long *result) {
    (void)vm, (void)result;
    if (nargs > 1)
        cccc_comptime_host_range_drop((void *)args[1]);
}

static void r_readdir(VirtualMachine *vm, long long *args, int nargs,
                      long long *result) {
    if (nargs > 0 && is_host(vm, (void *)*result))
        cccc_comptime_host_dirent((void *)*result, (void *)args[0]);
}

// An FTSENT stays readable until the next fts_read: libc reallocates the path
// buffer its entries share.
static void r_fts_read(VirtualMachine *vm, long long *args, int nargs,
                       long long *result) {
    FTSENT *e = (FTSENT *)*result;
    if (nargs < 1 || !is_host(vm, e))
        return;
    const void *owner = (void *)args[0];
    cccc_comptime_host_range_drop(owner);
    for (int depth = 0; e && depth < 4096; e = e->fts_parent, depth++) {
        cccc_comptime_host_ftsent(e, owner);
        if (depth == 0) {
            cccc_comptime_host_string(e->fts_path, owner);
            cccc_comptime_host_string(e->fts_accpath, owner);
        }
    }
}

static void r_fts_children(VirtualMachine *vm, long long *args, int nargs,
                           long long *result) {
    if (nargs < 1)
        return;
    for (FTSENT *e = (FTSENT *)*result; is_host(vm, e); e = e->fts_link)
        cccc_comptime_host_ftsent(e, (void *)args[0]);
}

static void r_getaddrinfo(VirtualMachine *vm, long long *args, int nargs,
                          long long *result) {
    if (*result != 0 || nargs < 4 ||
        !cccc_comptime_addr_ok(vm, (void *)args[3], sizeof(void *), false))
        return;
    struct addrinfo *head = *(struct addrinfo **)args[3];
    for (struct addrinfo *a = head; is_host(vm, a); a = a->ai_next) {
        cccc_comptime_host_range_add(a, sizeof *a, head, false);
        cccc_comptime_host_range_add(a->ai_addr, a->ai_addrlen, head, false);
        cccc_comptime_host_string(a->ai_canonname, head);
    }
}

static void r_nameindex(VirtualMachine *vm, long long *args, int nargs,
                        long long *result) {
    (void)args, (void)nargs;
    struct if_nameindex *v = (struct if_nameindex *)*result;
    if (!is_host(vm, v))
        return;
    size_t n = 0;
    while (v[n].if_index || v[n].if_name)
        n++;
    cccc_comptime_host_range_add(v, (n + 1) * sizeof *v, v, false);
    for (size_t i = 0; i < n; i++)
        cccc_comptime_host_string(v[i].if_name, v);
}

static void r_wordexp(VirtualMachine *vm, long long *args, int nargs,
                      long long *result) {
    if (*result != 0 || nargs < 2 ||
        !cccc_comptime_addr_ok(vm, (void *)args[1], sizeof(wordexp_t), false))
        return;
    wordexp_t *we = (wordexp_t *)args[1];
    cccc_comptime_host_range_drop(we);
    host_strv(we->we_wordv, we->we_offs, we->we_wordc, we);
}

// A tsearch/tfind node is readable until the tree is next modified by tdelete.
static void r_tnode(VirtualMachine *vm, long long *args, int nargs,
                    long long *result) {
    if (nargs > 1 && is_host(vm, (void *)*result))
        cccc_comptime_host_range_add((void *)*result, sizeof(void *),
                                     (void *)args[1], false);
}

static char hsearch_owner;

static void r_hsearch(VirtualMachine *vm, long long *args, int nargs,
                      long long *result) {
    (void)args, (void)nargs;
    if (is_host(vm, (void *)*result))
        cccc_comptime_host_range_add((void *)*result, sizeof(ENTRY),
                                     &hsearch_owner, true);
}

static void r_hdestroy(VirtualMachine *vm, long long *args, int nargs,
                       long long *result) {
    (void)vm, (void)args, (void)nargs, (void)result;
    cccc_comptime_host_range_drop(&hsearch_owner);
}

static void r_mmap(VirtualMachine *vm, long long *args, int nargs,
                   long long *result) {
    void *p = (void *)*result;
    if (nargs < 3 || !is_host(vm, p) || p == MAP_FAILED)
        return;
    cccc_comptime_host_range_add(p, (size_t)args[1], p,
                                 (args[2] & PROT_WRITE) != 0);
}

static const struct {
    const char *name;
    ResultFn    fn;
} RESULTS[] = {
    {"__cccc_environ_ptr", r_environ},
    {"catgets", r_string},
    {"closedir", r_release},
    {"fts_children", r_fts_children},
    {"fts_close", r_release},
    {"fts_read", r_fts_read},
    {"freeaddrinfo", r_release},
    {"getaddrinfo", r_getaddrinfo},
    {"getcwd", r_string_free},
    {"getenv", r_string},
    {"getgrgid", r_group},
    {"getgrnam", r_group},
    {"gethostbyaddr", r_hostent},
    {"gethostbyname", r_hostent},
    {"getlogin", r_string},
    {"getnetbyaddr", r_netent},
    {"getnetbyname", r_netent},
    {"getprotobyname", r_protoent},
    {"getprotobynumber", r_protoent},
    {"getpwnam", r_passwd},
    {"getpwuid", r_passwd},
    {"getservbyname", r_servent},
    {"getservbyport", r_servent},
    {"glob", r_glob},
    {"globfree", r_release},
    {"hdestroy", r_hdestroy},
    {"hsearch", r_hsearch},
    {"if_freenameindex", r_release},
    {"if_nameindex", r_nameindex},
    {"localeconv", r_lconv},
    {"mmap", r_mmap},
    {"munmap", r_release},
    {"readdir", r_readdir},
    {"realpath", r_string_free},
    {"scandir", r_scandir},
    {"tdelete", r_release_arg1},
    {"tfind", r_tnode},
    {"tmpnam", r_string},
    {"tsearch", r_tnode},
    {"ttyname", r_string},
    {"wordexp", r_wordexp},
    {"wordfree", r_release},
};

static HashMap        results;
static pthread_once_t results_once = PTHREAD_ONCE_INIT;

static void build_results(void) {
    for (size_t i = 0; i < sizeof(RESULTS) / sizeof(RESULTS[0]); i++)
        hashmap_put(&results, RESULTS[i].name, (void *)RESULTS[i].fn);
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
                if (p && !cccc_comptime_in_vm(vm, p)) {
                    void *copy = vm_copy(vm, p, strlen(p) + 1);
                    if (*res == 'D')
                        free(p);
                    *result = (long long)copy;
                }
                break;
            case 'B':
                if (p && !cccc_comptime_in_vm(vm, p)) {
                    CallCtx     c = {vm, name, args, nargs};
                    const char *q = res + 1;
                    size_t      n = 0;
                    if (eval_size(&c, &q, &n))
                        *result = (long long)vm_copy(vm, p, n);
                }
                break;
            case 'h':
                if (p && !cccc_comptime_in_vm(vm, p))
                    cccc_comptime_host_ptr_add(vm, p);
                break;
            case 'A': {
                char **out = nargs > 0 ? (char **)args[0] : NULL;
                if (out && *result >= 0 &&
                    cccc_comptime_addr_ok(vm, out, sizeof *out, true) && *out &&
                    !cccc_comptime_in_vm(vm, *out)) {
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
            if (p && !cccc_comptime_in_vm(vm, p))
                cccc_comptime_host_ptr_add(vm, p);
            return;
        }

    pthread_once(&results_once, build_results);
    ResultFn fn = hashmap_get(&results, name);
    if (fn)
        fn(vm, args, nargs, result);
}
