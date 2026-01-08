# jcc

> **WARNING**: This is a work in progress and shouldn't be used for any serious work

`JCC` is a **J**IT **C**11 **C**ompiler. The preprocessor/lexer/parser is built from [chibicc](http://https://github.com/rui314/chibicc) and the VM was initially built from [c4](https://github.com/rswier/c4) and [write-a-C-interpreter](https://github.com/lotabout/write-a-C-interpreter).

## Features

- GDB-like debugger (see [DEBUGGER.md](./DEBUGGER.md))
  - Export source maps as JSON `cc_output_source_map_json`
- Tonnes of memory safety features (see [SAFETY.md](./SAFETY.md))
  - Includes preset safety levels, `-0, -1, -2, 3` (0 is no safety, and is default)
  - Safety features come with an overhead
- Bytecode optimization passes (see [OPTIMIZATION.md](./OPTIMIZATION.md))
  - `--optimize[=LEVEL]` with levels 0-3 (disabled by default)
  - Constant folding, peephole optimization, dead code elimination
- Optional libcurl integration, include headers from URL
  - `#include <https://raw.githubusercontent.com/user/repo/main/header.h>`
  - Build with `make JCC_HAS_CURL=1`
- Output JSON files containing all function, struct, union, enums, and globals definitions
  - Useful for generating wrapper for FFI
  - `./jcc --json -o lib.json lib.h`
- Built in memory allocation using `--vm-heap` (enabled by some safety features)
  - All `malloc/calloc/realloc/free` calls are routed through an internal allocator
  - No need to use standard library for heap allocations


## Core C Language Support

### Operators

- Arithmetic: `+`, `-`, `*`, `/`, `%`, unary `-`
- Bitwise: `&`, `|`, `^`, `~`, `<<`, `>>`
- Comparison: `==`, `!=`, `<`, `>`, `<=`, `>=`
- Logical: `&&`, `||`, `!`
- Assignment: `=`, `+=`, `-=`, `*=`, `/=`, `%=`, `&=`, `|=`, `^=`, `<<=`, `>>=`
- Increment/decrement: `++`, `--` (both prefix and postfix)
- Ternary: `? :`
- Comma: `,`

### Control Flow

- `if`, `else`
- `for`, `while`, `do-while`
- `switch`, `case`, `default`
- `goto label`, `goto *expr;`, `&&label`
- `break`, `continue`

### Functions

- Function declarations and definitions
- Function calls (direct and indirect via function pointers)
- Recursion
- Parameters and return values
- Function pointers (declaration, assignment, indirect calls)
- Variadic functions (`<stdarg.h>` support with `va_start`, `va_arg`, `va_end`)

### Data Types

- Basic types: `void`, `char`, `short`, `int`, `long`, `float`, `double`, `_Bool`
- Pointers (declaration, dereference `*`, address-of `&`, pointer arithmetic)
- Arrays (fixed-size, initialization, multidimensional, indexing)
- Array decay (in function parameters and expressions)
- Zero-length arrays (`int arr[0];` GNU extension for flexible array members)
- Strings (literals with data segment storage)
- Structs (declaration, member access `.`, nested structs, initialization, flexible array members, anonymous structs/unions)
- Bitfields (bit-level struct member access with `: width` syntax, signed/unsigned, read-modify-write operations)
- Unions (declaration, member access, initialization, anonymous unions)
- Enums (declaration, explicit values, in expressions and switches)
- Variable-length arrays (VLA with runtime allocation via VM heap)

### Storage Classes & Qualifiers

- `static` (static locals, static globals, static functions)
- `extern` (external linkage for multi-file projects)
- `const` (const-correctness in type system and codegen)
- `inline` (parsed and generated normally, behaves like static, no special optimization)
- `volatile` (tracked in type system, prevents optimization of accesses)
- `register` (accepted and ***ignored***, no special optimization)
- `restrict` (accepted and ***ignored***, aliasing not tracked)

### Other Features

- `typedef` (all types including function pointers)
- `sizeof` operator (compile-time evaluation for all types)
- Forward declarations (incomplete types, self-referential structs)
- Floating-point arithmetic (double precision with full operations)
- Compound literals (scalars, arrays, structs)
- Designated initializers (arrays and structs)
- Cast expressions (explicit type conversions)

### Preprocessor

- `#include` (with intelligent search path handling)
  - **Standard library headers** (`<stdio.h>`, `<stdlib.h>`, etc.) automatically use JCC's `./include` directory
  - **User headers** with `"quotes"` search `-I` paths
  - **Non-standard angle-bracket headers** require `--isystem` paths (system headers not searched by default)
  - Full paths work directly
  - No `-I./include` flag needed - added automatically
- `#define` (object-like and function-like macros)
- `#ifdef`, `#ifndef`, `#if`, `#elif`, `#else`, `#endif`
- `#elifdef`, `#elifndef`
- `#undef`
- `#pragma once`
- `#error`, `#warning`
- Macro expansion and stringification
- `__VA_OPT__` for conditional variadic macro expansion (C23)
- `#embed` - Binary resource inclusion (C23)
  - Embeds binary files as comma-separated integer literals (0-255)
  - `#embed "file.bin"` - Quoted form (searches include paths)
  - `#embed <file.bin>` - Angle bracket form (searches system paths)
  - `limit(N)` parameter - Restricts number of bytes embedded
  - `__has_embed("file")` - Check file availability (returns 0=not found, 1=non-empty, 2=empty)
  - Files >10MB trigger warnings by default (50MB for secondary warning)
  - `--embed-limit=SIZE` - Customize warning threshold (e.g., `--embed-limit=100MB`)
  - `--embed-hard-limit` - Make limit violations hard errors instead of warnings
  - `prefix(...)`, `suffix(...)` - Pre/append parameters to data
  - `if_empty(...)` - Handle empty resources
- String literal concatenation (adjacent strings automatically combined)

### Linker

- Multiple input files
- Symbol resolution
- Duplicate definition detection

### Foreign Function Interface (FFI)

JCC provides two modes for variadic foreign functions:

- **Native Inline Assembly**
  - **AAPCS64** (macOS ARM64)
  - TODO: x86_64 (System V AMD64)
  - TODO: x86_64 (Windows x64)
  - TODO: x86_32 (Legacy 32-bit System V)
- **Optional libffi support** (Other platforms)
  - Build with `make JCC_HAS_FFI=1` to enable libffi support (see [BUILD](#build))
  - Use libffi if your platform isn't supported

### Inline Assembly

- `asm("...")` statements via callback mechanism
- Not executed in the VM (no-op by default)
- Callback can emit custom bytecode or perform logging

### C11 Features

- Universal character names `\uXXXX`, `\UXXXXXXXX` - Unicode escapes in strings
- `_Generic` type-generic expressions (compile-time type selection)
- `_Alignof` operator (query type/variable alignment)
- `_Alignas` specifier (control variable alignment)
- `static_assert` and `_Static_assert` (compile-time assertions with custom error messages)
- `Bitfields` - Bit-level struct member access (e.g., `unsigned int flags : 3;`)
- `Anonymous structs/unions` - Direct member access without intermediate name
- Variable length arrays `int arr[] = { 1, 2, 3 };`

### C23 Features

- `typeof` operator (compile-time type inquiry for variables and expressions)
- `typeof_unqual` - typeof that removes type qualifiers
- `[[...]]` - C23 attributes, like GNU attributes, are parsed but ***ignored***
- Binary literals (0b10101010)
- Digit separators with single quotes (1'000'000)
- `#embed` directive (C23) - Binary resource inclusion

### GNU Extensions

- Statement expressions `({...})`
- `__attribute__((...))` for functions and variables (currently ***ignored***, parsed only)
- Labels as values `&&label` - Get the address of a label for computed goto
- Computed goto `goto *expr;` - Jump to an address computed at runtime
- Switch case ranges `case 1 ... 5:` - Match ranges of values in switch statements
- Zero-length arrays `int arr[0];` - Flexible array member alternative
- Nested functions - Define functions inside other functions with access to parent variables

### Thread Support

> Not supported (yet)

JCC is single-threaded and does not implement any threading or atomic operations yet. Registering `pthread` as a dynamic library amd loading the functions may work but is untested.

## Standard Library Support

JCC includes embedded standard library headers that are compiled directly into the binary:
- `stdio.h`, `stdlib.h`, `string.h`, `math.h`, `time.h`, `ctype.h`, etc.
- `stdarg.h`, `setjmp.h` - JCC-specific implementations for VM calling convention

Headers are embedded using `std.py` which generates `src/std.c`. This means:
- No external header files needed at runtime
- Headers are read from strings instead of disk
- Consistent behavior across platforms

To regenerate embedded headers after modifying files in `include/`:
```bash
python3 std.py
make
```

> **Note**: The `--use-system-headers` flag is available but experimental. Apple's system headers contain macros and runtime dependencies that JCC cannot fully support.

## TODO

- Support for `_Defer` statement
- Support for `_Pragma` statement
- Support for (some) C23 `[[...]]` and GNU `__attribute__((...))` attributes
- Proper inlining support
- Support more architectures for native FFI
  - x86_64 (System V + Windows) + x86_64 (legacy System V only)
  - No plans for any other systems, but will accept patches
- Support for pthread + internal thread support
  - Thread safety features (race condition checks, etc)

## Building

```bash
make jcc        # Build jcc compiler
make all        # Build everything (jcc, libjcc.dylib) and run tests

# Optional: Build with libffi support for true variadic foreign functions
make JCC_HAS_FFI=1

# Optional: Build with libcurl support for URL header imports
make JCC_HAS_CURL=1
```

This produces:
- `jcc` - Full compiler executable (C source → bytecode → execute)
- `libjcc.dylib` - Shared library for embedding

### Compile to Bytecode

```bash
# Compile C source to bytecode file
./jcc -o program.bin program.c

# With multiple files
./jcc -o app.bin main.c utils.c helpers.c

# With preprocessor flags
./jcc -I./include -DDEBUG -o debug.bin main.c
```

### Running Tests

All test files are located in the `tests/` directory. To run the complete test suite:

```bash
./run_tests
```

This will run all test files and report:
- Total number of tests
- Number of passed tests
- Number of failed tests
- List of any failed tests

**Test runner options:**

```bash
# Run only tests matching a pattern
./run_tests --match "*embed*"     # Runs test_embed_basic.c, test_embed_empty.c, etc.
./run_tests --match "test_ptr*"   # Runs test_ptr_arithmetic.c, test_ptr_deref.c, etc.

# Enable memory leak detection (platform-specific: leaks on macOS, valgrind on Linux, drmemory on Windows)
./run_tests --leaks

# Pass additional flags to jcc (all non-option arguments are forwarded)
./run_tests -2                    # Run tests with standard safety level
./run_tests --vm-heap             # Run tests with VM heap enabled
```

Individual tests can be run directly:

```bash
./jcc tests/test_simple.c
echo $status  # Check exit code, should be 42
```

## LICENSE
```
jcc

Copyright (C) 2025 George Watson

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <https://www.gnu.org/licenses/>.
```
