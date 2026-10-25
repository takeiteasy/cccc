# Integer Overflow Detection

`--overflow-checks` traps signed integer overflow and division faults. It is in `-1` and above; off otherwise, with no overhead.

| Operation | Trap when | Report |
|---|---|---|
| `+`, `-`, `*`, unary `-` | The result leaves the range of the operation's signed type (`int`, `long long`, `_BitInt(N)` up to 64 bits) | `INTEGER OVERFLOW`, with operands and width |
| `/` | The divisor is zero | `DIVISION BY ZERO` |
| `/` | The type's minimum divided by `-1` | `INTEGER OVERFLOW` |

`++`, `--` and compound assignments (`+=`, `-=`, `*=`) are checked the same way. Unsigned arithmetic and floating-point operations are not affected. The checks use dedicated opcodes (`ADDC`, `SUBC`, `MULC`, `DIVC`) that run instead of the plain ones.

```c
#include "limits.h"

int main() {
    volatile int x = INT_MAX;
    int result = x + 1;   // overflow
    return 42;
}
```

```
$ ./cccc --overflow-checks test_overflow_add.c

========== INTEGER OVERFLOW ==========
Addition overflow detected
Operands: 2147483647 + 1 (32-bit)
PC:       0x94e8000a0 (offset: 20)
======================================
```

Other reports:

```
Subtraction overflow detected
Operands: -2147483648 - 1 (32-bit)

Multiplication overflow detected
Operands: 9223372036854775807 * 2 (64-bit)

========== DIVISION BY ZERO ==========
Attempted division by zero
Operands: 42 / 0

Division overflow detected
Operands: -2147483648 / -1 (32-bit)
```

Code that does not overflow runs normally and keeps its exit code.
