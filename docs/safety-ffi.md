# FFI Safety

Controlling and validating calls into host libraries.

The allow/deny/disable policy applies to registered FFI calls and runtime
native symbols returned by VM-managed `dlsym`.

Comptime code has its own capability classes on top of these flags; see
[comptime-policy.md](comptime-policy.md).


## `--ffi-allow=func1,func2` — FFI function whitelist

- Comma-separated list of allowed FFI function names
- When allow list is non-empty, only listed functions can be called via FFI
- Enforced at runtime during registered FFI and runtime `dlsym` calls
- Use with `cc_ffi_allow()` API for programmatic configuration

### Example

```c
// test_ffi_allow.c - FFI allow list example

int printf0(const char *fmt);
void *malloc(unsigned long size);

int main() {
    printf0("This call is allowed\n");
    malloc(100);  // This will be blocked
    return 0;
}
```

```bash
$ ./cccc --ffi-allow=printf0 test_ffi_allow.c

This call is allowed

========== FFI SAFETY ERROR ==========
Error type: FFI Access Denied
Function:   malloc
Details:    Function not in allow list
PC offset:  25
======================================
```

## `--ffi-deny=func1,func2` — FFI function blacklist

- Comma-separated list of denied FFI function names
- Prevents specific functions from being called via FFI
- Only checked when allow list is empty
- Use with `cc_ffi_deny()` API for programmatic configuration

### Example

```c
// test_ffi_deny.c - FFI deny list example

int printf0(const char *fmt);

int main() {
    printf0("Attempting to call blocked function\n");
    return 0;
}
```

```bash
$ ./cccc --ffi-deny=printf0 test_ffi_deny.c

========== FFI SAFETY ERROR ==========
Error type: FFI Access Denied
Function:   printf0
Details:    Function in deny list
PC offset:  12
======================================
```

## `--disable-ffi` — Disable all FFI calls

- Completely blocks all foreign function calls at runtime
- `dlopen` returns `NULL` and `dlerror()` reports the flag
- Overrides both allow and deny lists
- Compiler builtins used by `[[cccc::comptime]]` code stay callable
- Not a sandbox: see [security.md](security.md) for the whole model and `--sandbox`

### Example

```c
// test_disable_ffi.c - Disable all FFI calls

int printf0(const char *fmt);

int main() {
    printf0("This FFI call will be blocked\n");
    return 0;
}
```

```bash
$ ./cccc --disable-ffi test_disable_ffi.c

========== FFI SAFETY ERROR ==========
Error type: FFI Disabled
Function:   printf0
Details:    All FFI calls are disabled via --disable-ffi
PC offset:  12
======================================
```

## `--ffi-errors-fatal` — Make FFI errors abort execution

- By default, FFI safety violations print diagnostics, skip the call, zero
  the native return registers, and continue
- With this flag, violations abort execution as runtime errors
- Provides strict enforcement mode for production environments

### Example

```bash
# Default behavior: warnings only
$ ./cccc --ffi-deny=printf0 test.c
<FFI error printed, program continues, returns 0>

# Fatal mode: abort on error
$ ./cccc --ffi-deny=printf0 --ffi-errors-fatal test.c
<FFI error printed, program aborts as a runtime error>
```

## `--ffi-type-checking` — Runtime type validation on FFI calls

- Validates argument counts for registered FFI function signatures
- Requires exact arity for non-variadic functions
- Requires at least the fixed argument count for variadic functions
- Runtime `dlsym` calls are policy-checked but do not have registered
  signatures for arity checking yet


### Examples

```c
// test_ffi_arg_count.c - Wrong number of arguments
int strcmp(const char *s1, const char *s2);

int main() {
    // strcmp expects 2 arguments, but only 1 provided
    int result = strcmp("hello");
    return result;
}
```

```bash
$ ./cccc --ffi-type-checking test_ffi_arg_count.c

error: FFI function 'strcmp': argument count mismatch (requires 2, called with 1)
```

```c
// test_ffi_type_mismatch.c - Wrong argument type
unsigned long strlen(const char *s);

int main() {
    int not_a_string = 42;
    // strlen expects char*, but we're passing int
    unsigned long len = strlen(not_a_string);
    return len;
}
```

```bash
$ ./cccc --ffi-type-checking test_ffi_type_mismatch.c

test_ffi_type_mismatch.c:7: FFI function 'strlen': argument 1 type mismatch
  Expected: char*
  Actual:   int
    unsigned long len = strlen(not_a_string);
                               ^
```

```c
// test_ffi_valid.c - All types match correctly
void *malloc(unsigned long size);
void free(void *ptr);
int strcmp(const char *s1, const char *s2);

int main() {
    void *ptr = malloc(100);  // OK: unsigned long argument
    int cmp = strcmp("a", "b");  // OK: two char* arguments
    free(ptr);  // OK: void* argument
    return 0;
}
```

```bash
$ ./cccc --ffi-type-checking test_ffi_valid.c
<Program compiles and runs successfully>
```
