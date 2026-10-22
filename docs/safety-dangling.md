# Dangling Pointer Detection

`--dangling-pointers` (also on at `-3`) traps a dereference of a pointer into a stack frame that has already returned.

## What it catches

| Case | Example | Caught |
|---|---|---|
| Returned address, used by the caller | `int *p = get_local(); return *p;` | Yes |
| Used one or more calls deeper | `use(p)` where `use` dereferences `p` | Yes |
| Interior pointer with a runtime offset | `&arr[i]` returned and dereferenced | Yes |
| Result unused | `*p;`, `(void)*p` | Yes: the access is checked, not the value |
| Block (not function) scope exited, frame alive | `{ int x; p = &x; } *p` | No: see [safety-pointers.md](safety-pointers.md) (`--stack-instrumentation`) |
| Own local array or struct member in a live frame | `buf[i]`, `s.arr[i]`, `m[i][j]` | Never flagged |
| Array whose address was taken | `char *q = buf; q[i]` | Not flagged while the frame lives |

## Examples

```c
int *get_local_address() {
    int x = 42;
    return &x;          // fine to take, fine to return
}

int main() {
    int *ptr = get_local_address();
    return *ptr;        // caught here
}
```

```
$ ./cccc --dangling-pointers test_dangling_pointer.c

========== DANGLING STACK POINTER ==========
Dereferenced a pointer into a stack frame that has already returned
Address:    0x15fffffc8
Current SP: 0x15fffffe0 (address is below live stack -> frame is dead)
Current PC: 0x14 (offset: 20)
============================================
```

Passing the pointer deeper reuses the dead frame's memory, so the address looks live again; it is still caught:

```c
int *get_local(void) { int x = 42; return &x; }
void use(int *p)     { int y = *p; (void)y; }   // one frame deeper

int main(void) { use(get_local()); return 0; }
```

```
Dereferenced a pointer into a stack frame that has already returned
Address:    0x31fffffc8
Creating frame's epoch is no longer live (dereferenced through a deeper call)
```

The same applies to an interior pointer such as `&arr[i]` with a non-constant `i`:

```c
int *get_local(int i) { int arr[8]; arr[i] = 42; return &arr[i]; }
void use(int *p)      { int pad[8]; pad[0] = 0; int y = *p; (void)y; (void)pad; }
```

## How it decides

| Layer | Check |
|---|---|
| Range | The address lies in the stack but below the stack pointer, so its frame has returned |
| Frame liveness | The frame that created the address is no longer live, even if the address looks live again |
| Interior ranges | An address inside a retained array or struct extent whose frame is dead, with no live extent covering it |

A live frame's own extent always wins over a stale one at the same address, so reused stack memory is not misreported.[^cost] `__builtin_dynamic_object_size` uses the same bookkeeping and enables it by itself ([stdlib.md](stdlib.md)).

[^cost]: Recording is pruned for addresses that provably never escape their function, and frames are only tracked in functions that need it, so the cost falls on the escaping case. Internals: [safety-impl.md](safety-impl.md#dangling-pointers).
