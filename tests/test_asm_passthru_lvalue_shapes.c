// CCCC_FLAGS: --asm-passthru
//
// A VLA element is an addressable asm memory operand under --asm-passthru.

#if defined(__aarch64__)
#define LOADW "ldr %w0, %1"
#define STOREW "str %w1, %0"
#elif defined(__x86_64__)
#define LOADW "movl %1, %0"
#define STOREW "movl %1, %0"
#endif

int main(void) {
    int n = 3;
    int v[n];
    v[0] = v[1] = v[2] = 0;
    __asm__ volatile(STOREW : "=m"(v[1]) : "r"(7));
    if (v[1] != 7)
        return 1;

    int got;
    __asm__ volatile(LOADW : "=r"(got) : "m"(v[1]));
    if (got != 7)
        return 2;
    return 42;
}
