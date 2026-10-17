// CCCC_FLAGS: --asm-passthru --comptime-allow=all
//
// Extended asm inside a comptime function runs on the host while the program
// is still being compiled, so its thunk is built before the macro executes.

@comptime Node *forty_two(void) {
    int in = 41, out = 0;
#if defined(__aarch64__)
    __asm__ volatile("add %w0, %w1, #1" : "=r"(out) : "r"(in));
#elif defined(__x86_64__)
    __asm__ volatile("leal 1(%1), %0" : "=r"(out) : "r"(in));
#endif
    return MakeIntLiteral(out);
}

int main(void) { return forty_two(); }
