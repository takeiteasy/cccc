# OpenMP

`-fopenmp` makes `#pragma omp` take effect. The VM runs every parallel region
on one thread, which OpenMP allows, so results match a one-thread OpenMP run.

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
cccc -fopenmp sum.c
```

Without the flag `#pragma omp` lines are ignored without a warning and
`_OPENMP` is undefined. With it `_OPENMP` is `201511`. `--openmp` is the long
spelling.

## Directives

| Directive | Behaviour |
|---|---|
| `parallel` | Runs the block once |
| `for`, `parallel for` | Runs the loop once; `collapse(n)` checks `n` nested loops |
| `critical`, `critical(name)` | Runs the block |
| `atomic` (`read`, `write`, `update`, `capture`) | Runs the update statement |
| `barrier` | Does nothing |
| `single`, `master`, `masked` | Runs the block |
| `simd`, `ordered`, `for simd` | Runs the block; the hint is ignored |

`task`, `taskwait`, `sections`, `target`, `teams`, `threadprivate` and
`declare` are errors. A directive at file scope is an error.

## Clauses

| Clause | Behaviour |
|---|---|
| `private(x)` | The region gets its own uninitialised `x` |
| `firstprivate(x)` | The region gets its own `x`, copied from the original; arrays are copied whole |
| `shared(x)`, `default(shared)` | No change |
| `lastprivate(x)` | On `for` and `parallel for`: like `private`, then the final value is copied back to the original; combines with `firstprivate` on the same variable |
| `copyprivate(x)` | On `single`: accepted; with one thread there is nothing to broadcast. Not allowed with `nowait` |
| `reduction(op : x)` | The region gets its own `x` starting at the identity of `op`; the result is combined into the original at the end |
| `num_threads(n)`, `if(c)` | Evaluated once, then ignored |
| `schedule(...)`, `proc_bind(...)`, `nowait` | Parsed and ignored |

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

`<omp.h>` provides the functions below. The team is always the initial thread.

| Function | Result |
|---|---|
| `omp_get_num_threads`, `omp_get_max_threads`, `omp_get_thread_limit` | `1` |
| `omp_get_thread_num` | `0` |
| `omp_in_parallel` | `0` |
| `omp_get_num_procs` | Host CPU count |
| `omp_set_num_threads`, `omp_set_dynamic`, `omp_get_dynamic` | Accepted, no effect |
| `omp_get_wtime`, `omp_get_wtick` | Monotonic seconds, tick of 1 ns |
| `omp_init_lock`, `omp_set_lock`, `omp_unset_lock`, `omp_test_lock`, `omp_destroy_lock` | Simple lock |
| The same with `_nest_lock` | Nestable lock |

Setting a simple lock that is already held stops the program with a trap,
because with one thread nothing could release it.

## Races

Every region runs on one thread, so a data race in the source does not show
up in the VM. Code that is correct here can still race under another OpenMP
compiler.

## Limitations

- The VM only: `-c=native`, `-m` and `-c=generated` reject OpenMP
  ([#1368](https://todo.sr.ht/~takeiteasy/cccc/1368)).
- `copyin`, `linear` and `threadprivate` ([#1400](https://todo.sr.ht/~takeiteasy/cccc/1400)).
- `default(none)` is parsed but not enforced
  ([#1398](https://todo.sr.ht/~takeiteasy/cccc/1398)).
- `task`, `taskwait` and `sections`
  ([#1399](https://todo.sr.ht/~takeiteasy/cccc/1399)).
