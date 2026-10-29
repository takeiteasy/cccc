// Promoted locals used as index, base, multiply-add, compare and arithmetic
// operands give the same results as the same loop over volatile locals, which
// stay in memory.
#define BODY(NAME, Q)                                                          \
    static long NAME(long *p, int n) {                                         \
        long long Q     s  = 0;                                                \
        int Q           i  = 0;                                                \
        long *Q         q  = p;                                                \
        double Q        d  = 0.5;                                              \
        double Q        e  = 1.25;                                             \
        float Q         f  = 0.5f;                                             \
        unsigned Q      u  = 1;                                                \
        short Q         k  = -3;                                               \
        char Q          c  = 7;                                                \
        unsigned char Q uc = 250;                                              \
        for (i = 0; i < n; i++) {                                              \
            s += q[i] * i;                                                     \
            s += p[i] + q[n - 1 - i];                                          \
            s += p[u % (unsigned)n] * k;                                       \
            d += (double)i * e + d * 0.25;                                     \
            f += (float)i * f - 0.125f;                                        \
            if (i * 2 > n)                                                     \
                s -= i;                                                        \
            u  += (unsigned)i - 3u;                                            \
            k  += (short)(i - c);                                              \
            c  += (char)(k >> 2);                                              \
            uc += (unsigned char)i;                                            \
            s  ^= (long long)uc + (s >> 3);                                    \
            s  += (i < n - 2) ? q[i + 1] : -q[0];                              \
        }                                                                      \
        return (long)(s + (long)d + (long)f + u + k + c + uc);                 \
    }

BODY(in_registers, )
BODY(in_memory, volatile)

int main(void) {
    long data[16];
    for (int i = 0; i < 16; i++)
        data[i] = (i * 37 + 11) % 23 - 7;
    for (int n = 1; n <= 16; n++)
        if (in_registers(data, n) != in_memory(data, n))
            return n;
    return 42;
}
