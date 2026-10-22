# Source Map API

Map program counters to source locations, and export maps for IDEs and tools.

| Need | Use |
|---|---|
| Symbolize a PC from a bytecode program | `__builtin_pc_function_name`, `__builtin_pc_source_location` |
| Symbolize a PC from an embedder | `cc_pc_to_name`, `cc_pc_to_source` |
| Raw file, line and column | `cc_get_source_location` |
| Export everything as JSON | `cc_output_source_map_json` |

File and line need `-g`; a function name does not.

## From a program

Both builtins compose with `__builtin_return_address`:

```c
#include <stdio.h>

void print_caller_info(void) {
    void *ra = __builtin_return_address(0);          // a VM bytecode offset, not a host address
    const char *fn = __builtin_pc_function_name(ra); // no -g needed

    const char *file = NULL;
    int line = 0;
    __builtin_pc_source_location(ra, &file, &line);  // needs -g; NULL/0 without it

    if (file) printf("called from %s at %s:%d\n", fn ? fn : "?", file, line);
    else      printf("called from %s\n", fn ? fn : "?");
}
```

The two `__builtin_pc_*` builtins are VM only: both are a compile error under `-c=native`, `-m` and `-c=generated`. `__builtin_return_address` alone still works there, returning a real host address ([native-differences.md](native-differences.md#behaviour-differences)).

## From an embedder

```c
// Function name for a PC; no -g needed; NULL if not found.
const char *cc_pc_to_name(VirtualMachine *vm, Pc pc);

// File and 1-based line; needs -g. Returns 1 on success, else 0 and zeroed outputs.
int cc_pc_to_source(VirtualMachine *vm, Pc pc, const char **out_file, int *out_line);

const char *name = cc_pc_to_name(&vm, vm.pc);
const char *file = NULL;
int line = 0;
cc_pc_to_source(&vm, vm.pc, &file, &line);
```

The low-level form also gives the column (needs `-g`):

```c
File *file = NULL;
int line_no = 0, col_no = 0;
if (cc_get_source_location(&vm, vm.pc, &file, &line_no, &col_no))
    printf("At %s:%d:%d\n", file->name, line_no, col_no);
```

## Exporting JSON

```c
FILE *f = fopen("sourcemap.json", "w");
cc_output_source_map_json(&vm, f);
fclose(f);
```

| Field | Meaning |
|---|---|
| `pc` | Bytecode offset |
| `file` | Source file name |
| `line` | 1-based line |
| `col` | 1-based start column, UTF-8 aware |
| `end_col` | 1-based end column |

```json
{
  "version": 3,
  "mappings": [
    {"pc": 8, "file": "test.c", "line": 3, "col": 5, "end_col": 8},
    {"pc": 16, "file": "test.c", "line": 4, "col": 9, "end_col": 14}
  ]
}
```

## Generated code

Code from a comptime macro, an attribute handler or file-scope generation carries a synthetic location (such as `<cccc macro: label>:1:1`, or the macro's call site) rather than a line in a real file. It also carries an **expansion origin**, the chain of macro calls that produced it, which the diagnostic printer renders as `note: in expansion of ...` ([macros-debugging.md](macros-debugging.md#expansion-backtrace)). That chain is for diagnostics only and is not reachable through the source-map functions.

The instruction word format is described in [vm.md](vm.md).
