// CCCC_FLAGS: --asm-passthru
//
// --asm-passthru passes extended-asm operands to the host by address:
// outputs, read-write, inputs, constants, memory, floating point, pointers.

#if defined(__aarch64__)
#define MOVW "mov %w0, %w1"
#define ADDW "add %w0, %w0, %w1"
#define ADDI "add %w0, %w0, %1"
#define STOREW "str %w1, %0"
#define STOREX "str %x1, %0"
#define LOADW "ldr %w0, [%1]"
#define FMOVD "fmov %d0, %d1"
#define FREG "w"
#elif defined(__x86_64__)
#define MOVW "movl %1, %0"
#define ADDW "addl %1, %0"
#define ADDI "addl %1, %0"
#define STOREW "movl %1, %0"
#define STOREX "movq %1, %0"
#define LOADW "movl (%1), %0"
#define FMOVD "movsd %1, %0"
#define FREG "x"
#endif

typedef struct {
    int a, b;
} Pair;

static int fail(int code) { return code; }

int main(void) {
    int x = 40, y = 0, z = 1;

    __asm__ volatile(MOVW : "=r"(y) : "r"(x));
    if (y != 40)
        return fail(1);

    __asm__(ADDI : "+r"(z) : "i"(41));
    if (z != 42)
        return fail(2);

    __asm__(ADDW : [acc] "+r"(y) : [inc] "r"(x));
    if (y != 80)
        return fail(3);

    int mem = 0;
    __asm__ volatile(STOREW : "=m"(mem) : "r"(7));
    if (mem != 7)
        return fail(4);

    Pair p = {1, 2};
    __asm__ volatile(STOREW : "=m"(p.b) : "r"(9));
    if (p.a != 1 || p.b != 9)
        return fail(5);

    Pair q = {0, 0};
    __asm__ volatile(STOREX : "=m"(q) : "r"(0x0000002A00000001ULL));
    if (q.a != 1 || q.b != 42)
        return fail(6);

    double d = 0.0, e = 2.5;
    __asm__ volatile(FMOVD : "=" FREG(d) : FREG(e));
    if (d != 2.5)
        return fail(7);

    int src = 33, dst = 0;
    int *ptr = &src;
    __asm__ volatile(LOADW : "=r"(dst) : "r"(ptr));
    if (dst != 33)
        return fail(8);

    int arr[2] = {11, 22};
    __asm__ volatile(LOADW : "=r"(dst) : "r"(arr));
    if (dst != 11)
        return fail(9);

    __asm__ volatile(MOVW : "=r"(dst) : "r"(x + 2));
    if (dst != 42)
        return fail(10);

    __asm__ volatile("" ::: "memory");
    return 42;
}
