# ccccl

`ccccl` is a small Lisp — SectorLISP-sized — whose compiler runs entirely
inside cccc's comptime pass. There is no REPL and no interpreter: a `.lisp`
source file is read and lowered to C while `cccc` compiles an ordinary C
translation unit, and the result is plain, portable C that needs no `cccc`
(or `ccccl`) to build or run.

```sh
cccc --build build.c   # builds the runtime and the three examples
./build/bin/append     # -> (A B C D)
```

This is a condensed copy kept as a worked example of cccc's comptime pass,
not a full project checkout — it has no test suite or docs/ subdirectory of
its own. The `ccccl_rt` library, `src/ccccl_comptime.c`, and three example
programs (`append`, `lambda_head`, `mutual`) are exactly as built upstream.

## Two universes

`ccccl` deliberately keeps two disjoint pieces of code that never share
source, only a runtime data-layout contract:

- **The comptime universe** — the reader, the lowering pass, and the
  `Node*`-building replay, all running inside cccc's comptime VM while
  `cccc` compiles a translation unit. Its cons cells (`CccclForm`) exist
  only at compile time and never reach the generated program.
- **The runtime universe** — `runtime/ccccl_rt.{h,c}`, ordinary C with zero
  `cccc` dependency, linked into the final program by the system `cc`.

The one file that bridges them is `src/ccccl_comptime.c`. Everything else on
the comptime side (`include/ccccl/ccccl_plan.h`, `ccccl_reader.h`,
`ccccl_lower.h`) is plain, dependency-free C, pulled into the comptime VM via
`#include @comptime` from `ccccl_comptime.c`.

The acceptance criterion this project holds itself to: the generated C must
contain the program's control flow as real, recognizable function bodies —
not a precomputed literal — and it must compile and run with plain `cc`,
with no `cccc` in the loop.

`LObj` (the runtime's only object type) is opaque outside
`runtime/ccccl_rt.c` — `runtime/ccccl_rt.h` forward-declares it and never
defines it. Every operation goes through an accessor function (`ccccl_car`,
`ccccl_cons`, `ccccl_assoc`, ...).

### Invoking cccc

```sh
cccc -c=generated --emit-only src/ccccl_comptime.c \
    -Iinclude/ccccl -Iruntime \
    -DCCCCL_LISP_PATH='"examples/append.lisp"' \
    -o build/append.gen.c
```

- `src/ccccl_comptime.c` is the only file ever passed to `cccc`.
- `--emit-only` avoids cccc's auto-captured `#include` text and its own
  re-derived type declarations colliding in the output; the runtime
  declarations the generated code needs are added back with
  `EmitDirective("#include \"ccccl_rt.h\"")` from inside the comptime
  function.
- `-D` rather than a source `#define` for the input path: comptime function
  bodies do not see ordinary source `#define`s (only `#define @shared`
  ones).
- `examples/append_main.c` (with `main()`) is never passed to `cccc` at
  all — `-c=generated` only serializes macro-touched content. It is
  ordinary C, compiled and linked by the system `cc` alongside the
  generated output and `runtime/ccccl_rt.c`.

### Scoping

Dynamic, environment-passing scoping. Each lowered function has the shape
`LObj *f(LObj *args, LObj *env)`. At entry, its own parameters are bound
into a fresh, extended environment (`ccccl_bind_list`), and every lookup and
every call inside its body uses that extended environment — never the raw
incoming `env`. A callee's free variables therefore resolve in the
*caller's* live environment, matching SectorLISP's own dynamic scoping. This
is deliberate over closure conversion (lexical scoping): faithful to
SectorLISP, and small enough for a proof of concept.

## Language

Strict SectorLISP: the seven primitive forms, `LAMBDA`, `LABEL`, and a
toplevel `DEFINE`. There are no numbers — an `LObj` is atom-or-pair,
nothing else — and no macros, strings, or characters.

- Symbols are case-insensitive and read as upper-case (`append` reads as
  `APPEND`).
- `;` starts a line comment.
- `NIL` and `T` are ordinary atoms, not special reader syntax —
  `ccccl_nil` and `ccccl_t` in the runtime are real interned atoms, never
  `NULL`.

| Form | Meaning |
|---|---|
| `(quote x)` | `x` unevaluated. `x` must be an atom — quoting a list is not supported. |
| `(atom x)` | `T` if `x` is an atom (including `NIL`), else `NIL`. |
| `(eq x y)` | `T` if `x` and `y` are the same object (pointer equality on interned atoms), else `NIL`. |
| `(car x)` / `(cdr x)` | Head/tail of a pair; `NIL` if `x` is not a pair. |
| `(cons x y)` | A new pair. |
| `(cond (p1 e1) (p2 e2) ...)` | Evaluates each `pi` in order; returns the `ei` of the first non-`NIL` `pi`, or `NIL` if none match. Short-circuits — an unmatched clause's `ei` is never evaluated, which is what lets a recursive function terminate. |

```lisp
(define (append x y)
  (cond ((eq x nil) y)
        (t (cons (car x) (append (cdr x) y)))))
```

`(define (name params...) body)` at toplevel. `body` is a single expression
(SectorLISP's implicit `PROGN` is not implemented). Self-recursion, a
`LAMBDA`/`LABEL` nested inside one function, and mutual recursion between
two independently-defined toplevel `define`s (`examples/mutual.lisp`) all
work.

`(lambda (params...) body)` produces a first-class function value (a
closure over the environment active where the `LAMBDA` form appears),
lowered to a real generated C function plus a
`ccccl_closure`/`ccccl_apply` call site (`examples/lambda_head.lisp`).
`(label name (lambda (params...) body))` additionally binds `name` to the
closure itself inside its own body, for anonymous recursion.

Deliberately out of scope: integer arithmetic, strings, characters,
`defmacro`, quoted lists, tail-call elimination, garbage collection.

## Lowering, worked example

`(define (append x y) (cond ((eq x nil) y) (t (cons (car x) (append (cdr x) y)))))`
lowers through three stages:

1. **Reader** (`ccccl_reader.h`) produces a `CccclForm` tree: a `PAIR` whose
   `car` is the atom `DEFINE`.
2. **Lowering** (`ccccl_lower.h`) creates a `CccclPlanFn` named `append` and
   lowers the body to a flat, postfix `CccclOp` list. A `COND` clause's
   predicate and value expressions are lowered into a *separate* op pool
   (`cond_ops[]`), never inlined into the function's main `ops[]` — this
   isn't a style choice, it's required correctness: if a clause's ops were
   interleaved into the top-level array, the linear walk that array gets
   would execute them unconditionally in addition to `COND`'s own lazy,
   conditional re-dispatch into them. For `append`, the untaken branch
   recursing forever was the concrete symptom before this split existed.
3. **Comptime replay** (`src/ccccl_comptime.c`) walks the op list as a
   non-recursive stack machine, building `Node*` AST fragments via
   `Quote(...)` splices, turning each `COND` clause into a C ternary.

The result (formatted for readability; real output is one line):

```c
LObj *append(LObj *args, LObj *env) {
    return ccccl_eq(
        ccccl_assoc(ccccl_sym_0(), ccccl_bind_list(params, args, env)),
        ccccl_get_nil()
    ) != ccccl_get_nil()
    ? ccccl_assoc(ccccl_sym_1(), ccccl_bind_list(params, args, env))
    : ccccl_get_t() != ccccl_get_nil()
      ? ccccl_cons(
          ccccl_car(ccccl_assoc(ccccl_sym_0(), ccccl_bind_list(params, args, env))),
          append(
              ccccl_cons(
                  ccccl_cdr(ccccl_assoc(ccccl_sym_0(), ccccl_bind_list(params, args, env))),
                  ccccl_cons(ccccl_assoc(ccccl_sym_1(), ccccl_bind_list(params, args, env)), ccccl_get_nil())
              ),
              ccccl_bind_list(params, args, env)
          )
        )
      : ccccl_get_nil();
}
```

`ccccl_get_nil()`/`ccccl_get_t()` are function-call accessors, not the bare
`ccccl_nil`/`ccccl_t` globals: cccc's comptime `Quote()` identifier resolver
accepts a bare function-call identifier without requiring it to be
separately comptime-visible, but rejects a bare *variable* reference to an
extern declared only via a plain (non-comptime-routed) `#include`. Only
generated code needs the accessors; hand-written code
(`examples/append_main.c`) uses the globals directly.

`ccccl_sym_0`/`ccccl_sym_1` are memoized per-symbol functions, forward
-declared before any function body that calls them, since emission order
does not follow call order. `cccc -c=generated` scans each generated
function's body for calls to other not-yet-declared generated functions as
it emits it, and inserts a forward declaration right there — this is what
makes mutual recursion between two independently-defined toplevel
`define`s (`examples/mutual.lisp`, `evenp` calling `oddp` calling `evenp`
back) work regardless of `MakeFunction`/creation order.

```sh
cc -Iruntime examples/append_main.c build/append.gen.c runtime/ccccl_rt.c -o build/append
./build/append   # -> (A B C D)
```

No `cccc` anywhere in this step.

## License

MIT — see [LICENSE](LICENSE).
