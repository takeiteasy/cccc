# OpenMP

`-fopenmp` makes `#pragma omp` take effect. In the VM every parallel region
runs on one thread, which OpenMP allows. With `-c=native` and `-m` the regions
run on real threads.
`-c=generated` does too, for the regions in macro-generated code. Threads and
the VM's global lock are in [threading.md](threading.md).

```c
#include <omp.h>

int main(void) {
    long sum = 0;
#pragma omp parallel for reduction(+ : sum)
    for (int i = 1; i <= 10; i++)
        sum += i;
    return sum == 55 ? 42 : 1;
}
```

```sh
cccc -fopenmp sum.c              # VM: one thread
cccc -fopenmp -c=native sum.c    # native: a thread per CPU
```

Without the flag `#pragma omp` lines are ignored without a warning and
`_OPENMP` is undefined. With it `_OPENMP` is `201511`. `--openmp` is the long
spelling.

## VM and native

| | VM | `-c=native`, `-m` |
|---|---|---|
| Team size | 1 | `num_threads(n)`, else `omp_set_num_threads`, else `OMP_NUM_THREADS`, else the CPU count |
| Synchronisation | No-ops | Real mutexes, barrier and atomics |
| Races | Hidden | Real |
| `schedule` | Ignored | `static`, `static,n`, `dynamic`, `guided`, `auto` (static) and `runtime`[^4] |
| `ordered` | Runs in order | Runs in iteration order[^3] |

Native output carries its own runtime: a thread pool that starts on the first
parallel region and is reused, a barrier, named critical sections and the
`omp_*` functions. No libomp is needed and `-pthread` is already passed.[^1]

A parallel region inside another one runs as a team of one. `if(0)` also gives
a team of one.

## Directives

| Directive | Behaviour |
|---|---|
| `parallel` | VM: runs the block once. Native: runs it on every thread of the team |
| `for`, `parallel for` | VM: runs the loop once. Native: splits the iterations across the team; `collapse(n)` merges `n` nested loops |
| `critical`, `critical(name)` | One thread at a time; one lock per name |
| `atomic` (`read`, `write`, `update`, `capture`) | Atomic update of one variable |
| `barrier` | Waits for the team |
| `single` | One thread runs the block; the others wait unless `nowait` |
| `master`, `masked` | Thread 0 runs the block |
| `simd`, `for simd` | Runs the block; the hint is ignored |
| `sections`, `parallel sections` | VM: runs the `section` blocks in order. Native: each thread takes the next unclaimed block |
| `task` | Runs the block immediately on the thread that meets it |
| `taskwait`, `taskyield` | No effect |
| `taskgroup` | Runs the block |
| `threadprivate(x, ...)` | Gives each thread its own copy of a global or `static` variable; allowed at file scope and in a function |
| `ordered` | Runs the block in loop-iteration order; allowed inside a loop with the `ordered` clause, or in a function called from one |

`target`, `teams` and `declare` are errors. Other directives at file scope are
errors.

Native loops use the canonical OpenMP form: an integer variable, a bound
compared with `<`, `<=`, `>`, `>=` or `!=`, and a step written `i++`, `i += n`,
`i = i + n` or the decrementing equivalents. `collapse` needs perfectly nested,
rectangular loops. `return` inside a parallel region is an error. An
`atomic` statement is one of `x op= e`, `x++`, `x = x op e`, `v = x`, `x = e`
or `v = x op= e`.

## Clauses

| Clause | Behaviour |
|---|---|
| `private(x)` | The region gets its own uninitialised `x` |
| `firstprivate(x)` | The region gets its own `x`, copied from the original; arrays are copied whole |
| `shared(x)`, `default(shared)` | No change |
| `default(none)` | On `parallel` and `parallel for`: every variable from outside the region that the body uses needs a data-sharing clause, else an error. Loop variables, `sizeof` operands, `threadprivate` variables and variables declared inside the region are exempt |
| `lastprivate(x)` | On `for` and `parallel for`: like `private`, then the value from the last iteration is copied back to the original; combines with `firstprivate` on the same variable |
| `copyprivate(x)` | On `single`: the value from the thread that ran the block is copied to every thread's `x`. Not allowed with `nowait` |
| `linear(x : step)` | On `for`, `parallel for` and `simd`: `x` is `base + i * step` in iteration `i` (step defaults to 1; `x` is an integer or pointer), and the value from the last iteration is copied back |
| `copyin(x)` | On `parallel` directives: every thread's copy of the `threadprivate` variable `x` starts with the primary thread's value |
| `reduction(op : x)` | The region gets its own `x` starting at the identity of `op`; the result is combined into the original at the end |
| `num_threads(n)`, `if(c)` | VM: evaluated once, then ignored. Native: set the team size. Variables in these clauses need no data-sharing clause under `default(none)` |
| `if(c)`, `final(c)`, `priority(n)` on `task` | Evaluated once, then ignored |
| `untied`, `mergeable`, `depend(...)` on `task` | Parsed and ignored |
| `ordered` | On `for` and `parallel for`: `ordered` regions in the body run in iteration order. `ordered(n)` is an error under native |
| `schedule(kind[, n])` | VM: checked, then ignored. Native: `static` splits the loop into one block per thread, `static, n` deals chunks of `n` round-robin, `dynamic` and `guided` hand out chunks on demand, `auto` is `static`, and `runtime` reads `omp_set_schedule`, else `OMP_SCHEDULE`, else `static`. `monotonic`, `nonmonotonic` and `simd` modifiers are accepted and ignored |
| `proc_bind(...)` | Parsed and ignored |
| `nowait` | Skips the barrier at the end of `for` and `single` (native) |

A `task` works on its own copy of a local variable it uses and does not list,
unless an enclosing `parallel` region shares the variable. `default(firstprivate)`
and `default(private)` apply to every unlisted local; `default(shared)` shares
them. A task's copy is initialised when the task starts:

```c
int x = 1;
#pragma omp task
x = 2;
// x is still 1 here
```

Reduction operators are `+ - * & | ^ && || max min`. `&`, `|` and `^` need an
integer variable; the others need an arithmetic one.

```c
int p = 5;
#pragma omp parallel private(p)
{ p = 100; }
// p is still 5 here
```

## Runtime functions

`<omp.h>` provides the functions below.

| Function | VM | Native |
|---|---|---|
| `omp_get_num_threads` | `1` | Team size |
| `omp_get_max_threads`, `omp_get_thread_limit` | `1` | Default team size |
| `omp_get_thread_num` | `0` | Thread index in the team |
| `omp_in_parallel` | `0` | `1` inside a team of more than one thread |
| `omp_get_num_procs` | Host CPU count | Host CPU count |
| `omp_set_num_threads` | No effect | Team size of later regions |
| `omp_set_dynamic`, `omp_get_dynamic` | No effect | No effect |
| `omp_set_schedule`, `omp_get_schedule` | Stores and returns the value | Sets and returns the schedule of `schedule(runtime)` loops |
| `omp_get_wtime`, `omp_get_wtick` | Monotonic seconds, tick of 1 ns | Same |
| `omp_init_lock`, `omp_set_lock`, `omp_unset_lock`, `omp_test_lock`, `omp_destroy_lock` | Simple lock | Simple lock |
| The same with `_nest_lock` | Nestable lock | Nestable lock |

Setting a simple lock the calling thread already holds stops the program with
a trap, because nothing could release it.

## Generated code

`-c=generated` emits only macro-generated code. A region in a `Quote()`
template is written with `_Pragma`, and the output carries the thread pool and
runtime the region needs:

```c
[[cccc::comptime]]
void gen(void) {
    Obj *fn = MakeFunction("psum", GetType("int"));
    WithFn(fn) {
        FunctionSetBody(
            fn, Quote("int sum = 0;"
                      "_Pragma(\"omp parallel for reduction(+:sum)\")"
                      "for (int i = 0; i < 10; i++) sum += i;"
                      "return sum;"));
    }
}
gen();
```

Hand-written `#pragma omp` lines and `#include <omp.h>` are never copied into
the output, and a region only in hand-written code adds no runtime. Without
`-fopenmp` the `_Pragma` is dropped. Any other `_Pragma` in a template is an
error.

## Races

Every region runs on one thread in the VM, so a data race in the source does
not show up there. Code that is correct in the VM can race under `-c=native`
or another OpenMP compiler. Building the native output with
`-fsanitize=thread` finds races.[^2]

## Limitations

- Native output uses pthreads, so Windows has no OpenMP.
- Doacross `ordered(n)` is rejected under native (#1747).
- An `ordered` loop without a `schedule` clause runs close to serial; `schedule(static, 1)` or `schedule(dynamic)` lets the work outside the `ordered` region overlap.
- `continue` in the body of a native `for` loop is rejected (#1748).
- Native loops need an integer loop variable, and `atomic capture` needs a
  single statement.
- `#pragma omp` inside a `#pragma cccc emit` block is rejected.
- Tasks run inline, so they never overlap (#1743).
- A `task` that contains a `parallel` region shares the variables it uses from
  outside the task (#1744).

[^1]: Each parallel region becomes a nested function. The enclosing locals it
    reads and writes are captured by reference, which is `shared`; `private`,
    `firstprivate`, `lastprivate` and `reduction` variables are locals of the
    region function, so every thread has its own. The runtime is emitted into
    the output only when a directive or an `omp_*` function is used.
[^2]: `CCCC_NATIVE_CC` can point at a wrapper script that adds
    `-fsanitize=thread` to the host compiler command.
[^3]: Each thread waits for the iterations before a chunk before its first
    `ordered` region in it and hands over when the chunk ends. `ordered`
    outside a loop with the `ordered` clause is a compile error unless it is in
    a function of its own.
[^4]: `OMP_SCHEDULE` takes `kind` or `kind,n`, for example `dynamic,4`. A chunk
    size of `0` or less means `1` for `dynamic` and `guided`, and no chunk for
    `static`. The chunk size is evaluated once, before the region starts, so a
    variable it names is read from outside the region. `auto` and `runtime`
    take no chunk size. Named `critical` sections have no limit on the number
    of names.
