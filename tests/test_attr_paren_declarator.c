// Attributes and asm-labels after a parenthesised declarator apply to the
// declared entity. A C23 attribute directly after the type-suffix applies to
// that type instead, so [[gnu::aligned]] there leaves the member unaligned.
// CCCC_FLAGS: -Wall -Werror
#include <stddef.h>
#include <stdlib.h>

struct Fn {
    char c;
    int (*fp)(void) __attribute__((aligned(16)));
};
struct Arr {
    char c;
    int (*ap)[4] __attribute__((aligned(32)));
};
struct Sibling {
    char c;
    int (*a)(void) __attribute__((aligned(64))), (*b)(void);
};
struct TypeAttr {
    char c;
    int (*fp)(void) [[gnu::aligned(16)]];
};

static_assert(offsetof(struct Fn, fp) == 16);
static_assert(offsetof(struct Arr, ap) == 32);
static_assert(offsetof(struct Sibling, a) == 64);
static_assert(offsetof(struct Sibling, b) == 72);
static_assert(offsetof(struct TypeAttr, fp) == sizeof(void *));

static int one(void) {
    return 1;
}
static void log_fn(const char *fmt, ...) {
    (void)fmt;
}
__attribute__((noreturn)) static void stop(void) {
    exit(42);
}

int (*gfp)(void) __attribute__((aligned(64)))                       = one;
void (*lg)(const char *, ...) __attribute__((format(printf, 1, 2))) = log_fn;
void (*die)(void) __attribute__((noreturn))                         = stop;
extern int (*ext_fp)(void) __asm__("ext_fp_sym") __attribute__((unused));
void (*signal_like(int sig, void (*handler)(int)))(int)
    __attribute__((warn_unused_result));

int main(void) {
    struct Fn s = {.fp = one};
    lg("%d", s.fp() + gfp());
    die();
}
