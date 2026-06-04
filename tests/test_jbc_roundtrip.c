// Regression test: round-trip a small program through bytecode save + load
// to verify cc_save_bytecode persists the FFI table and cc_load_bytecode
// restores it (cc_load_libc in main.c resolves func_ptrs by dlsym).
//
// The test is run as: ./jcc tests/test_jbc_roundtrip.c (exits 42).
// It does NOT exercise the .jbc load path on its own — for that, run:
//   ./jcc -o /tmp/jbc_roundtrip.jbc tests/test_jbc_roundtrip.c
//   ./jcc /tmp/jbc_roundtrip.jbc
// The expected exit code is 42 in both cases.

#include <stdio.h>

int main(void) {
    printf("result: jbc-roundtrip-ok\n");
    return 42;
}
