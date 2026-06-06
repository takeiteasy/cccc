# LLVM Backend

JCC has optional LLVM linkage for bytecode-to-LLVM IR backend work. Enable it at
build time with `JCC_HAS_LLVM=1`.

```bash
make JCC_HAS_LLVM=1 LLVM_CONFIG=/opt/homebrew/opt/llvm/bin/llvm-config
```

The LLVM integration is intentionally internal. JCC still compiles C to VM
bytecode and executes it in the built-in VM by default.

For native executables without LLVM, use `-c=native`. This mode runs the normal
JCC frontend, including preprocessing and compile-time macros, serializes the
post-macro C program to a temporary file, and invokes a system compiler. Compiler
selection uses `JCC_NATIVE_CC` when set, otherwise `cc`, `clang`, then `gcc`.
`-o <file>` is required to name the produced executable; the temporary C
source is removed after the build.

```bash
./jcc -c=native -o program program.c
JCC_NATIVE_CC=clang ./jcc -c=native -o program program.c
```

`-c=native` is a stand-in pipeline, not LLVM IR generation. VM bytecode,
debugger, profiling, and runtime safety instrumentation options do not apply to
native mode.
