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
};

void apply_checked_libc_bounds(VirtualMachine *vm, Obj *fn, Token *decl_tok) {
    if (!(vm->flags & CCCC_CHECKED_BOUNDS) || fn->is_static || !decl_tok ||
        !decl_tok->file || !decl_tok->file->is_system_header ||
        vm->compiler.in_type_lookahead)
        return;

    for (size_t i = 0; i < sizeof ROWS / sizeof *ROWS; i++) {
        if (strcmp(fn->name, ROWS[i].name))
            continue;
        Token  *tok    = tokenize_string(vm, "<checked-libc>", ROWS[i].proto);
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
            if (o->checked_bounds_form == CB_NONE &&
                m->checked_bounds_form != CB_NONE)
                fn->checked_libc_mask |= 1u << idx;
        return;
    }
}
