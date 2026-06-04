# LLVM Backend

> TODO: This is a stub, the LLVM backend is not yet implemented. Only the build system integration is available at the moment.

JCC has optional LLVM linkage for bytecode-to-LLVM IR backend work. Enable it at
build time with `JCC_HAS_LLVM=1`.

```bash
make JCC_HAS_LLVM=1 LLVM_CONFIG=/opt/homebrew/opt/llvm/bin/llvm-config
```

The current integration is intentionally internal. JCC still compiles C to VM
bytecode and executes it in the built-in VM. LLVM IR generation, object output,
and native AOT output are future work.
