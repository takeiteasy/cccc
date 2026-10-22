# Type and Initialization Checks

Detect type confusion through pointers, and reads of uninitialized locals.

## `--type-checks`

Tracks an effective type per heap byte, following the C11 effective-type model. A store establishes the type of the bytes it writes; a load is checked against it.

```c
int main() {
    int *int_ptr = (int *)malloc(sizeof(int) * 10);
    int_ptr[0] = 42;

    float *float_ptr = (float *)int_ptr;
    float value = *float_ptr;   // type mismatch
    return (int)value;
}
```

```
$ ./cccc --type-checks test_type_check.c

========== TYPE MISMATCH DETECTED ==========
Pointer type mismatch on dereference
Address:       0x7f8640028
Expected type: float
Actual type:   int
Allocated at PC offset: 15
Current PC:    0x7f84002d8 (offset: 52)
============================================
```

| Behaviour | Detail |
|---|---|
| Granularity | Any offset into an allocation; each struct member or array element has its own type |
| `char` access | Always legal: a `char` load is never checked, a `char` store clears the range |
| Union members | Never checked; a store clears the range, so legal punning is not flagged |
| `void *` and generic pointers | Not checked |
| Reusing a buffer as another type | Fine: the next store re-establishes the type |
| `memcpy` / `memmove` | Carry the source type to the destination |
| `realloc` | Carries the old block's types to the new block |
| `qsort` | Keeps coverage when every element has the same pattern; otherwise clears the sorted range |
| Other host functions | Clear the affected shadow before running[^clear] |
| Coverage | Heap and globals; stack subobjects are not tracked |

[^clear]: The VM cannot see what an unclassified host call writes, so it clears first. This trades a false negative for no false positives. Common libc functions are classified to avoid the wipe: read-only ones (`strlen`, `strcmp`, `memcmp`, `fwrite`) clear nothing, and bounded writers (`fread`, `snprintf`, `read`, `strncpy`, the `strto*` family) clear only the written extent. `printf`-family calls clear only the output buffer unless the format may contain `%n`. `scanf`-family and `va_list` variants are unclassified. See [safety-impl.md](safety-impl.md#type-shadow).

## `--uninitialized-detection`

Traps a read of a local that was never assigned.

```c
int main() {
    int x;
    int y = 10;
    return x + y;   // reads uninitialized x
}
```

```
$ ./cccc --uninitialized-detection test_uninit.c

========== UNINITIALIZED VARIABLE READ ==========
Attempted to read uninitialized variable
Stack offset: -1
Address:      0x7ffee4b3f8
BP:           0x7ffee4b400
PC:           0x7ffe400120 (offset: 32)
================================================
```

Locals written by a mechanism the detector cannot see are exempt:

| Exempt local | Why |
|---|---|
| Struct, union, array, wide `_BitInt`, `_Decimal` | Written piecewise |
| Any local whose address is taken (`&var`, `&arr[i]`, `&s.field`) | The usual out-parameter idiom (`void fill(int *out)`) |
| Captured by a nested function | Written from another frame |
| `__block` local written inside a block literal | Same |

An uninitialized local that has its address taken is therefore no longer caught.
