// File-scope basic asm is accepted at every spelling. The VM ignores it; the
// serializers pass it to the host assembler verbatim.

asm(".p2align 2");
__asm(".p2align 2");
__asm__(".p2align 2\n"
        ".p2align 2");

int main(void) {
    return 42;
}
