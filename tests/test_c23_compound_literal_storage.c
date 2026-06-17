// CCCC_FLAGS: --std=c23 -Wignored-features
struct Pair {
    int a;
    int b;
};

int *global_static = &(static int){ 11 };

int next_value(void) {
    int *p = &(static int){ 7 };
    *p = *p + 1;
    return *p;
}

int register_value(void) {
    int *p = &(register int){ 5 };
    *p = *p + 1;
    return *p;
}

int tls_value(void) {
    int *p = &(thread_local int){ 13 };
    return *p;
}

static_assert((constexpr int){ 42 } == 42);
static_assert(((constexpr struct Pair){ 3, 9 }).b == 9);

int main(void) {
    if (*global_static != 11) return 1;
    *global_static = 12;
    if (*global_static != 12) return 2;
    if (next_value() != 8) return 3;
    if (next_value() != 9) return 4;
    if (register_value() != 6) return 5;
    if (register_value() != 6) return 6;
    if (tls_value() != 13) return 7;
    return 42;
}
