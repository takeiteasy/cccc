// CCCC_FLAGS: --testing
// Consolidated suite: GNU extended asm (operands, clobbers, goto labels).
//
// The VM never executes asm, so an output keeps its previous value; -c=native
// hands the statement to the host assembler. Every output here is seeded with
// the value the asm would write (or the template is empty with a read-write
// operand), so each test returns the same result both ways and the suite runs
// under --native.

#pragma cccc suite begin "asm_extended"

struct AsmPair {
    int a;
    int b;
};

[[cccc::test(return = 42)]]
int test_asm_output_register(void) {
    int y = 42; // seeded: the VM leaves it, native overwrites it with 42
#if defined(__aarch64__)
    __asm__("mov %w0, #42" : "=r"(y));
#elif defined(__x86_64__)
    __asm__("movl $42, %0" : "=r"(y));
#endif
    return y;
}

[[cccc::test(return = 42)]]
int test_asm_input_and_output(void) {
    int x = 42, y = 42;
#if defined(__aarch64__)
    __asm__("mov %w0, %w1" : "=r"(y) : "r"(x));
#elif defined(__x86_64__)
    __asm__("movl %1, %0" : "=r"(y) : "r"(x));
#endif
    return y;
}

[[cccc::test(return = 42)]]
int test_asm_symbolic_names(void) {
    int x = 42, y = 42;
#if defined(__aarch64__)
    __asm__("mov %w[dst], %w[src]" : [dst] "=r"(y) : [src] "r"(x));
#elif defined(__x86_64__)
    __asm__("movl %[src], %[dst]" : [dst] "=r"(y) : [src] "r"(x));
#endif
    return y;
}

[[cccc::test(return = 42)]]
int test_asm_read_write_empty_template(void) {
    int x = 42;
    __asm__ volatile("" : "+r"(x));
    return x;
}

[[cccc::test(return = 42)]]
int test_asm_clobbers(void) {
    int x = 42;
    __asm__ volatile("" ::: "memory");
    __asm__ volatile("" ::: "cc");
    __asm__ volatile("" : "+r"(x) : : "memory", "cc");
    return x;
}

[[cccc::test(return = 42)]]
int test_asm_qualifier_spellings(void) {
    int x = 42;
    __asm__ __volatile__("" : "+r"(x));
    __asm__ __volatile("" : "+r"(x));
    asm volatile("" : "+r"(x));
    asm inline("" : "+r"(x));
    __asm__ volatile inline("" : "+r"(x));
    return x;
}

[[cccc::test(return = 42)]]
int test_asm_memory_operand(void) {
    int x = 42;
    __asm__ volatile("" : "+m"(x));
    return x;
}

[[cccc::test(return = 42)]]
int test_asm_lvalue_forms(void) {
    struct AsmPair p      = {40, 2};
    int            arr[2] = {40, 2};
    int           *ptr    = &p.a;
    __asm__ volatile("" : "+r"(p.a), "+r"(arr[1]), "+r"(*ptr));
    return p.a + arr[1];
}

[[cccc::test(return = 42)]]
int test_asm_input_expressions(void) {
    int x = 41;
    int y = 42;
    __asm__ volatile("" : "+r"(y) : "r"(x + 1), "i"(5), "r"((char)x));
    return y;
}

[[cccc::test(return = 42)]]
int test_asm_percent_escapes(void) {
    int x = 42;
#if defined(__x86_64__)
    __asm__ volatile("movl %%eax, %%eax" : "+r"(x) : : "eax");
#elif defined(__aarch64__)
    __asm__ volatile("mov x9, x9" : "+r"(x) : : "x9");
#endif
    return x;
}

[[cccc::test(return = 42)]]
int test_asm_goto_label_resolution(void) {
    int r = 0;
    __asm__ goto("" : : : : done);
    r = 42;
done:
    return r;
}

[[cccc::test(return = 42)]]
int test_asm_goto_multiple_labels(void) {
    int r = 0;
    __asm__ volatile goto("" : : : "memory" : first, second);
    r = 42;
first:
second:
    return r;
}

[[cccc::test(return = 42)]]
int test_asm_in_loop(void) {
    int n = 0;
    for (int i = 0; i < 42; i++)
        __asm__ volatile("" : "+r"(n));
    return 42 + n;
}

#pragma cccc suite end
