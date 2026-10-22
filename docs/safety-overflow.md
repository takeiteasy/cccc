# Integer Overflow Detection

`--overflow-checks` traps signed integer overflow and division faults. It is in `-1` and above; off otherwise, with no overhead.

| Operation | Trap when | Report |
|---|---|---|
| `+`, `-` | The result leaves `LLONG_MIN`..`LLONG_MAX` | `INTEGER OVERFLOW`, with operands |
| `*` | The product overflows | `INTEGER OVERFLOW` |
| `/` | The divisor is zero | `DIVISION BY ZERO` |
| `/` | `LLONG_MIN / -1` | `INTEGER OVERFLOW` |

Floating-point operations are not affected. The checks use dedicated opcodes (`ADDC`, `SUBC`, `MULC`, `DIVC`) that run instead of the plain ones.

```c
#include "limits.h"

int main() {
    long long x = LLONG_MAX;
    long long result = x + 1;   // overflow
    return 42;
}
```

```
$ ./cccc --overflow-checks test_overflow_add.c

========== INTEGER OVERFLOW ==========
Addition overflow detected
Operands: 9223372036854775807 + 1
PC:       0x94e8000a0 (offset: 20)
======================================
```

Other reports:

```
Subtraction overflow detected
Operands: -9223372036854775808 - 1

Multiplication overflow detected
Operands: 9223372036854775807 * 2

========== DIVISION BY ZERO ==========
Attempted division by zero
Operands: 42 / 0

Division overflow detected
Operands: -9223372036854775808 / -1
Result would overflow (LLONG_MIN / -1 = LLONG_MAX + 1)
```

Code that does not overflow runs normally and keeps its exit code.
