# ccccl

`ccccl` is a small Lisp — SectorLISP-sized — whose compiler runs entirely
inside cccc's comptime pass. There is no REPL and no interpreter: a `.lisp`
source file is read and lowered to C while `cccc` compiles an ordinary C
translation unit, and the result is plain, portable C that needs no `cccc`
(or `ccccl`) to build or run.

```sh
cccc -c=native src/ccccl_comptime.c examples/append_main.c runtime/ccccl_rt.c \
    -Iinclude/ccccl -Iruntime -DCCCCL_LISP_PATH='"examples/append.lisp"' \
    -o append
./append   # -> (A B C D E F G)
```

That one invocation reads `examples/append.lisp`, lowers it to C, and
compiles and links the whole program in a single step — no intermediate
`.gen.c`, no separate `cc`. `make -C examples/ccccl native` does the same for
every example below. There is no `cccc as target` support in the `build.c`
DSL yet, so this directory is built with a plain `Makefile`, not
`cccc --build build.c`.

This is a condensed copy kept as a worked example of cccc's comptime pass,
not a full project checkout — it has no test suite or `docs/` subdirectory
of its own, and `make -C examples/ccccl check`/`native` is a separate,
opt-in target: nothing in cccc's own `test` build or CI runs it.

## Two universes

`ccccl` deliberately keeps two disjoint pieces of code that never share
source, only a runtime data-layout contract:

- **The comptime universe** — the reader, the lowering pass, and the
  `Node*`-building emitter, all running inside cccc's comptime VM while
  `cccc` compiles a translation unit. Its own data structures
  (`CccclForm`, `CccclPlan`) exist only at compile time and never reach the
  generated program.
- **The runtime universe** — `runtime/ccccl_rt.{h,c}`, ordinary C with zero
  `cccc` dependency, linked into the final program by the system `cc`.

The one file that bridges them is `src/ccccl_comptime.c` — the only file
ever passed to `cccc`. Everything else on the comptime side
(`include/ccccl/ccccl_form.h`, `ccccl_ir.h`, `ccccl_lower.h`) is plain,
dependency-free C, pulled into the comptime VM via `#include @comptime` from
`ccccl_comptime.c`. `runtime/ccccl_rt.h` is pulled in with
`#include @shared` instead — that is what lets generated code reference the
runtime's `ccccl_nil`/`ccccl_t` globals directly; a bare extern variable
reached only through a plain `#include` is rejected by `Quote()`'s
identifier resolver.

The acceptance criterion this project holds itself to: **the generated C
must contain the program's control flow as real, recognizable function
bodies — not a precomputed literal — and it must compile and run with plain
`cc`, with no `cccc` in the loop.**

`LObj` (the runtime's only object type) is opaque outside
`runtime/ccccl_rt.c` — `runtime/ccccl_rt.h` forward-declares it and never
defines it. Every operation goes through an accessor function (`ccccl_car`,
`ccccl_cons`, `ccccl_add`, ...).

## Building

```sh
make -C examples/ccccl              # build every example's binary under build/
make -C examples/ccccl check        # build + run every example, diff against .expected
make -C examples/ccccl native       # one-shot `cccc -c=native` per example -- the headline path
make -C examples/ccccl show-append  # print the generated C for one example
make -C examples/ccccl clean
```

`make check` goes through `cccc -c=generated` to produce a `build/NAME.gen.c`
file, then a separate `cc` step links it against the hand-written
`examples/NAME_main.c` and `runtime/ccccl_rt.c` — the portability proof: the
generated C is inspectable, and needs nothing but a C compiler from that
point on. `make native` does the same work in one `cccc -c=native`
invocation, with no intermediate file. Both must produce byte-identical
program output; `make check` and `make native` are run separately in CI-like
verification for exactly that reason.

`CCCC` (default `../../cccc`) and `CC` (default `cc`) are both overridable —
`CCCC=../../build/cccc make native` runs the full build's binary instead of
the stage0 one. cccc's own `build.c` and repo-root `./cccc` binaries go
stale independently of each other; if `make native` behaves unexpectedly
after touching cccc's own source, rebuild both before assuming this
directory regressed.

## Invoking cccc directly

```sh
cccc -c=generated src/ccccl_comptime.c \
    -Iinclude/ccccl -Iruntime \
    -D CCCCL_LISP_PATH='"examples/append.lisp"' \
    -o build/append.gen.c
```

- `-D` rather than a source `#define` for the input path: comptime function
  bodies do not see ordinary source `#define`s (see `man/MACROS.md`'s
  include-scoping section).
- `examples/append_main.c` (with `main()`) is never passed to `cccc` at
  all — `-c=generated` only serializes macro-touched content. It is
  ordinary C, compiled and linked by the system `cc` alongside the
  generated output and `runtime/ccccl_rt.c`.

## Scoping

**Lexical**, not SectorLISP's own dynamic, environment-passing scoping —
the one deliberate semantic departure from SectorLISP in this rewrite. Each
lowered function gets real, named C parameters:
`(define (append x y) ...)` becomes `LObj *append(LObj *x, LObj *y)`, not
`LObj *append(LObj *args, LObj *env)` reading its arguments back out of a
runtime assoc-list. Name resolution happens once, at lowering time (a scope
stack in `ccccl_lower.h`), not on every variable reference at runtime — an
unbound symbol is a comptime error (`MacroErrorAt`, with the source
position), not a silent `NIL`. A `LAMBDA`/`LABEL`'s free variables are
collected during lowering and captured **positionally**, by index, at the
closure's creation site — not by re-walking a live caller environment. This
is what makes "real C parameters, statement-form bodies" possible at all:
dynamic scoping needs an environment to thread through every call; lexical
scoping needs only the variables a function actually captures.

### Closures

`(lambda (params...) body)` lowers to a generated
`LObj *NAME(LObj *captures, LObj *args)` plus a
`ccccl_closure(NAME, captures)` call site, where `captures` is an ordinary
cons list built at the closure's creation site — one entry per free
variable the lambda actually reads, in a fixed order recorded at lowering
time. The generated body unpacks its own parameters positionally
(`ccccl_nth(args, 0)`, ...) and its captures the same way
(`ccccl_nth(captures, 0)`, ...). `ccccl_apply(f, args)` calls through any
closure value uniformly. See `examples/adder.lisp`:

```lisp
(define (make-adder n) (lambda (x) (+ x n)))
```

`n` is free inside the `LAMBDA` and bound in `make-adder`'s own parameter
list, so it becomes a one-entry capture list built at the closure's
creation site, read back out as `ccccl_nth(captures, 0)` inside the
generated lambda body.

`(label name (lambda (params...) body))` additionally binds `name` to the
closure itself, inside its own body, for anonymous recursion — but the
capture list is built *before* the closure exists (chicken-and-egg: the
closure needs its capture list to be created; the capture list needs a slot
for the closure it will become part of). Lowered as allocate-then-patch: the
`name` capture slot gets a `ccccl_nil` placeholder, the closure is created,
then `ccccl_capture_set` patches the real closure into that slot —
`ccccl_closure_self` composes both steps into one expression.

### A toplevel `define` in value position

A toplevel `define`'s real C signature — `LObj *square(LObj *x)` — has no
`(captures, args)` closure entry point of its own, so `ccccl_apply` can't
call it directly. A bare reference to a toplevel name in *value* position
(not a call: `square` on its own, not `(square 6)`) is detected at lowering
time and gets a small generated wrapper,
`static LObj *square__thunk(LObj *captures, LObj *args)`, that unpacks
`args` positionally and calls the real `square(...)`; only functions
actually used that way get a thunk. See `examples/adder.lisp`'s
`(define (get-square) square)`.

### Tail calls

A **self**-tail call — `reverse-acc` calling itself in tail position, in
`examples/reverse.lisp` — never becomes a recursive C call at all. It
lowers to parameter reassignment (through temporaries, so earlier
parameters aren't clobbered mid-update) plus setting a repeat flag, inside
a loop wrapping the whole function body:

```sh
make -C examples/ccccl show-reverse
```

```c
LObj *reverse_acc(LObj *xs, LObj *acc) {
    LObj *ccccl_tt_0;
    LObj *ccccl_tt_1;
    int   ccccl_again;
    LObj *result;

    ccccl_again = 1;
    while (ccccl_again) {
        ccccl_again = 0;
        if (ccccl_eq(xs, ccccl_nil) != ccccl_nil) {
            result = acc;
        } else {
            /* self-tail call: reassign, don't recurse */
            ccccl_tt_0  = ccccl_cdr(xs);
            ccccl_tt_1  = ccccl_cons(ccccl_car(xs), acc);
            xs          = ccccl_tt_0;
            acc         = ccccl_tt_1;
            ccccl_again = 1;
        }
    }
    return result;
}
```

(Reformatted for readability; the real output — `make show-reverse` — casts
every `LObj*` operand explicitly and prints the serializer's own canonical
brace style, which reflows `while` back out as `for (; cond; )`.)

Not `continue`/`for (;;)`, which was the first design tried: `Quote()`
rejects `continue`/`break` as "stray" the moment that `Quote()` call is
parsed, before the resulting statement node is ever spliced anywhere — a
separately built statement is parsed (and rejected, if unattached to a real
loop already visible to *that* parse) in isolation from wherever it later
gets spliced, so neither keyword can be made to work by embedding it inside
a later template that happens to also spell the enclosing loop out
syntactically. A repeat flag sidesteps this entirely: `again = 1;` is an
ordinary assignment, and composes through the same statement-sequencing
machinery (`ccccl_seq`) every other statement in this compiler uses.

A **non**-self-tail call — `append` calling itself inside `cons`, or
`evenp`/`oddp` calling each other in `examples/mutual.lisp` — still compiles
to an ordinary recursive C call. cccc's own native-backend tail-call
elimination (`CALLT`) may or may not kick in there depending on the host
compiler's optimization level (see `man/NATIVE.md`'s serialized-output
divergences); `ccccl`'s own TCO above is unconditional and guaranteed only
for the direct self-tail case.

## Language

**SectorLISP+**: the seven original SectorLISP primitives plus
`lambda`/`label`/`define`, extended with fixnums and their arithmetic,
`if`, `let`, `progn`, quoted lists, and `print`.

- Symbols are case-insensitive and read as upper-case (`append` reads as
  `APPEND`).
- `;` starts a line comment.
- `NIL` and `T` are ordinary atoms, not special reader syntax —
  `ccccl_nil` and `ccccl_t` in the runtime are real interned atoms, never
  `NULL`.
- An integer token (`42`, `-7`) reads as a fixnum (`LObj` tag `CCCCL_INT`),
  distinct from an atom.

| Form | Meaning |
|---|---|
| `(quote x)` | `x` unevaluated — an atom, a fixnum, or a list, which desugars recursively into nested `cons`/atom/fixnum construction at lowering time. |
| `(atom x)` | `T` if `x` is an atom or fixnum (i.e. not a pair), else `NIL`. |
| `(eq x y)` | `T` if `x` and `y` are the same object (pointer equality on interned atoms; value equality on fixnums), else `NIL`. |
| `(car x)` / `(cdr x)` | Head/tail of a pair; `NIL` if `x` is not a pair. |
| `(cons x y)` | A new pair. |
| `(cond (p1 e1) (p2 e2) ...)` | Evaluates each `pi` in order; returns the `ei` of the first non-`NIL` `pi`, or `NIL` if none match. Short-circuits — an unmatched clause's `ei` is never evaluated, which is what lets a recursive function terminate. |
| `(if p then else)` | `then` if `p` is non-`NIL`, else `else`. Both arms are always present. |
| `(+ a b)` `(- a b)` `(* a b)` `(/ a b)` `(mod a b)` | Fixnum arithmetic. A non-fixnum operand is a runtime error, not undefined behaviour — this is a teaching demo, not a hardened VM. |
| `(< a b)` `(= a b)` | Fixnum comparison, `T`/`NIL`. |
| `(let ((v1 e1) (v2 e2) ...) body)` | Evaluates each `ei` in the *outer* scope, binds `vi` to a real C local, evaluates `body` with all bindings visible. |
| `(progn e1 e2 ... en)` | Evaluates each `ei` in order for its side effects; the value is `en`. |
| `(print x)` | Prints `x`, then evaluates to `x` itself — composes in expression position, matching Common Lisp's `PRINT`. |

```lisp
(define (append x y)
  (cond ((eq x nil) y)
        (t (cons (car x) (append (cdr x) y)))))
```

`(define (name params...) body)` at toplevel. `body` is a single expression
— use `progn` for a sequence. Self-recursion, a `LAMBDA`/`LABEL` nested
inside one function, and mutual recursion between two independently-defined
toplevel `define`s (`examples/mutual.lisp`) all work.

Deliberately out of scope: strings, characters, `defmacro`, vectors, garbage
collection (fixed arenas, sized for this project's own examples — see each
`CL_MAX_*`/`CCCCL_RT_*` constant). Constant folding of fixnum arithmetic at
comptime (`(+ 1 2)` lowering straight to `ccccl_int(3)` instead of a runtime
`ccccl_add` call) was considered and left unimplemented — a real optimization
this compiler could do, just not one built here.

## The example matrix

| Example | Feature it proves |
|---|---|
| `append.lisp` | `cond`, non-tail self-recursion, a quoted-list literal |
| `lambda_head.lisp` | an inline `LAMBDA` applied immediately, capturing nothing |
| `mutual.lisp` | mutual recursion between two toplevel `define`s |
| `reverse.lisp` | a self-tail call → loop-and-reassign, not recursion (see "Tail calls" above) |
| `fib.lisp` | fixnums, `+`/`-`/`<`, `if` |
| `adder.lisp` | a real lexical closure capture, and a toplevel name used in value position (the `__thunk` path) |
| `letsum.lisp` | `let` → real C locals, `progn`, `print` (prints the sum twice — once from the Lisp `(print s)`, once from `letsum_main.c` printing the returned value, hence `77` in `letsum.expected`) |

Each has a `NAME.lisp`, a hand-written `NAME_main.c`, and a `NAME.expected`
— `make check`/`make native` build, run, and diff every one.

## Lowering, worked example

`(define (append x y) (cond ((eq x nil) y) (t (cons (car x) (append (cdr x) y)))))`
lowers through three stages:

1. **Reader** (`ccccl_form.h`) produces a `CccclForm` tree: a `PAIR` whose
   `car` is the atom `DEFINE`.
2. **Lowering** (`ccccl_lower.h`) creates a `CccclPlanFn` named `append` and
   lowers the body into a `CccclExpr` **tree** (`ccccl_ir.h`) — a `COND`
   clause's predicate and value are just child expressions of the `COND`
   node, walked by ordinary recursive descent. This replaced an earlier
   flat, postfix op-list design (`CccclOp` plus a *separate* `cond_ops[]`
   pool, kept apart from the main op list only because an interleaved
   layout would execute a clause's ops unconditionally instead of under
   `COND`'s own lazy dispatch) — a tree only ever visits the branch the
   emitter actually recurses into, so that hazard has no way to occur.
3. **Comptime replay** (`src/ccccl_comptime.c`) walks the tree recursively,
   building `Node*` AST fragments via `Quote(...)` splices. `if`/`cond` and
   `let`/`progn` are destination-passing (`ccccl_emit_stmt`, assigning into
   an already-declared local) so nested control flow composes as real
   `if`/`else` statements, not an expression.

The result (`make show-append`, reformatted here for readability — the real
output casts every operand and keeps everything on few, long lines):

```c
LObj *append(LObj *x, LObj *y) {
    LObj *result;
    if (ccccl_eq(x, ccccl_nil) != ccccl_nil) {
        result = y;
    } else {
        if (ccccl_t != ccccl_nil) {
            result = ccccl_cons(ccccl_car(x), append(ccccl_cdr(x), y));
        } else {
            result = ccccl_nil;
        }
    }
    return result;
}
```

Compare that against the old version of this example, which produced one
enormous nested-ternary `return` rebuilding an assoc-list environment on
every variable reference — the entire motivation for this rewrite. Real
`if`/`else` statements, named parameters, and a named local are what "the
generated C must contain the program's control flow as real, recognizable
function bodies" means in practice.

`ccccl_sym_0`/`ccccl_sym_1`/... are memoized per-symbol/per-quoted-atom
accessor functions (`GlobalVar` + `GlobalVarSetStatic`, a file-scope static
cache that survives serialization), forward-declared before any function
body that calls them, since emission order does not follow call order.
`cccc -c=generated`/`-c=native` scan each generated function's body for
calls to other not-yet-declared generated functions as they emit it, and
insert a forward declaration right there — this is what makes mutual
recursion between two independently-defined toplevel `define`s
(`examples/mutual.lisp`, `evenp` calling `oddp` calling `evenp` back) work
regardless of creation order.

```sh
cc -Iruntime examples/append_main.c build/append.gen.c runtime/ccccl_rt.c -o build/append
./build/append   # -> (A B C D E F G)
```

No `cccc` anywhere in this step.

## License

MIT — see [LICENSE](LICENSE).
