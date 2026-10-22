# Source Map API

Map program counters to source locations, and export maps for IDEs.

## Source Map API

CCCC provides a programmatic API for accessing source location information, which is useful for building custom debugging tools or IDE integrations.

### Symbolizing a PC from User Code

Two builtins compose with `__builtin_return_address` to produce human-readable symbolization from within bytecode programs:

```c
#include <stdio.h>

void print_caller_info(void) {
    // Capture the return address (= a VM bytecode offset, not a host address).
    void *ra = __builtin_return_address(0);

    // Map to the enclosing function name — always available, no -g required.
    const char *fn = __builtin_pc_function_name(ra);

    // Map to file/line — requires -g; gracefully returns NULL/0 without it.
    const char *file = NULL;
    int line = 0;
    __builtin_pc_source_location(ra, &file, &line);

    if (file)
        printf("called from %s at %s:%d\n", fn ? fn : "?", file, line);
    else
        printf("called from %s\n", fn ? fn : "?");
}
```

This pipeline is **VM only**. `__builtin_pc_function_name` and
`__builtin_pc_source_location` both lower to a call into a VM-internal FFI
shim that resolves a VM bytecode offset through the VM's own symbol/source-map
table — neither exists natively, so both are a hard compile error under
`-c=native`/`-m`/`-c=generated`. `__builtin_return_address` alone still
serializes and runs there, mapping to a real host return address instead of a
VM bytecode offset — see [native.md § Serialized-output
divergences](native.md#serialized-output-divergences).

### Embedder C API

The same lookups are available to embedders through `cccc.h`:

```c
// Map a bytecode PC to the enclosing function name.
// Does NOT require -g. Returns NULL if not found.
const char *cc_pc_to_name(VirtualMachine *vm, Pc pc);

// Map a bytecode PC to a source file name and 1-based line number.
// Requires -g. Returns 1 on success (sets *out_file, *out_line);
// returns 0 and zeros the outputs on failure.
int cc_pc_to_source(VirtualMachine *vm, Pc pc, const char **out_file, int *out_line);
```

Example:

```c
// Symbolize the current PC from the embedder side.
const char *name = cc_pc_to_name(&vm, vm.pc);
const char *file = NULL;
int line = 0;
cc_pc_to_source(&vm, vm.pc, &file, &line);
if (name) printf("in %s", name);
if (file) printf(" (%s:%d)", file, line);
putchar('\n');
```

### Getting Source Location (low-level)

```c
// Get source file location for a given PC address (requires -g).
File *file = NULL;
int line_no = 0;
int col_no = 0;

if (cc_get_source_location(&vm, vm.pc, &file, &line_no, &col_no)) {
    printf("At %s:%d:%d\n", file->name, line_no, col_no);
}
```

### Exporting Source Maps

Source maps can be exported to JSON format for use with external tools:

```c
// Export source map to JSON
FILE *f = fopen("sourcemap.json", "w");
cc_output_source_map_json(&vm, f);
fclose(f);
```

The JSON output includes:
- **pc**: Bytecode offset
- **file**: Source file name
- **line**: Line number (1-based)
- **col**: Starting column number (1-based, UTF-8 aware)
- **end_col**: Ending column number (1-based)

Example output:
```json
{
  "version": 3,
  "mappings": [
    {"pc": 8, "file": "test.c", "line": 3, "col": 5, "end_col": 8},
    {"pc": 16, "file": "test.c", "line": 4, "col": 9, "end_col": 14}
  ]
}
```

### Generated Code

Code produced by a comptime macro, an `@attr` handler, or file-scope
generation carries its own synthetic file/line (e.g. `<cccc macro:
label>:1:1` for `SyntheticToken`, or the macro's call-site location by
default) rather than a location in the real source file — there is no
line-for-line mapping back to a template the way a preprocessor macro
expansion has one. What generated code does carry is an **expansion
origin**: the chain of macro calls / attribute handlers / file-scope
generation that produced it, walked by the diagnostic printer to render
the `note: in expansion of ...` chain described in
[macros.md § Expansion backtrace](macros-debugging.md#expansion-backtrace). This is
compiler-diagnostics plumbing, not part of the source-map/PC-symbolization
API above — it isn't queryable through `cc_pc_to_source`/
`cc_get_source_location`, only surfaced on a compile error or warning.

### Instruction Encoding

The VM text segment uses 32-bit instruction words. Operands that need 64 bits,
such as C integer immediates and text/data byte offsets, are encoded as two
consecutive 32-bit words. Direct branch and call targets are instruction
indexes, while C-visible function and label values are byte offsets from the
text segment base.

Floating-point bytecode uses a tagged register file. `FLDR`/`FSTR` load and
store `double` values tagged as f64, while `FLDR_F32`/`FSTR_F32` load and store
C `float` values tagged as f32. Arithmetic and comparison opcodes without a
suffix operate as f64; `_F32` opcodes operate in f32 precision. `FROUND_F32`
converts a floating register to f32 and tags the destination as f32. Raw-bit
moves are typed: `FR2R`/`R2FR` move f64 payloads, and `FR2R_F32`/`R2FR_F32`
move f32 payloads.
