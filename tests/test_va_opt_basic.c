// __VA_OPT__ (C23) expands to its argument only when the variadic arguments
// are non-empty -- the standard idiom for a printf-style wrapper macro that
// must omit the trailing comma when no extra arguments are given.

#define LOG(fmt, ...) printf(fmt __VA_OPT__(, ) __VA_ARGS__)

int printf(const char *fmt, ...);

int main(void) {
    LOG("no args\n");
    LOG("one: %d\n", 7);
    LOG("two: %d %d\n", 7, 8);
    return 42;
}
