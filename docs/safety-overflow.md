# Integer Overflow Detection

Signed overflow checks.

## `--overflow-checks` — Signed integer overflow detection

- Detects arithmetic overflow for addition, subtraction, multiplication, and division
- Emits checked opcodes (ADDC, SUBC, MULC, DIVC) when enabled
- ADDC/SUBC: Validates result stays within LLONG_MIN to LLONG_MAX range
- MULC: Uses `__builtin_mul_overflow` to detect multiplication overflow
- DIVC: Detects division by zero and signed overflow (LLONG_MIN / -1)
- Reports overflow with operands, operation type, and PC offset
- Zero overhead when disabled (uses regular ADD/SUB/MUL/DIV opcodes)
- Does not affect floating-point operations

### Addition Overflow
```c
// test_overflow_add.c - Addition overflow example
#include "limits.h"

int main() {
    // This will overflow: LLONG_MAX + 1
    long long x = LLONG_MAX;
    long long result = x + 1;  // Overflow!
    return 42;
}
```

```bash
$ ./cccc --overflow-checks -I./include test_overflow_add.c

========== INTEGER OVERFLOW ==========
Addition overflow detected
Operands: 9223372036854775807 + 1
PC:       0x94e8000a0 (offset: 20)
======================================
```

### Subtraction Underflow
```c
// test_overflow_sub.c - Subtraction underflow example
#include "limits.h"

int main() {
    // This will underflow: LLONG_MIN - 1
    long long x = LLONG_MIN;
    long long result = x - 1;  // Underflow!
    return 42;
}
```

```bash
$ ./cccc --overflow-checks -I./include test_overflow_sub.c

========== INTEGER OVERFLOW ==========
Subtraction overflow detected
Operands: -9223372036854775808 - 1
PC:       0x8948000e8 (offset: 29)
======================================
```

### Multiplication Overflow
```c
// test_overflow_mul.c - Multiplication overflow example
#include "limits.h"

int main() {
    // This will overflow: LLONG_MAX * 2
    long long x = LLONG_MAX;
    long long result = x * 2;  // Overflow!
    return 42;
}
```

```bash
$ ./cccc --overflow-checks -I./include test_overflow_mul.c

========== INTEGER OVERFLOW ==========
Multiplication overflow detected
Operands: 9223372036854775807 * 2
PC:       0xb348000a0 (offset: 20)
======================================
```

### Division By Zero
```c
// test_overflow_div.c - Division by zero example

int main() {
    int x = 42;
    int y = 0;
    int result = x / y;  // Division by zero!
    return 42;
}
```

```bash
$ ./cccc --overflow-checks test_overflow_div.c

========== DIVISION BY ZERO ==========
Attempted division by zero
Operands: 42 / 0
PC:       0x9b28000d0 (offset: 26)
======================================
```

### Signed Division Overflow
```c
// test_overflow_div_signed.c - Signed division overflow
#include "limits.h"

int main() {
    // This will overflow: LLONG_MIN / -1 = LLONG_MAX + 1 (unrepresentable)
    long long x = LLONG_MIN;
    long long result = x / -1;  // Signed division overflow!
    return 42;
}
```

```bash
$ ./cccc --overflow-checks -I./include test_overflow_div_signed.c

========== INTEGER OVERFLOW ==========
Division overflow detected
Operands: -9223372036854775808 / -1
Result would overflow (LLONG_MIN / -1 = LLONG_MAX + 1)
PC:       0x81a800108 (offset: 33)
======================================
```

### Normal Arithmetic (No Overflow)
```c
// test_overflow_none.c - Valid arithmetic with overflow checks enabled

int main() {
    // Normal arithmetic operations that don't overflow
    int a = 10 + 20;      // 30
    int b = 50 - 20;      // 30
    int c = 6 * 7;        // 42
    int d = 84 / 2;       // 42

    // All checks pass, program continues normally
    return 42;
}
```

```bash
$ ./cccc --overflow-checks test_overflow_none.c
$ echo $?
42
```

**Note:** Overflow detection is **disabled by default** for zero overhead. When enabled with `--overflow-checks`, codegen emits specialized checked arithmetic opcodes (ADDC, SUBC, MULC, DIVC) that validate operations before completing them. Floating-point operations are not affected by this flag.
