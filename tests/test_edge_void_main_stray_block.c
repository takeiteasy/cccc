// CCCC_EXPECT_STDOUT: hello world
// Tests: non-standard "void main() void;" forward declaration and a
// free-standing "void; { ... }" block that acts as the function body.
// Godbolt/GCC exits 12 (UB from void main), CCCC exits 0 — both are valid.
#include <stdio.h>

void main() void;

void; {
  puts("hello world");
}
