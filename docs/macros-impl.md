# Comptime Implementation Notes

Contributor notes behind the [macros](macros.md) guides.

## Visibility of globals

Types and prototypes are indexed from any non-system file and resolve on demand. Objects are real storage: the comptime program's data segment is allocated once, right after its own declarations are parsed. An object resolved later, for example from a `GetType`/`VarRef` call at comptime execution time, would have no slot to read or write, so initialized globals resolve only from the main file with a self-contained constant initializer (literals, punctuators, `sizeof`, casts; no identifiers).

Reading a visible global from comptime source splices a *copy* into the comptime program. A `Quote` template built afterwards would then resolve that copy rather than the runtime object, so it is rejected, and the error names the comptime-copy relationship.

A template built by a function called at file scope is parsed after the comptime program's own parse finished. It can name types, tags, enumerators and functions, but not a plain file-scope variable: the data segment is already allocated.

## Declaration tags

A declaration registers under the `struct|union|enum` tag in its own top-level specifiers. A tag inside a function declarator's parameter list is a reference, not a registration: `typedef int (*fp)(struct S *);` registers `fp`, not `S`.

## Demand-driven forwarding

A function definition is indexed by signature. When comptime code calls it, the body is forwarded into the comptime program, and bodies it calls are forwarded transitively. Forwarding is attempted only on demand, so a body that cannot compile in the isolated context is never tried for functions comptime code never references. Typedefs and struct/union/enum definitions inside a comptime block pass through to the main token stream unchanged.

Comptime errors collected during the compile abort it with diagnostics rather than executing a partial program.
