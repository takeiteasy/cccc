#include "./parse_internal.h"

// Bounds for libc/POSIX prototypes, applied under --checked-pointers to a
// system-header declaration of the same name so callers are checked without
// the header spelling any attribute. Each row is a full prototype; only its
// bounds are used (merge_checked_interface_defaults), so the types are
// stand-ins and a bound names its siblings by the row's own parameter names.
//
// A pointer gets byte_count(n) only when the function may touch all n bytes,
// the rule glibc's _FORTIFY_SOURCE uses. strncpy's source, strncmp, memchr,
// memcmp and strncat's destination stop early or extend past n, so have no row.
//
// The `__*_chk` rows cover the fortified entry points (the `__builtin___*_chk`
// stubs, and glibc/Darwin declarations of the same name): the object-size
// argument is not a bound, the count is. They are matched by name, then by
// asm label (`__read_chk_warn` is labelled `__read_chk`, `pread` `pread64`).
#define BC(n) "[[cccc::array, cccc::byte_count(" n ")]]"

static const struct {
    const char *name;
    const char *proto;
} ROWS[] = {
    // string.h
    {"memcpy", "void *memcpy(void *" BC("n") " d, const void *" BC(
                   "n") " s, "
                        "unsigned long n)"},
    {"memmove", "void *memmove(void *" BC("n") " d, const void *" BC(
                    "n") " s, "
                         "unsigned long n)"},
    {"memset", "void *memset(void *" BC("n") " s, int c, unsigned long n)"},
    {"mempcpy", "void *mempcpy(void *" BC("n") " d, const void *" BC(
                    "n") " s, "
                         "unsigned long n)"},
    {"strncpy", "char *strncpy(char *" BC("n") " d, const char *s, "
                                               "unsigned long n)"},
    {"stpncpy", "char *stpncpy(char *" BC("n") " d, const char *s, "
                                               "unsigned long n)"},
    {"strlcpy", "unsigned long strlcpy(char *" BC("n") " d, const char *s, "
                                                       "unsigned long n)"},
    {"strlcat", "unsigned long strlcat(char *" BC("n") " d, const char *s, "
                                                       "unsigned long n)"},

    // stdio.h
    {"fread",
     "unsigned long fread(void *" BC(
         "size * nmemb") " p, "
                         "unsigned long size, unsigned long nmemb, void *f)"},
    {"fwrite",
     "unsigned long fwrite(const void *" BC(
         "size * nmemb") " p, "
                         "unsigned long size, unsigned long nmemb, void *f)"},
    {"fgets", "char *fgets(char *" BC("n") " s, int n, void *f)"},
    {"snprintf", "int snprintf(char *" BC("n") " s, unsigned long n, "
                                               "const char *fmt, ...)"},
    {"vsnprintf", "int vsnprintf(char *" BC("n") " s, unsigned long n, "
                                                 "const char *fmt, void *ap)"},

    // unistd.h
    {"read", "long read(int fd, void *" BC("n") " buf, unsigned long n)"},
    {"write", "long write(int fd, const void *" BC("n") " buf, "
                                                        "unsigned long n)"},
    {"pread", "long pread(int fd, void *" BC("n") " buf, unsigned long n, "
                                                  "long off)"},
    {"pwrite",
     "long pwrite(int fd, const void *" BC("n") " buf, "
                                                "unsigned long n, long off)"},
    {"getcwd", "char *getcwd(char *" BC("n") " buf, unsigned long n)"},
    {"pread64", "long pread64(int fd, void *" BC("n") " buf, unsigned long n, "
                                                      "long off)"},
    {"readlink",
     "long readlink(const char *path, char *" BC("n") " buf, "
                                                      "unsigned long n)"},
    {"gethostname", "int gethostname(char *" BC("n") " name, unsigned long n)"},

    // sys/socket.h
    {"recv", "long recv(int fd, void *" BC("n") " buf, unsigned long n, "
                                                "int flags)"},
    {"send", "long send(int fd, const void *" BC("n") " buf, unsigned long n, "
                                                      "int flags)"},
    {"recvfrom",
     "long recvfrom(int fd, void *" BC(
         "n") " buf, "
              "unsigned long n, int flags, void *addr, void *alen)"},
    {"sendto",
     "long sendto(int fd, const void *" BC(
         "n") " buf, "
              "unsigned long n, int flags, const void *addr, unsigned alen)"},

    // Fortified entry points. `os` / `buflen` / `nreal` are object sizes.
    {"__memcpy_chk", "void *__memcpy_chk(void *" BC("n") " d, const void *" BC(
                         "n") " s, "
                              "unsigned long n, unsigned long os)"},
    {"__memmove_chk", "void *__memmove_chk(void *" BC(
                          "n") " d, const void *" BC("n") " s, "
                                                          "unsigned long n, "
                                                          "unsigned long os)"},
    {"__mempcpy_chk", "void *__mempcpy_chk(void *" BC(
                          "n") " d, const void *" BC("n") " s, "
                                                          "unsigned long n, "
                                                          "unsigned long os)"},
    {"__memset_chk", "void *__memset_chk(void *" BC("n") " s, int c, "
                                                         "unsigned long n, "
                                                         "unsigned long os)"},
    {"__strncpy_chk",
     "char *__strncpy_chk(char *" BC("n") " d, const char *s, "
                                          "unsigned long n, unsigned long os)"},
    {"__stpncpy_chk",
     "char *__stpncpy_chk(char *" BC("n") " d, const char *s, "
                                          "unsigned long n, unsigned long os)"},
    {"__strlcpy_chk", "unsigned long __strlcpy_chk(char *" BC(
                          "n") " d, const char *s, "
                               "unsigned long n, unsigned long os)"},
    {"__strlcat_chk", "unsigned long __strlcat_chk(char *" BC(
                          "n") " d, const char *s, "
                               "unsigned long n, unsigned long os)"},
    {"__snprintf_chk",
     "int __snprintf_chk(char *" BC("n") " s, unsigned long n, int flag, "
                                         "unsigned long os, const char *fmt, "
                                         "...)"},
    {"__vsnprintf_chk",
     "int __vsnprintf_chk(char *" BC("n") " s, unsigned long n, int flag, "
                                          "unsigned long os, const char *fmt, "
                                          "void *ap)"},
    {"__fgets_chk", "char *__fgets_chk(char *" BC(
                        "n") " s, unsigned long size, int n, void *f)"},
    {"__fread_chk",
     "unsigned long __fread_chk(void *" BC("size * nmemb") " p, "
                                                           "unsigned long "
                                                           "plen, unsigned "
                                                           "long size, "
                                                           "unsigned long "
                                                           "nmemb, void *f)"},
    {"__read_chk",
     "long __read_chk(int fd, void *" BC("n") " buf, unsigned long n, "
                                              "unsigned long buflen)"},
    {"__pread_chk", "long __pread_chk(int fd, void *" BC(
                        "n") " buf, unsigned long n, "
                             "long off, unsigned long buflen)"},
    {"__pread64_chk", "long __pread64_chk(int fd, void *" BC(
                          "n") " buf, unsigned long n, "
                               "long off, unsigned long buflen)"},
    {"__readlink_chk", "long __readlink_chk(const char *path, char *" BC(
                           "n") " buf, "
                                "unsigned long n, unsigned long buflen)"},
    {"__getcwd_chk", "char *__getcwd_chk(char *" BC(
                         "n") " buf, unsigned long n, unsigned long buflen)"},
    {"__gethostname_chk",
     "int __gethostname_chk(char *" BC("n") " name, unsigned long n, "
                                            "unsigned long nreal)"},
    {"__recv_chk", "long __recv_chk(int fd, void *" BC(
                       "n") " buf, unsigned long n, "
                            "unsigned long buflen, int flags)"},
    {"__recvfrom_chk",
     "long __recvfrom_chk(int fd, void *" BC(
         "n") " buf, unsigned long n, unsigned long buflen, int flags, "
              "void *addr, void *alen)"},

    // Entry points added with the rest of the fortified set. Wide counts are
    // elements, so the byte count is n * 4 (wchar_t is 4 bytes on every host).
    {"readlinkat", "long readlinkat(int fd, const char *path, char *" BC(
                       "n") " buf, unsigned long n)"},
    {"getdomainname",
     "int getdomainname(char *" BC("n") " name, unsigned long n)"},
    {"ptsname_r",
     "int ptsname_r(int fd, char *" BC("n") " buf, unsigned long n)"},
    {"bzero", "void bzero(void *" BC("n") " s, unsigned long n)"},
    {"explicit_bzero",
     "void explicit_bzero(void *" BC("n") " s, unsigned long n)"},
    {"fgets_unlocked", "char *fgets_unlocked(char *" BC("n") " s, int n, "
                                                             "void *f)"},
    {"fread_unlocked",
     "unsigned long fread_unlocked(void *" BC(
         "size * nmemb") " p, "
                         "unsigned long size, unsigned long nmemb, void *f)"},
    {"wmemcpy", "int *wmemcpy(int *" BC("n * 4") " d, const int *" BC(
                    "n * 4") " s, "
                             "unsigned long n)"},
    {"wmemmove", "int *wmemmove(int *" BC("n * 4") " d, const int *" BC(
                     "n * 4") " s, "
                              "unsigned long n)"},
    {"wmempcpy", "int *wmempcpy(int *" BC("n * 4") " d, const int *" BC(
                     "n * 4") " s, "
                              "unsigned long n)"},
    {"wmemset", "int *wmemset(int *" BC("n * 4") " s, int c, unsigned long n)"},
    {"__readlinkat_chk",
     "long __readlinkat_chk(int fd, const char *path, char *" BC(
         "n") " buf, unsigned long n, unsigned long buflen)"},
    {"__getdomainname_chk",
     "int __getdomainname_chk(char *" BC("n") " name, unsigned long n, "
                                              "unsigned long nreal)"},
    {"__ptsname_r_chk",
     "int __ptsname_r_chk(int fd, char *" BC("n") " buf, unsigned long n, "
                                                  "unsigned long nreal)"},
    {"__ttyname_r_chk",
     "int __ttyname_r_chk(int fd, char *" BC("n") " buf, unsigned long n, "
                                                  "unsigned long nreal)"},
    {"__getlogin_r_chk",
     "int __getlogin_r_chk(char *" BC("n") " buf, unsigned long n, "
                                           "unsigned long nreal)"},
    {"__explicit_bzero_chk",
     "void __explicit_bzero_chk(void *" BC("n") " s, unsigned long n, "
                                                "unsigned long os)"},
    {"__fgets_unlocked_chk",
     "char *__fgets_unlocked_chk(char *" BC(
         "n") " s, unsigned long size, int n, void *f)"},
    {"__fread_unlocked_chk",
     "unsigned long __fread_unlocked_chk(void *" BC(
         "size * nmemb") " p, unsigned long plen, unsigned long size, "
                         "unsigned long nmemb, void *f)"},
    {"__wmemcpy_chk",
     "int *__wmemcpy_chk(int *" BC("n * 4") " d, const int *" BC(
         "n * 4") " s, unsigned long n, unsigned long ns)"},
    {"__wmemmove_chk",
     "int *__wmemmove_chk(int *" BC("n * 4") " d, const int *" BC(
         "n * 4") " s, unsigned long n, unsigned long ns)"},
    {"__wmempcpy_chk",
     "int *__wmempcpy_chk(int *" BC("n * 4") " d, const int *" BC(
         "n * 4") " s, unsigned long n, unsigned long ns)"},
    {"__wmemset_chk",
     "int *__wmemset_chk(int *" BC("n * 4") " s, int c, unsigned long n, "
                                            "unsigned long ns)"},
    {"__wcsncpy_chk",
     "int *__wcsncpy_chk(int *" BC("n * 4") " d, const int *s, unsigned long "
                                            "n, unsigned long dl)"},
    {"__wcpncpy_chk",
     "int *__wcpncpy_chk(int *" BC("n * 4") " d, const int *s, unsigned long "
                                            "n, unsigned long dl)"},
};

static int find_row(const char *name) {
    if (!name)
        return -1;
    for (size_t i = 0; i < sizeof ROWS / sizeof *ROWS; i++)
        if (!strcmp(name, ROWS[i].name))
            return (int)i;
    return -1;
}

static void apply_row(VirtualMachine *vm, Obj *fn, int row) {
    Token  *tok    = tokenize_string(vm, "<checked-libc>", ROWS[row].proto);
    VarAttr attr   = {0};
    Type   *base   = declspec(vm, &tok, tok, &attr);
    Type   *ty     = declarator(vm, &tok, tok, base);
    Type   *before = fn->ty;
    merge_checked_interface_defaults(vm, fn, ty);
    if (fn->ty == before)
        return;
    int idx = 0;
    for (Type *o = before->params, *m = fn->ty->params; o && m;
         o = o->next, m = m->next, idx++)
        if (type_chk(o)->form == CB_NONE && type_chk(m)->form != CB_NONE)
            fn->checked_libc_mask |= 1u << idx;
}

static void apply_by_name_or_label(VirtualMachine *vm, Obj *fn) {
    int row = find_row(fn->name);
    if (row < 0)
        row = find_row(fn->asm_label);
    if (row >= 0)
        apply_row(vm, fn, row);
}

void apply_checked_libc_bounds(VirtualMachine *vm, Obj *fn, Token *decl_tok) {
    if (!(vm->flags & CCCC_CHECKED_BOUNDS) || fn->is_static || !decl_tok ||
        !decl_tok->file || !decl_tok->file->is_system_header ||
        vm->compiler.in_type_lookahead)
        return;
    apply_by_name_or_label(vm, fn);
}

// For a private builtin stub, which has no declaration token to gate on.
void apply_checked_libc_builtin_bounds(VirtualMachine *vm, Obj *fn) {
    if (!(vm->flags & CCCC_CHECKED_BOUNDS) || vm->compiler.in_type_lookahead)
        return;
    apply_by_name_or_label(vm, fn);
}
