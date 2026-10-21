// CCCC_VM_SKIP: the VM does not run extended asm
// extended asm: -c=native hands operands to the host assembler, so an unseeded
// output holds the asm's result (the VM never runs it)

int main(void) {
    int x = 40, y;
#if defined(__aarch64__)
    __asm__ volatile("add %w0, %w1, #2" : "=r"(y) : "r"(x));
#elif defined(__x86_64__)
    __asm__ volatile("leal 2(%1), %0" : "=r"(y) : "r"(x));
#else
    y = 42;
#endif
    return y;
}
