// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: __asm__ __volatile__ \("mov %w0, %w1" : \[dst\] "=r" \(y\) : "r" \(x\) : "memory", "cc"\);
// CCCC_EXPECT_STDOUT: __asm__\("" : : : "memory"\);
// CCCC_EXPECT_STDOUT: __asm__ __inline__ \(""\);
// CCCC_EXPECT_STDOUT: __asm__ goto \("" : : : "memory" : out, again\);
// CCCC_REJECT_STDOUT: unsupported expr kind
//
// Extended asm round-trips verbatim: qualifiers, operand names, constraints,
// clobbers and goto labels. An extended statement with no operands stays
// extended, because `%%` means something different in basic asm.

int main(void) {
    int x = 1, y = 0;
    __asm__ volatile("mov %w0, %w1" : [dst] "=r"(y) : "r"(x) : "memory", "cc");
    __asm__("" ::: "memory");
    asm inline("");
    __asm__ goto("" ::: "memory" : out, again);
again:
out:
    return 42;
}
