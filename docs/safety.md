# Memory Safety

Runtime detection of common C bugs, from zero overhead to maximum checking.

```sh
./cccc -2 program.c          # or --safety=standard
```

## Levels

Each level adds checks and cost. **`-2` is the recommended default for development and testing.**

| Level | Flags | Overhead | Use when |
|---|---|---|---|
| 0 none | `-0`, `--safety=none` | about 0% | Validated code that needs full speed |
| 1 basic | `-1`, `--safety=basic` | 5 to 10% | Production with a safety net |
| 2 standard | `-2`, `--safety=standard` | 20 to 40% | Development and testing |
| 3 max | `-3`, `--safety=max` | 60 to 100%+ | Debugging corruption, security-critical code |

| Check | 0 | 1 | 2 | 3 |
|---|---|---|---|---|
| Stack canaries | | yes | yes | yes |
| Heap canaries | | yes | yes | yes |
| Memory leak detection | | yes | yes | yes |
| Integer overflow checks | | yes | yes | yes |
| Format string validation | | yes | yes | yes |
| Pointer sanitizer (bounds, use-after-free, type checks) | | | yes | yes |
| Uninitialized variable detection | | | yes | yes |
| Memory poisoning (`0xCD` allocated, `0xDD` freed) | | | yes | yes |
| Control flow integrity | | | | yes |
| Memory tagging | | | | yes |
| Dangling stack pointers | | | | yes |
| Alignment, provenance, invalid arithmetic | | | | yes |
| Stack instrumentation | | | | yes |
| Random canaries | | | | yes |

The VM heap is on at every level; `-V`/`--no-vm-heap` turns it off ([safety-vm-heap.md](safety-vm-heap.md)). Level 1 catches stack overruns that reach the canary or return address (reported when the function returns); level 3 also catches dereferences of pointers into returned frames, including interior pointers with runtime offsets ([safety-dangling.md](safety-dangling.md)). Checked pointers are opt-in at every level.

### Adding single flags

Levels are additive:

```sh
./cccc -2 --memory-tagging program.c        # level 2 plus temporal tagging
./cccc -1 --control-flow-integrity program.c
./cccc -3 program.c                          # already everything: extra flags are redundant
```

## Smoke-testing before a compile

`--test-run[=LEVEL]` runs the program once under the VM's safety instrumentation and refuses to continue if it crashes or trips a check. `LEVEL` is `none`/`basic`/`standard`/`max` or `0` to `3`, default `max`. Use it as a cheap CI gate before a native build, for example `--test-run=1`, without writing a test suite ([testing-running.md](testing-running.md#--test-runlevel-smoke-test-the-program-itself-before-compiling)).

## Configuring from source

`#pragma cccc config(key = value, ...)` sets safety options for a self-contained file:

```c
#pragma cccc config(safety = 2)
#pragma cccc config(bounds_checks = true, memory_tagging = false)
#pragma cccc config(overflow_checks)     // a bare key means "= true"
```

| Key | Values | Same as |
|---|---|---|
| `safety` | `0` to `3` | `-0` to `-3` |
| `bounds_checks`, `uaf_detection`, `type_checks`, `overflow_checks`, `stack_canaries`, `heap_canaries`, `memory_leak_detection`, `pointer_sanitizer`, `memory_tagging`, `checked_pointers` | `true`/`false` (or `1`/`0`) | `--` followed by the name with `-` |

Several keys can share one pragma and the pragma can repeat; the last write to a key wins.

| Precedence rule | |
|---|---|
| Command-line flags win | An explicit `-N`, `--safety=` or individual flag silently overrides `config(...)` for that setting |
| Native and serialized output | Under `-c=native`, `-m` and `-c=generated`, `config(...)` does not change VM flags (the C is built by `cc`, which has its own sanitizers) and prints a `-Wignored-features` warning. `checked_pointers` is the exception: `-c=native` and `-m` enforce it ([safety-checked-interfaces.md](safety-checked-interfaces.md#native-output)) |
| Unknown keys, out-of-range values | Always hard errors, in every mode |

## Pages

| Page | Covers |
|---|---|
| [safety-memory.md](safety-memory.md) | Canaries, poisoning, leaks, use-after-free, double free |
| [safety-bounds.md](safety-bounds.md) | Runtime array bounds checks |
| [safety-type-checks.md](safety-type-checks.md) | Pointer type checks, uninitialized reads |
| [safety-overflow.md](safety-overflow.md) | Signed integer overflow |
| [safety-threads.md](safety-threads.md) | Thread safety, floating-point traps |
| [safety-dangling.md](safety-dangling.md) | Dangling stack pointers |
| [safety-pointers.md](safety-pointers.md) | Alignment, provenance, tagging, format strings, CFI |
| [safety-checked-pointers.md](safety-checked-pointers.md) | Checked-pointer declarations and bounds |
| [safety-checked-propagation.md](safety-checked-propagation.md) | Bounds following assignments |
| [safety-checked-interfaces.md](safety-checked-interfaces.md) | Annotated parameters, bounds casts |
| [safety-checked-types.md](safety-checked-types.md) | Terminator guard, checked arrays |
| [safety-checked-regions.md](safety-checked-regions.md) | `_Checked` / `_Unchecked` regions |
| [safety-vm-heap.md](safety-vm-heap.md) | The VM heap allocator |
| [safety-ffi.md](safety-ffi.md) | FFI allow/deny lists and type checking |
| [safety-impl.md](safety-impl.md) | Implementation notes for contributors |
