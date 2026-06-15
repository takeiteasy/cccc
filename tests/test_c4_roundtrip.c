// Regression test: round-trip a small program through bytecode save + load
// to verify cc_save_bytecode persists the FFI table and cc_load_bytecode
// restores it (cc_load_libc in main.c resolves func_ptrs by dlsym).
//
// The test is run as: ./cccc tests/test_c4_roundtrip.c (exits 42).
// It does NOT exercise the .c4 load path on its own — for that, run:
//   ./cccc -o /tmp/c4_roundtrip.c4 tests/test_c4_roundtrip.c
//   ./cccc /tmp/c4_roundtrip.c4
// The expected exit code is 42 in both cases.

#include <stdio.h>

int main(void) {
    printf("result: c4-roundtrip-ok\n");
    return 42;
}
