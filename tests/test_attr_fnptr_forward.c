// Function attributes on a function-pointer declarator apply to the pointee,
// so calls through the pointer are checked like direct calls.
// CCCC_FLAGS: --format-string-checks -Wnonnull -Wnodiscard
// CCCC_EXPECT_STDERR: (?s)\A(?=(?:.*?warning: format argument){2})(?=.*?warning: null passed)(?=.*?warning: ignoring return value).*^4 warnings generated
#include <stddef.h>

static void log_fn(const char *fmt, ...) {
    (void)fmt;
}
static void take(int *p) {
    (void)p;
}
static int answer(void) {
    return 42;
}

typedef void (*log_t)(const char *, ...);

void (*lg)(const char *, ...) __attribute__((format(printf, 1, 2))) = log_fn;
log_t lg2 __attribute__((format(printf, 1, 2)))                     = log_fn;
void (*nn)(int *) __attribute__((nonnull))                          = take;
int (*ans)(void) __attribute__((warn_unused_result))                = answer;

int main(void) {
    lg("%d", "x");
    lg2("%d", "x");
    nn(NULL);
    ans();
    return ans();
}
