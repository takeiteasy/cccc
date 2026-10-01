# OpenMP

`-fopenmp` makes `#pragma omp` take effect. In the VM every parallel region
runs on one thread, which OpenMP allows. With `-c=native` and `-m` the regions
run on real threads.

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
| `schedule` | Ignored | Static; `dynamic`, `guided` and `auto` run as static |
| `ordered` | Runs in order | Error |

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
| `ordered` | VM only |

`task`, `taskwait`, `sections`, `target`, `teams`, `threadprivate` and
`declare` are errors. A directive at file scope is an error.

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
| `lastprivate(x)` | On `for` and `parallel for`: like `private`, then the value from the last iteration is copied back to the original; combines with `firstprivate` on the same variable |
| `copyprivate(x)` | On `single`: the value from the thread that ran the block is copied to every thread's `x`. Not allowed with `nowait` |
| `reduction(op : x)` | The region gets its own `x` starting at the identity of `op`; the result is combined into the original at the end |
| `num_threads(n)`, `if(c)` | VM: evaluated once, then ignored. Native: set the team size |
| `schedule(...)`, `proc_bind(...)` | Parsed and ignored |
| `nowait` | Skips the barrier at the end of `for` and `single` (native) |

Reduction operators are `+ - * & | ^ && || max min`. `&`, `|` and `^` need an
integer variable; the others need an arithmetic one.

```c
int p = 5;
#pragma omp parallel private(p)
{ p = 100; }
// p is still 5 here
```

`copyin` and `linear` are errors.

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
| `omp_get_wtime`, `omp_get_wtick` | Monotonic seconds, tick of 1 ns | Same |
| `omp_init_lock`, `omp_set_lock`, `omp_unset_lock`, `omp_test_lock`, `omp_destroy_lock` | Simple lock | Simple lock |
| The same with `_nest_lock` | Nestable lock | Nestable lock |

Setting a simple lock the calling thread already holds stops the program with
a trap, because nothing could release it.

## Races

Every region runs on one thread in the VM, so a data race in the source does
not show up there. Code that is correct in the VM can race under `-c=native`
or another OpenMP compiler. Building the native output with
`-fsanitize=thread` finds races.[^2]

## Limitations

- Native output uses pthreads, so Windows has no OpenMP
  ([#1402](https://todo.sr.ht/~takeiteasy/cccc/1402)).
- `ordered` is rejected under native
  ([#1401](https://todo.sr.ht/~takeiteasy/cccc/1401)).
- `schedule(dynamic|guided)` run as static
  ([#1403](https://todo.sr.ht/~takeiteasy/cccc/1403)).
- Native loops need an integer loop variable, and `atomic capture` needs a
  single statement
  ([#1404](https://todo.sr.ht/~takeiteasy/cccc/1404)).
- `-c=generated` replays `#pragma omp` lines and `#include <omp.h>` as written
  ([#1405](https://todo.sr.ht/~takeiteasy/cccc/1405)).
- `copyin`, `linear` and `threadprivate`
  ([#1400](https://todo.sr.ht/~takeiteasy/cccc/1400)).
- `default(none)` is parsed but not enforced
  ([#1398](https://todo.sr.ht/~takeiteasy/cccc/1398)).
- `task`, `taskwait` and `sections`
  ([#1399](https://todo.sr.ht/~takeiteasy/cccc/1399)).

[^1]: Each parallel region becomes a nested function. The enclosing locals it
    reads and writes are captured by reference, which is `shared`; `private`,
    `firstprivate`, `lastprivate` and `reduction` variables are locals of the
    region function, so every thread has its own. The runtime is emitted into
    the output only when a directive or an `omp_*` function is used.
[^2]: `CCCC_NATIVE_CC` can point at a wrapper script that adds
    `-fsanitize=thread` to the host compiler command.
