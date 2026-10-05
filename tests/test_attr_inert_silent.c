// Attributes that __has_attribute reports as recognized are accepted without
// an "unknown attribute" warning, in both GNU and [[gnu::]] spellings.
// CCCC_FLAGS: -Wall -Werror

#if __has_attribute(cold)
void report(const char *fmt, ...)
    __attribute__((cold, __hot__, noinline, format(printf, 1, 2)));
#endif

__attribute__((used, visibility("default"))) int g = 40;
__attribute__((always_inline)) static inline int one(void) {
    return 1;
}
[[gnu::cold]] void gnu_cold(void) {}
[[gnu::noinline]] static int two(void) {
    return 1;
}

void report(const char *fmt, ...) {
    (void)fmt;
}

int main(void) {
    report("%d", 1);
    gnu_cold();
    return g + one() + two();
}
