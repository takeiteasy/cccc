// CCCC_NATIVE_SKIP: asm runs natively, so there is no VM -Wignored-features warning
// CCCC_FLAGS: -Wignored-features
// CCCC_EXPECT_STDERR: file-scope asm is not executed by the VM
//
// The VM cannot run host assembly, so file-scope asm is a no-op that says so
// under -Wignored-features.

__asm__(".p2align 2");

int main(void) {
    return 42;
}
