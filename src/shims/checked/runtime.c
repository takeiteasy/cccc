static void __cccc_chk_fail(void) {
    fflush(stdout);
    _Exit(255);
}
static inline void *__cccc_chkr(const volatile void *__cccc_a,
                                const volatile void *__cccc_lo,
                                const volatile void *__cccc_hi,
                                long long __cccc_n, int __cccc_optional) {
    intptr_t a = (intptr_t)__cccc_a, lo = (intptr_t)__cccc_lo,
              hi = (intptr_t)__cccc_hi;
    if (__cccc_optional && lo == -1 && hi == 0)
        return (void *)a;
    if (a == 0 || a < lo || a + (intptr_t)__cccc_n > hi) {
        printf("\n========== CHECKED BOUNDS VIOLATION ==========\n");
        printf("Checked-pointer access out of declared bounds\n");
        printf("Address:       0x%llx\n", (unsigned long long)a);
        printf("Access size:   %lld bytes\n", __cccc_n);
        printf("Declared bounds: [0x%llx, 0x%llx)\n", (unsigned long long)lo,
               (unsigned long long)hi);
        printf("================================================\n");
        __cccc_chk_fail();
    }
    return (void *)a;
}
static inline void __cccc_chknt(const volatile void *__cccc_a,
                                const volatile void *__cccc_hi,
                                const volatile void *__cccc_src,
                                long long __cccc_slot, long long __cccc_n,
                                int __cccc_scalar) {
    intptr_t a = (intptr_t)__cccc_a, hi = (intptr_t)__cccc_hi;
    if (__cccc_slot <= 0 || __cccc_n <= 0 || hi < (intptr_t)__cccc_slot ||
        a < hi - (intptr_t)__cccc_slot)
        return;
    const volatile unsigned char *src =
        (const volatile unsigned char *)__cccc_src;
    for (long long i = 0; i < __cccc_n; i++) {
        if (src[i]) {
            printf("\n========== CHECKED TERMINATOR VIOLATION ==========\n");
            printf("Non-%s store into a [[cccc::ntarray]] terminator slot\n",
                   __cccc_scalar ? "null" : "zero");
            printf("Address:         0x%llx\n", (unsigned long long)a);
            printf("Terminator slot: [0x%llx, 0x%llx)\n",
                   (unsigned long long)(hi - (intptr_t)__cccc_slot),
                   (unsigned long long)hi);
            printf("====================================================\n");
            __cccc_chk_fail();
        }
    }
}
static inline void __cccc_chkab(const volatile void *__cccc_v,
                                const volatile void *__cccc_slo,
                                const volatile void *__cccc_shi,
                                int __cccc_is_hi) {
    intptr_t v = (intptr_t)__cccc_v, slo = (intptr_t)__cccc_slo,
              shi = (intptr_t)__cccc_shi;
    if (v < slo || v > shi) {
        printf("\n===== CHECKED ASSIGNMENT BOUNDS VIOLATION =====\n");
        printf("Assignment does not satisfy the target's declared bounds\n");
        printf("Target's declared %s: 0x%llx\n",
               __cccc_is_hi ? "upper bound" : "lower bound",
               (unsigned long long)v);
        printf("Source's declared bounds: [0x%llx, 0x%llx]\n",
               (unsigned long long)slo, (unsigned long long)shi);
        printf("================================================\n");
        __cccc_chk_fail();
    }
}
static inline void __cccc_chkdc(int __cccc_cond, long long __cccc_line) {
    if (!__cccc_cond) {
        printf("\n========== DYNAMIC CHECK FAILED ==========\n");
        printf("dynamic_check() assertion failed at line %lld\n", __cccc_line);
        printf("===========================================\n");
        __cccc_chk_fail();
    }
}
