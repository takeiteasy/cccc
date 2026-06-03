# LLVM Backend

JCC has optional LLVM linkage for bytecode-to-LLVM IR backend work. Enable it at
build time with `JCC_HAS_LLVM=1`.

```bash
make JCC_HAS_LLVM=1 LLVM_CONFIG=/opt/homebrew/opt/llvm/bin/llvm-config
make JCC_HAS_LLVM=1 LLVM_CONFIG=/opt/homebrew/opt/llvm/bin/llvm-config llvm-smoke
```

The current integration is intentionally internal: it initializes LLVM through
the C API and verifies that contexts, modules, and builders can be created and
destroyed. JCC still compiles C to VM bytecode and executes it in the built-in
VM. LLVM IR generation, object output, and native AOT output are future work.
