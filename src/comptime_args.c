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
//   m        writable NUL-terminated string (strtok splits it in place)
//   h        opaque host handle      c / C    strcat / strncat destination
//   x        guest function pointer, not NULL
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
// A token ending in [inner;@off inner...] also checks the pointers stored in
// the buffer it just validated: each entry reads the pointer at byte offset
// `off` (default 0) and checks it with a leaf token (r w s m n W h x). `^i`
// before the bracket applies it only when argument i is NULL. Walks that need a
// loop or state (argv, longopts, tree roots, regex_t) are PRECHECKS below.
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
#include <getopt.h>

static const struct {
    const char *name;
    const char *spec;
} SPECS[] = {
    {"_Exit", ""},
    {"_exit", ""},
    {"__fdelt_chk", "-"},
    {"__fdelt_warn", "-"},
    {"__memcpy_chk", "w@2 r@2 - -"},
    {"__memmove_chk", "w@2 r@2 - -"},
    {"__mempcpy_chk", "w@2 r@2 - -"},
    {"__memset_chk", "w@2 - - -"},
    {"__memccpy_chk", "w@3 r@3 - - -"},
    {"__strcpy_chk", "w$1 s -"},
    {"__stpcpy_chk", "w$1 s -"},
    {"__strncpy_chk", "w@2 n@2 - -"},
    {"__stpncpy_chk", "w@2 n@2 - -"},
    {"__strlcpy_chk", "w@2 s - -"},
    {"__strlcat_chk", "w@2 s - -"},
    {"__strcat_chk", "c s -"},
    {"__strncat_chk", "C n@2 - -"},
    {"__snprintf_chk", "w@1? - - - f"},
    {"__sprintf_chk", "w=1 - - f"},
    {"__printf_chk", "- f"},
    {"__fprintf_chk", "h - f"},
    {"__vsnprintf_chk", "w@1? - - - f V"},
    {"__vsprintf_chk", "w=1 - - f V"},
    {"__vprintf_chk", "- f V"},
    {"__vfprintf_chk", "h - f V"},
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
    {"bsearch", "r@3 r@2*@3 - - x"},
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
    {"getopt", "- w@0*=8 s"},
    {"getopt_long", "- w@0*=8 s - w=4?"},
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
    {"hsearch", "r=16[s] -"},
    {"htonl", ""},
    {"htons", ""},
    {"iconv", "h w=8?[r@@2?] w=8? w=8?[w@@4?] w=8?"},
    {"iconv_close", "h"},
    {"iconv_open", "s s > h"},
    {"index", "s -"},
    {"inet_addr", "s"},
    {"inet_ntoa", "> S"},
    {"inet_ntop", "- r=4 w@3 -"},
    {"inet_pton", "- s w=4"},
    {"insque", "w=16 w=16?[w=16?]"},
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
    {"lfind", "r@3 r@@2*@3 r=8 - x"},
    {"llabs", ""},
    {"lldiv", ""},
    {"localeconv", ""},
    {"localtime", "r=8 > B=tm"},
    {"localtime_r", "r=8 w=tm"},
    {"lsearch", "r@3 w@@2*@3 w=8 - x"},
    {"malloc", ""},
    {"mblen", "r@1? -"},
    {"mbrlen", "r@1? - w=mbstate?"},
    {"mbrtoc16", "w=2? r@2? - w=mbstate?"},
    {"mbrtoc32", "w=4? r@2? - w=mbstate?"},
    {"mbrtoc8", "w=1? r@2? - w=mbstate?"},
    {"mbrtowc", "w=4? r@2? - w=mbstate?"},
    {"mbsinit", "r=mbstate?"},
    {"mbsrtowcs", "w@2*=4? w=8[s] - w=mbstate?"},
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
    {"qsort", "w@1*@2 - - x"},
    {"quick_exit", ""},
    {"rand", ""},
    {"realloc", ""},
    {"reallocarray", ""},
    {"regcomp", "w=regex s -"},
    {"regerror", "- r=regex w@3? -"},
    {"regexec", "r=regex s - w@2*=regmatch? -"},
    {"regfree", "w=regex"},
    {"remque", "w=16[w=16?;@8 w=16?]"},
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
    {"strtok", "m? s"},
#ifdef __APPLE__
    {"strtok_r", "m? s w=8^0[m?]"},
#else
    {"strtok_r", "m? s w=8^0[m]"},
#endif
    {"strtol", "s w=8? -"},
    {"strtold", "s w=8?"},
    {"strtoll", "s w=8? -"},
    {"strtoul", "s w=8? -"},
    {"strtoull", "s w=8? -"},
    {"strxfrm", "w@2? s -"},
    {"sysconf", ""},
    {"tdelete", "- w=8 x"},
    {"tfind", "- w=8 x"},
    {"time", "w=8?"},
    {"timegm", "w=tm"},
    {"times", "w=tms"},
    {"timespec_get", "w=ts -"},
    {"tolower", ""},
    {"toupper", ""},
    {"towctrans", ""},
    {"towlower", ""},
    {"towupper", ""},
    {"tsearch", "- w=8 x"},
    {"twalk", "- x?"},
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
    {"wcsrtombs", "w@2? w=8[W] - w=mbstate?"},
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
    // Compiler builtins whose prototype can't say enough; the rest are derived
    // from reflection.h (cccc_builtin_note_signature).
    {"__builtin_ast_global_var_set_init_data", "- r@2 -"},
    {"__builtin_macro_error_at", "- f"},
    {"__builtin_macro_warning_at", "- f"},
    {"__builtin_quote", "s"},
    {"__builtin_quote_lazy", "s"},
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
    HashMap         regexes; // regex_t address -> RegexSnap
    pthread_mutex_t lock;
} Registry;

// The regex_t bytes regcomp left, so a forged or edited one is refused.
typedef struct {
    regex_t copy;
    bool    live;
} RegexSnap;

static int free_regex_snap(char *key, int keylen, void *val, void *user) {
    (void)key, (void)keylen, (void)user;
    free(val);
    return 0;
}

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
    hashmap_foreach(&r->regexes, free_regex_snap, NULL);
    hashmap_deinit(&r->regexes);
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
    if (kind == CC_H_NONE || !p || !r)
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
        if (ty->base && ty->base->kind == TY_CHAR)
            return CC_H_STR | CC_H_ARRAY;
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

// Spec token for a parameter that is neither a handle nor a string array.
static const char *param_spec_token(const Type *p, int handle_kind) {
    static _Thread_local char buf[16];
    if (handle_kind != CC_H_NONE || p->kind != TY_PTR || !p->base)
        return "-";
    switch (p->base->kind) {
        case TY_CHAR:
            return "s?";
        case TY_FUNC:
        case TY_VOID:
        case TY_PTR:
        case TY_STRUCT:
        case TY_UNION:
            return "-";
        default:
            snprintf(buf, sizeof buf, "%c=%d?", p->base->is_const ? 'r' : 'w',
                     p->base->size);
            return buf;
    }
}

// Only the reflection.h prototype counts, so a user's own declaration of a
// builtin name cannot weaken its checks.
static bool is_reflection_header(const char *file) {
    static const char tail[] = "cccc/reflection.h";
    size_t            len    = file ? strlen(file) : 0;
    return file && (strcmp(file, "<implicit-reflection.h>") == 0 ||
                    (len >= sizeof tail - 1 &&
                     strcmp(file + len - (sizeof tail - 1), tail) == 0));
}

void cccc_builtin_note_signature(const char *name, const Type *fn_ty,
                                 const char *file) {
    if (!name || !fn_ty || strncmp(name, "__builtin_", 10) != 0 ||
        !is_reflection_header(file))
        return;
    CcBuiltin *b = builtin_by_name(name);
    if (!b || b->sig_ready)
        return;
    b->ret_kind = (uint8_t)handle_kind_of_type(fn_ty->return_ty);
    int    i    = 0;
    size_t used = 0;
    for (const Type *p = fn_ty->params; p && i < CC_BUILTIN_MAX_PARAMS;
         p             = p->next, i++) {
        b->arg_kind[i]  = (uint8_t)handle_kind_of_type(p);
        b->arg_count[i] = -1;
        used += (size_t)snprintf(b->arg_spec + used, sizeof b->arg_spec - used,
                                 "%s%s", i ? " " : "",
                                 param_spec_token(p, b->arg_kind[i]));
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

static int run_spec(VirtualMachine *vm, const char *name, const char *spec,
                    long long *args, int nargs, bool locale_tail);

// Strings and buffers by spec (a SPECS row wins over the prototype's), then
// handles by kind.
int cccc_comptime_check_builtin(VirtualMachine *vm, const CcBuiltin *b,
                                long long *args, int nargs) {
    pthread_once(&specs_once, build_specs);
    const char *spec = hashmap_get(&specs, b->name);
    if (!spec)
        spec = b->sig_ready ? b->arg_spec : "";
    int rc = run_spec(vm, b->name, spec, args, nargs, false);
    if (rc || !b->sig_ready)
        return rc;
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
        void **arr   = (void **)args[i];
        int    ci   = b->arg_count[i];
        if (!arr || ci < 0 || ci >= nargs)
            continue;
        long long count = args[ci];
        if (count <= 0)
            continue;
        char what[96];
        snprintf(what, sizeof what, "call to '%s' argument %d", b->name, i);
        if ((size_t)count > SIZE_MAX / sizeof *arr ||
            !cccc_comptime_addr_ok(vm, arr, (size_t)count * sizeof *arr, false))
            return cccc_comptime_fault(vm, what, arr, 1, false);
        for (long long e = 0; e < count; e++) {
            if (kind == CC_H_STR) {
                if (arr[e] && cccc_comptime_cstr_len(vm, arr[e], SIZE_MAX) < 0)
                    return cccc_comptime_fault(vm, what, arr[e], 1, false);
            } else if (!cccc_comptime_handle_ok(vm, arr[e], kind)) {
                return forged_handle(vm, b, i, kind);
            }
        }
    }
    return 0;
}

void cccc_comptime_note_builtin_result(VirtualMachine *vm, const CcBuiltin *b,
                                       long long result) {
    // A void builtin leaves `result` undefined, so test the kind first.
    if (!b->sig_ready || b->ret_kind == CC_H_NONE || !result)
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

typedef struct HostRange HostRange;
struct HostRange {
    uintptr_t   base;
    size_t      len;
    const void *owner;
    bool        writable;
    bool        tree_node; // a tsearch node, owner is the root variable
    bool        seen;      // set by a tdelete walk, cleared by its sweep
    int         height;
    HostRange  *left, *right;            // AVL tree ordered by base
    HostRange  *owner_prev, *owner_next; // ranges sharing an owner
};

// Ranges never overlap, so one AVL tree keyed by base orders them; the owner
// index lets a release call find its ranges without a scan.
static HostRange      *range_root;
static HashMap         range_owners; // owner -> first HostRange of its list
static pthread_mutex_t range_lock = PTHREAD_MUTEX_INITIALIZER;

static int range_height(const HostRange *n) {
    return n ? n->height : 0;
}

static void range_update(HostRange *n) {
    int l     = range_height(n->left);
    int r     = range_height(n->right);
    n->height = 1 + (l > r ? l : r);
}

static HostRange *range_rotate_right(HostRange *y) {
    HostRange *x = y->left;
    y->left      = x->right;
    x->right     = y;
    range_update(y);
    range_update(x);
    return x;
}

static HostRange *range_rotate_left(HostRange *x) {
    HostRange *y = x->right;
    x->right     = y->left;
    y->left      = x;
    range_update(x);
    range_update(y);
    return y;
}

static HostRange *range_balance(HostRange *n) {
    range_update(n);
    int skew = range_height(n->left) - range_height(n->right);
    if (skew > 1) {
        if (range_height(n->left->left) < range_height(n->left->right))
            n->left = range_rotate_left(n->left);
        return range_rotate_right(n);
    }
    if (skew < -1) {
        if (range_height(n->right->right) < range_height(n->right->left))
            n->right = range_rotate_right(n->right);
        return range_rotate_left(n);
    }
    return n;
}

static HostRange *range_tree_insert(HostRange *n, HostRange *r) {
    if (!n)
        return r;
    if (r->base < n->base)
        n->left = range_tree_insert(n->left, r);
    else
        n->right = range_tree_insert(n->right, r);
    return range_balance(n);
}

static HostRange *range_tree_pop_min(HostRange *n, HostRange **min) {
    if (!n->left) {
        *min = n;
        return n->right;
    }
    n->left = range_tree_pop_min(n->left, min);
    return range_balance(n);
}

// Detaches the range at `base` (bases are unique); the caller frees it.
static HostRange *range_tree_erase(HostRange *n, uintptr_t base) {
    if (!n)
        return NULL;
    if (base < n->base) {
        n->left = range_tree_erase(n->left, base);
    } else if (base > n->base) {
        n->right = range_tree_erase(n->right, base);
    } else {
        HostRange *l = n->left, *r = n->right;
        if (!r)
            return l;
        HostRange *min;
        r          = range_tree_pop_min(r, &min);
        min->left  = l;
        min->right = r;
        return range_balance(min);
    }
    return range_balance(n);
}

// First range whose end is above `a`.
static HostRange *range_find(uintptr_t a) {
    HostRange *best = NULL;
    for (HostRange *n = range_root; n;) {
        if (n->base + n->len <= a) {
            n = n->right;
        } else {
            best = n;
            n    = n->left;
        }
    }
    return best;
}

static HostRange *range_successor(const HostRange *r) {
    HostRange *best = NULL;
    for (HostRange *n = range_root; n;) {
        if (n->base > r->base) {
            best = n;
            n    = n->left;
        } else {
            n = n->right;
        }
    }
    return best;
}

static long long owner_key(const void *owner) {
    return (long long)(uintptr_t)owner;
}

// A NULL owner is never dropped, so it is not indexed (0 is no hashmap key).
static void owner_link(HostRange *r) {
    if (!r->owner)
        return;
    HostRange *head = hashmap_get_int(&range_owners, owner_key(r->owner));
    r->owner_prev   = NULL;
    r->owner_next   = head;
    if (head)
        head->owner_prev = r;
    hashmap_put_int(&range_owners, owner_key(r->owner), r);
}

static void owner_unlink(HostRange *r) {
    if (!r->owner)
        return;
    if (r->owner_prev)
        r->owner_prev->owner_next = r->owner_next;
    else if (r->owner_next)
        hashmap_put_int(&range_owners, owner_key(r->owner), r->owner_next);
    else
        hashmap_delete_int(&range_owners, owner_key(r->owner));
    if (r->owner_next)
        r->owner_next->owner_prev = r->owner_prev;
}

static void range_remove(HostRange *r) {
    owner_unlink(r);
    range_root = range_tree_erase(range_root, r->base);
    free(r);
}

// A new range replaces whatever it overlaps (a reused dirent buffer).
static HostRange *range_add_locked(const void *base, size_t len,
                                   const void *owner, bool writable,
                                   bool tree_node) {
    uintptr_t  a = (uintptr_t)base;
    HostRange *old;
    while ((old = range_find(a)) && old->base < a + len)
        range_remove(old);
    HostRange *r = calloc(1, sizeof *r);
    if (!r)
        return NULL;
    r->base      = a;
    r->len       = len;
    r->owner     = owner;
    r->writable  = writable;
    r->tree_node = tree_node;
    r->height    = 1;
    range_root   = range_tree_insert(range_root, r);
    owner_link(r);
    return r;
}

static void range_add(const void *base, size_t len, const void *owner,
                      bool writable, bool tree_node) {
    if (!base || !len)
        return;
    pthread_mutex_lock(&range_lock);
    range_add_locked(base, len, owner, writable, tree_node);
    pthread_mutex_unlock(&range_lock);
}

void cccc_comptime_host_range_add(const void *base, size_t len,
                                  const void *owner, bool writable) {
    range_add(base, len, owner, writable, false);
}

void cccc_comptime_host_node_add(const void *node, const void *tree) {
    range_add(node, sizeof(void *), tree, false, true);
}

bool cccc_comptime_host_node_known(const void *node, const void *tree) {
    uintptr_t a   = (uintptr_t)node;
    bool      hit = false;
    pthread_mutex_lock(&range_lock);
    HostRange *r = range_find(a);
    if (r && r->base == a && r->tree_node && (!tree || r->owner == tree))
        hit = true;
    pthread_mutex_unlock(&range_lock);
    return hit;
}

void cccc_comptime_host_range_drop(const void *owner) {
    if (!owner)
        return;
    pthread_mutex_lock(&range_lock);
    HostRange *r;
    while ((r = hashmap_get_int(&range_owners, owner_key(owner))))
        range_remove(r);
    pthread_mutex_unlock(&range_lock);
}

// tdelete leaves the tree's live nodes marked; the sweep drops the rest.
static void tree_node_mark(const void *node, const void *tree) {
    pthread_mutex_lock(&range_lock);
    HostRange *r = range_find((uintptr_t)node);
    if (!(r && r->base == (uintptr_t)node && r->tree_node && r->owner == tree &&
          r->len == sizeof(void *)))
        r = range_add_locked(node, sizeof(void *), tree, false, true);
    if (r)
        r->seen = true;
    pthread_mutex_unlock(&range_lock);
}

static void tree_node_sweep(const void *tree) {
    pthread_mutex_lock(&range_lock);
    HostRange *r = hashmap_get_int(&range_owners, owner_key(tree));
    while (r) {
        HostRange *next = r->owner_next;
        if (r->tree_node && !r->seen)
            range_remove(r);
        else
            r->seen = false;
        r = next;
    }
    pthread_mutex_unlock(&range_lock);
}

size_t cccc_comptime_host_range_left(const void *p, bool write) {
    uintptr_t a    = (uintptr_t)p;
    size_t    left = 0;
    pthread_mutex_lock(&range_lock);
    HostRange *r = range_find(a);
    if (r && r->base <= a && (!write || r->writable)) {
        uintptr_t end = r->base + r->len;
        left          = end - a;
        // Adjacent ranges read as one (an ENTRY's read-only key, writable
        // data).
        for (HostRange *n = range_successor(r);
             n && n->base == end && (!write || n->writable);
             n = range_successor(n)) {
            end  += n->len;
            left += n->len;
        }
    }
    pthread_mutex_unlock(&range_lock);
    return left;
}

// --- Spec evaluation ---------------------------------------------------------

typedef struct {
    VirtualMachine *vm;
    const char     *name;
    long long      *args;
    int             nargs;
    bool            in_deref; // checking a pointer stored at byte deref_off
    int             deref_off;
} CallCtx;

static int fault(CallCtx *c, int arg, const void *p, size_t n, bool write) {
    char what[96];
    if (c->in_deref)
        snprintf(what, sizeof what, "call to '%s' argument %d [%d]", c->name,
                 arg, c->deref_off);
    else
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

// One parsed spec token: kind letter, size expression, flags and deref suffix.
typedef struct {
    char        kind;
    bool        opt, minus1;
    int         cond;  // deref only when args[cond] is NULL, -1 for always
    const char *size;  // start of the <size> expression, NULL when unsized
    const char *deref; // contents of [...], NULL when absent
} Tok;

static const char *skip_size(const char *p) {
    for (;;) {
        if (p[0] == '@' && p[1] == '@') {
            p += 3;
        } else if (p[0] == '@' || p[0] == '$') {
            p += 2;
        } else if (p[0] == '=') {
            p++;
            if (*p >= '0' && *p <= '9')
                while (*p >= '0' && *p <= '9')
                    p++;
            else
                while (*p >= 'a' && *p <= 'z')
                    p++;
        } else {
            return p;
        }
        if (*p != '*')
            return p;
        p++;
    }
}

static const char *parse_tok(const char *p, Tok *t) {
    *t = (Tok){.kind = *p++, .cond = -1};
    if (t->kind == 'r' || t->kind == 'w' || t->kind == 'n') {
        t->size = p;
        p       = skip_size(p);
    }
    for (;; p++) {
        if (*p == '?')
            t->opt = true;
        else if (*p == '!')
            t->minus1 = true;
        else
            break;
    }
    if (*p == '^') {
        t->cond  = p[1] - '0';
        p       += 2;
    }
    if (*p == '[') {
        t->deref = ++p;
        while (*p && *p != ']')
            p++;
        if (*p == ']')
            p++;
    }
    return p;
}

static int fault_arg_msg(CallCtx *c, int arg, const char *msg) {
    char buf[160];
    snprintf(buf, sizeof buf, "call to '%s' argument %d: %s", c->name, arg,
             msg);
    return cccc_comptime_abort(c->vm, buf);
}

// Checks the token kinds that need only the pointer value and a byte count.
static int check_leaf(CallCtx *c, int arg, char kind, size_t n, void *ptr) {
    VirtualMachine *vm = c->vm;
    switch (kind) {
        case 'r':
        case 'w':
            if (n && !cccc_comptime_addr_ok(vm, ptr, n, kind == 'w'))
                return fault(c, arg, ptr, n, kind == 'w');
            break;
        case 's':
            if (cccc_comptime_cstr_len(vm, ptr, SIZE_MAX) < 0)
                return fault(c, arg, ptr, 1, false);
            break;
        case 'm': {
            long long len = cccc_comptime_cstr_len(vm, ptr, SIZE_MAX);
            if (len < 0)
                return fault(c, arg, ptr, 1, true);
            if (!cccc_comptime_addr_ok(vm, ptr, (size_t)len + 1, true))
                return fault(c, arg, ptr, (size_t)len + 1, true);
            break;
        }
        case 'n':
            if (cccc_comptime_cstr_len(vm, ptr, n) < 0 &&
                !cccc_comptime_addr_ok(vm, ptr, n, false))
                return fault(c, arg, ptr, n, false);
            break;
        case 'W':
            if (!wide_str_ok(vm, ptr))
                return fault(c, arg, ptr, sizeof(wchar_t), false);
            break;
        case 'h':
            if (!host_ptr_known(vm, ptr))
                return fault(c, arg, ptr, 1, false);
            break;
        case 'x':
            if (!ptr)
                return fault_arg_msg(c, arg, "function pointer is NULL");
            break;
    }
    return 0;
}

// Follows the pointers stored in the buffer at `outer`, which passed its own
// check, and checks each against its leaf token.
static int check_deref(CallCtx *c, int arg, const Tok *t, void *outer) {
    if (t->cond >= 0 && (t->cond >= c->nargs || c->args[t->cond]))
        return 0;
    const char *p = t->deref;
    while (*p && *p != ']') {
        while (*p == ' ' || *p == ';')
            p++;
        unsigned long off = 0;
        if (*p == '@') {
            off = strtoul(p + 1, (char **)&p, 10);
            while (*p == ' ')
                p++;
        }
        Tok in;
        p          = parse_tok(p, &in);

        char *slot = (char *)outer + off;
        void *inner;
        c->in_deref  = true;
        c->deref_off = (int)off;
        int rc       = 0;
        if (!cccc_comptime_addr_ok(c->vm, slot, sizeof inner, false)) {
            rc = fault(c, arg, slot, sizeof inner, false);
        } else {
            memcpy(&inner, slot, sizeof inner);
            size_t n = 0;
            if (in.kind == '-' || (in.minus1 && inner == (void *)-1) ||
                (!inner && in.opt)) {
                // nothing to check
            } else if (in.size && !eval_size(c, &(const char *){in.size}, &n)) {
                rc = fault(c, arg, inner, 1, in.kind == 'w');
            } else {
                rc = check_leaf(c, arg, in.kind, n, inner);
            }
        }
        c->in_deref = false;
        if (rc)
            return rc;
    }
    return 0;
}

// --- Checks that walk arrays or consult registries
// -----------------------------

typedef int (*PrecheckFn)(CallCtx *c);

// argv[0..argc) are strings libc reads (and, permuting, moves).
static int check_argv(CallCtx *c) {
    long long argc = c->args[0];
    char    **argv = (char **)c->args[1];
    for (long long i = 0; i < argc; i++) {
        c->in_deref  = true;
        c->deref_off = (int)(i * (long long)sizeof *argv);
        int rc       = 0;
        if (cccc_comptime_cstr_len(c->vm, argv[i], SIZE_MAX) < 0)
            rc = fault(c, 1, argv[i], 1, false);
        c->in_deref = false;
        if (rc)
            return rc;
    }
    return 0;
}

static int pre_getopt_long(CallCtx *c) {
    if (c->nargs < 5)
        return 0;
    int rc = check_argv(c);
    if (rc)
        return rc;
    const struct option *longopts = (const struct option *)c->args[3];
    if (!longopts)
        return 0;
    for (size_t i = 0; i < ((size_t)1 << 16); i++) {
        const struct option *o   = longopts + i;
        bool                 end = false;
        c->in_deref              = true;
        c->deref_off             = (int)(i * sizeof *o);
        if (!cccc_comptime_addr_ok(c->vm, o, sizeof *o, false))
            rc = fault(c, 3, o, sizeof *o, false);
        else if (!o->name)
            end = true;
        else if (cccc_comptime_cstr_len(c->vm, o->name, SIZE_MAX) < 0)
            rc = fault(c, 3, o->name, 1, false);
        else if (o->flag &&
                 !cccc_comptime_addr_ok(c->vm, o->flag, sizeof(int), true))
            rc = fault(c, 3, o->flag, sizeof(int), true);
        c->in_deref = false;
        if (rc || end)
            return rc;
    }
    return fault_arg_msg(c, 3, "longopts has no terminating entry");
}

static int pre_getopt(CallCtx *c) {
    return c->nargs < 3 ? 0 : check_argv(c);
}

// scandir hands alphasort pointers to dirent pointers.
static int pre_alphasort(CallCtx *c) {
    for (int i = 0; i < 2 && i < c->nargs; i++) {
        char *e;
        memcpy(&e, (void *)c->args[i], sizeof e);
        c->in_deref  = true;
        c->deref_off = 0;
        int rc       = 0;
        if (!cccc_comptime_addr_ok(c->vm, e, offsetof(struct dirent, d_name),
                                   false))
            rc = fault(c, i, e, offsetof(struct dirent, d_name), false);
        else if (cccc_comptime_cstr_len(
                     c->vm, e + offsetof(struct dirent, d_name), SIZE_MAX) < 0)
            rc = fault(c, i, e + offsetof(struct dirent, d_name), 1, false);
        c->in_deref = false;
        if (rc)
            return rc;
    }
    return 0;
}

// *rootp is NULL or a node libc returned for this root variable.
static int pre_tree_root(CallCtx *c) {
    if (c->nargs < 2)
        return 0;
    void *rootp = (void *)c->args[1];
    void *root;
    memcpy(&root, rootp, sizeof root);
    if (root && !cccc_comptime_host_node_known(root, rootp))
        return fault_arg_msg(c, 1, "the tree root is not a node libc returned");
    return 0;
}

static int pre_twalk(CallCtx *c) {
    void *root = c->nargs > 0 ? (void *)c->args[0] : NULL;
    if (root && !cccc_comptime_host_node_known(root, NULL))
        return fault_arg_msg(c, 0, "not a tree node libc returned");
    return 0;
}

// --- regex_t: only one regcomp filled, unchanged since ---

static bool regex_matches(VirtualMachine *vm, const regex_t *preg,
                          bool need_live) {
    Registry *r = vm->comptime_registry;
    if (!r)
        return false;
    pthread_mutex_lock(&r->lock);
    RegexSnap *snap = hashmap_get_int(&r->regexes, (long long)(uintptr_t)preg);
    bool       ok   = snap && (snap->live || !need_live) &&
                      memcmp(&snap->copy, preg, sizeof *preg) == 0;
    pthread_mutex_unlock(&r->lock);
    return ok;
}

static void regex_record(VirtualMachine *vm, const regex_t *preg, bool live) {
    Registry *r = vm->comptime_registry;
    if (!r)
        return;
    pthread_mutex_lock(&r->lock);
    RegexSnap *snap = hashmap_get_int(&r->regexes, (long long)(uintptr_t)preg);
    if (!snap && (snap = calloc(1, sizeof *snap)))
        hashmap_put_int(&r->regexes, (long long)(uintptr_t)preg, snap);
    if (snap) {
        memcpy(&snap->copy, preg, sizeof *preg);
        snap->live = live;
    }
    pthread_mutex_unlock(&r->lock);
}

static int pre_regexec(CallCtx *c) {
    if (!regex_matches(c->vm, (const regex_t *)c->args[0], true))
        return fault_arg_msg(c, 0, "not a regex_t compiled by regcomp");
    return 0;
}

static int pre_regfree(CallCtx *c) {
    if (!regex_matches(c->vm, (const regex_t *)c->args[0], false))
        return fault_arg_msg(c, 0, "not a regex_t compiled by regcomp");
    return 0;
}

// A Quote template's placeholders each take a Node * from the variadic tail.
static int pre_quote(CallCtx *c) {
    if (c->nargs < 1 || !c->args[0])
        return 0;
    int need = cc_quote_max_index((const char *)c->args[0]);
    if (need > 64)
        need = 64;
    if (c->nargs - 1 < need) {
        char msg[96];
        snprintf(msg, sizeof msg,
                 "template references $%d but only %d argument%s passed", need,
                 c->nargs - 1, c->nargs == 2 ? " was" : "s were");
        return fault_msg(c, msg);
    }
    for (int i = 1; i <= need; i++)
        if (!cccc_comptime_handle_ok(c->vm, (void *)c->args[i], CC_H_NODE))
            return fault_arg_msg(c, i, "not a Node handle");
    return 0;
}

static const struct {
    const char *name;
    PrecheckFn  fn;
} PRECHECKS[] = {
    {"__builtin_quote", pre_quote},
    {"__builtin_quote_lazy", pre_quote},
    {"alphasort", pre_alphasort},
    {"getopt", pre_getopt},
    {"getopt_long", pre_getopt_long},
    {"regexec", pre_regexec},
    {"regfree", pre_regfree},
    {"tdelete", pre_tree_root},
    {"tfind", pre_tree_root},
    {"tsearch", pre_tree_root},
    {"twalk", pre_twalk},
};

static HashMap        prechecks;
static pthread_once_t prechecks_once = PTHREAD_ONCE_INIT;

static void build_prechecks(void) {
    for (size_t i = 0; i < sizeof(PRECHECKS) / sizeof(PRECHECKS[0]); i++)
        hashmap_put(&prechecks, PRECHECKS[i].name, (void *)PRECHECKS[i].fn);
}

static int run_spec(VirtualMachine *vm, const char *name, const char *spec,
                    long long *args, int nargs, bool locale_tail) {
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
        Tok t;
        p = parse_tok(p, &t);
        if (t.kind == '-' || i >= nargs)
            continue;
        void *ptr = (void *)args[i];
        if (t.minus1 && ptr == (void *)-1)
            continue;
        if (!ptr && t.opt)
            continue;
        size_t n = 0;
        if (t.size && !eval_size(&c, &(const char *){t.size}, &n))
            return fault(&c, i, ptr, 1, t.kind == 'w');
        switch (t.kind) {
            case 'f':
            case 'F': {
                int rc = check_leaf(&c, i, 's', n, ptr);
                if (rc)
                    return rc;
                fmt_idx  = i;
                fmt_scan = t.kind == 'F';
                break;
            }
            case 'c':
            case 'C': {
                long long dl = cccc_comptime_cstr_len(vm, ptr, SIZE_MAX);
                long long sl =
                    i + 1 < nargs
                        ? cccc_comptime_cstr_len(vm, (void *)args[i + 1],
                                                 t.kind == 'C' && i + 2 < nargs
                                                     ? (size_t)args[i + 2]
                                                     : SIZE_MAX)
                        : 0;
                if (dl < 0)
                    return fault(&c, i, ptr, 1, true);
                if (sl < 0)
                    sl = t.kind == 'C' && i + 2 < nargs ? args[i + 2] : 0;
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
            default: {
                int rc = check_leaf(&c, i, t.kind, n, ptr);
                if (rc)
                    return rc;
                if (t.deref && (rc = check_deref(&c, i, &t, ptr)))
                    return rc;
            }
        }
    }

    pthread_once(&prechecks_once, build_prechecks);
    PrecheckFn pre = hashmap_get(&prechecks, name);
    if (pre) {
        int rc = pre(&c);
        if (rc)
            return rc;
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

int cccc_comptime_check_args(VirtualMachine *vm, const char *name,
                             long long *args, int nargs) {
    if (strncmp(name, "__builtin_", 10) == 0)
        return 0; // cccc_comptime_check_builtin
    const char *spec = cccc_comptime_arg_spec(name);
    if (!spec)
        return 0;
    bool locale_tail = false, alias = false;
    lookup_spec(name, strlen(name), &locale_tail, &alias);
    if (!*spec && !locale_tail)
        return 0;
    return run_spec(vm, name, spec, args, nargs, locale_tail);
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

// A tsearch/tfind node is readable until tdelete frees it.
static void r_tnode(VirtualMachine *vm, long long *args, int nargs,
                    long long *result) {
    if (nargs > 1 && is_host(vm, (void *)*result))
        cccc_comptime_host_node_add((void *)*result, (void *)args[1]);
}

static _Thread_local const void *walk_tree;

static void walk_register(const void *node, VISIT which, int depth) {
    (void)depth;
    if (which == postorder || which == leaf)
        tree_node_mark(node, walk_tree);
}

// libc frees the matched node or, on glibc and musl, a neighbour whose key it
// moved, so the walk marks the live nodes and the sweep drops the rest.
static void r_tdelete(VirtualMachine *vm, long long *args, int nargs,
                      long long *result) {
    (void)result;
    if (nargs < 2)
        return;
    void *rootp = (void *)args[1];
    void *root  = NULL;
    if (cccc_comptime_addr_ok(vm, rootp, sizeof root, false))
        memcpy(&root, rootp, sizeof root);
    if (!root) {
        cccc_comptime_host_range_drop(rootp);
        return;
    }
    walk_tree = rootp;
    twalk(root, walk_register);
    tree_node_sweep(rootp);
}

static char hsearch_owner;

static void r_hsearch(VirtualMachine *vm, long long *args, int nargs,
                      long long *result) {
    (void)args, (void)nargs;
    ENTRY *e = (ENTRY *)*result;
    if (!is_host(vm, e))
        return;
    // The key is hsearch's own comparison state; only data is the caller's.
    cccc_comptime_host_range_add(&e->key, sizeof e->key, &hsearch_owner, false);
    cccc_comptime_host_range_add(&e->data, sizeof e->data, &hsearch_owner,
                                 true);
}

static void r_hdestroy(VirtualMachine *vm, long long *args, int nargs,
                       long long *result) {
    (void)vm, (void)args, (void)nargs, (void)result;
    cccc_comptime_host_range_drop(&hsearch_owner);
}

static void r_regcomp(VirtualMachine *vm, long long *args, int nargs,
                      long long *result) {
    if (nargs > 0)
        regex_record(vm, (const regex_t *)args[0], *result == 0);
}

// A freed regex_t may be freed again, as regfree allows, but not executed.
static void r_regfree(VirtualMachine *vm, long long *args, int nargs,
                      long long *result) {
    (void)result;
    if (nargs > 0)
        regex_record(vm, (const regex_t *)args[0], false);
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
    {"regcomp", r_regcomp},
    {"regfree", r_regfree},
    {"realpath", r_string_free},
    {"scandir", r_scandir},
    {"tdelete", r_tdelete},
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
