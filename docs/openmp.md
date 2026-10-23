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
| `ordered(n)` with `sink`/`source` | No waiting needed | Waits for the named iterations[^5] |

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
| `atomic` (`read`, `write`, `update`, `capture`, `compare`, `fail`, `weak`, a memory order, `hint`) | Atomic update of one variable. A memory order is `relaxed` (the default), `acquire`, `release`, `acq_rel` or `seq_cst` |
| `barrier` | Waits for the team |
| `single` | One thread runs the block; the others wait unless `nowait` |
| `master`, `masked` | Thread 0 runs the block |
| `simd`, `for simd` | Runs the block; the hint is ignored |
| `sections`, `parallel sections` | VM: runs the `section` blocks in order. Native: each thread takes the next unclaimed block |
| `task` | VM: runs the block where it is met. Native: queues the block for the team; see [tasks](#tasks) |
| `taskwait` | Native: waits for the tasks the current task created. VM: no effect |
| `taskyield` | No effect |
| `taskgroup` | Native: waits at the end of the block for the tasks created in it and their descendants. VM: runs the block |
| `threadprivate(x, ...)` | Gives each thread its own copy of a global or `static` variable; allowed at file scope and in a function |
| `ordered` | Runs the block in loop-iteration order; allowed inside a loop with the `ordered` clause, or in a function called from one |
| `ordered depend(sink: ...)`, `ordered depend(source)` | Standalone directive inside a loop with `ordered(n)`; see [doacross loops](#doacross-loops) |

`target`, `teams` and `declare` are errors. Other directives at file scope are
errors.

`break` out of the loop of `for` and `parallel for` is an error; `continue`
ends the iteration.

Native loops use the canonical OpenMP form: an integer or object-pointer
variable (`for (int *p = a; p < a + n; p++)`), a bound compared with `<`, `<=`, `>`, `>=` or `!=`, and a step written `i++`, `i += n`,
`i = i + n` or the decrementing equivalents. `collapse` needs perfectly nested,
rectangular loops. `return` inside a parallel region is an error. An
`atomic` statement, in the VM too, is one of `x op= e`, `x++`, `x = x op e`
(`op` is `+ - * / & ^ | << >>`, and `e` has no operator that binds as loosely),
`v = x`, `x = e` or `v = x op= e`; `atomic capture` also takes a two-statement
block:

```c
#pragma omp atomic capture
{ v = x; x += 1; }   // also { x op= e; v = x; } and { v = x; x = e; }
```

Clauses may be separated by commas. The memory order reaches `-c=native`
as the order of the emitted `__atomic` operations, and the VM runs every
atomic as `seq_cst`:

```c
#pragma omp atomic write release
flag = 1;
#pragma omp atomic read acquire
f = flag;
```

`hint(e)` takes a constant, such as `omp_sync_hint_contended` from `omp.h`,
and is checked then ignored.

`atomic compare` takes a conditional store, alone or with `capture`:

```c
#pragma omp atomic compare
if (x == e) { x = d; }   // also x = x == e ? d : x;

#pragma omp atomic compare
if (x < e) { x = e; }    // also x = x < e ? e : x; and the `>` forms

#pragma omp atomic compare capture
{ v = x; if (x == e) { x = d; } }   // v is the old value
```

The accepted forms are listed in a footnote.[^compare]

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
| `if(c)`, `final(c)`, `priority(n)`, `depend(...)` on `task` | VM: evaluated once or parsed, then ignored. Native: see [tasks](#tasks) |
| `untied`, `mergeable` on `task` | Parsed and ignored |
| `ordered` | On `for` and `parallel for`: `ordered` regions in the body run in iteration order. `ordered(n)` makes a [doacross loop](#doacross-loops) |
| `schedule(kind[, n])` | VM: checked, then ignored. Native: `static` splits the loop into one block per thread, `static, n` deals chunks of `n` round-robin, `dynamic` and `guided` hand out chunks on demand, `auto` is `static`, and `runtime` reads `omp_set_schedule`, else `OMP_SCHEDULE`, else `static`. `monotonic`, `nonmonotonic` and `simd` modifiers are accepted and ignored |
| `proc_bind(...)` | Parsed and ignored |
| `nowait` | Skips the barrier at the end of `for` and `single` (native) |

A `task` works on its own copy of a local variable it uses and does not list,
unless an enclosing `parallel` region shares the variable. `default(firstprivate)`
and `default(private)` apply to every unlisted local; `default(shared)` shares
them. A task's copy takes the value the variable has when the task is created:

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

## Tasks

Native tasks run on the threads of the team; the VM runs each task where it
is met. Queued tasks run at a barrier, at `taskwait`, at the end of a
`taskgroup`, at the end of the `parallel` region, and on any thread that has
nothing else to do.[^tasks]

```c
long fib(int n) {
    if (n < 2) return n;
    long a, b;
#pragma omp task shared(a) firstprivate(n)
    a = fib(n - 1);
#pragma omp task shared(b) firstprivate(n)
    b = fib(n - 2);
#pragma omp taskwait
    return a + b;
}

#pragma omp parallel
#pragma omp single
r = fib(40);
```

A task pays off when it does real work. Give small ones a cutoff, for example
`if(n > 25)`, or call a serial function below it.

| Clause | Native |
|---|---|
| `if(0)` | The task runs at once on the thread that meets it, which waits until it is done |
| `final(c)` | When true the task and every task it creates run at once; `omp_in_final()` is `1` inside |
| `priority(n)` | A task with `n > 0` is taken before tasks without a priority |
| `depend(in: x)` | Waits for the earlier sibling tasks that have `x` in an `out` or `inout` |
| `depend(out: x)`, `depend(inout: x)` | Waits for the earlier sibling tasks that have `x` in any `depend` |
| `depend(mutexinoutset: x)`, `depend(inoutset: x)` | Treated as `inout` |

A `depend` item is a variable, an element, or an array section `a[lo:len]`;
two items are the same dependence when their first addresses match.
`depend(depobj: ...)` and the `iterator` modifier are errors. `taskwait
depend(...)` waits for every task the current task created.

## Doacross loops

`ordered(n)` on a loop nest of `n` loops lets an iteration wait for earlier
ones. `sink` names the iteration to wait for and `source` marks the current
one as done:

```c
long a[65] = {1};
#pragma omp parallel for ordered(1)
for (int i = 1; i <= 64; i++) {
    long x = expensive(i);              // runs in parallel
#pragma omp ordered depend(sink : i - 1)
    a[i] = a[i - 1] + x;                // after iteration i - 1 is done
#pragma omp ordered depend(source)
}
```

A sink vector has one `name ± constant` element per loop, so a two-loop nest
writes `depend(sink : i - 1, j)`. Several `sink` clauses wait for all of their
iterations. An iteration that does not exist, such as `i - 1` in the first
iteration, is not waited for. `doacross(sink : ...)`, `doacross(source :)` and
`doacross(sink : omp_cur_iteration - 1)` are the OpenMP 5.2 spellings.

A sink that names the current or a later iteration, or one that no iteration
has because the offset is not a multiple of the step, is ignored with a
`-Wopenmp` warning (on by default). An iteration that ends without reaching its
`source`, for example through `continue`, still counts as done.

## Runtime functions

`<omp.h>` provides the functions below.

| Function | VM | Native |
|---|---|---|
| `omp_get_num_threads` | `1` | Team size |
| `omp_get_max_threads`, `omp_get_thread_limit` | `1` | Default team size |
| `omp_get_thread_num` | `0` | Thread index in the team |
| `omp_in_parallel` | `0` | `1` inside a team of more than one thread |
| `omp_in_final` | `0` | `1` inside a final task |
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
- An `ordered` loop without a `schedule` clause runs close to serial; `schedule(static, 1)` or `schedule(dynamic)` lets the work outside the `ordered` region overlap.
- `#pragma omp` inside a `#pragma cccc emit` block is rejected.
- A task cannot have its own copy of a variable-length array under `-c=native`; name it in `shared` (#1766).
- A thread waiting at `taskwait` or the end of a `taskgroup` runs only the tasks it created; other threads run the rest.

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
[^5]: Iterations are numbered across the whole nest. A thread posts the last
    iteration it finished in a counter for each chunk, since a chunk runs its
    iterations in order, and a `sink` waits until the counter of the chunk
    holding its iteration has passed it. A waiting thread spins briefly, then
    sleeps until a post wakes it. Every schedule keeps one counter per chunk,
    so memory follows the number of chunks, not the trip count.

[^tasks]: A task is a nested function with its own copies of the variables it
    privatises, stored with the task when it is created. Tasks wait in one
    queue per team, oldest first for idle threads and newest first for a
    thread waiting on its own tasks. A task that is not deferred, runs in a
    team of one thread, or is met while more than 64 tasks per thread are
    queued runs at once on the thread that meets it. Parallel regions drain
    the queue before they end.
[^compare]: `x` is on the left of `==`; `<` and `>` take `x` on either side
    (`<=` and `>=` are not accepted). `capture` also takes
    `{ if (...) {...} v = x; }` (the new value),
    `if (x == e) { x = d; } else { v = x; }` and
    `{ r = x == e; if (r) { x = d; } [else { v = x; }] }`. `x == e` compares
    the stored bits, so `-0.0` does not match `0.0`. `weak` needs `==` and
    makes the exchange weak. `fail(seq_cst|acquire|relaxed)` sets the order of
    a failed exchange; without it that is the success order minus its release
    half. `read` rejects `release` and `write` rejects `acquire`; `acq_rel`
    acquires on a `read` and releases on a `write`. A failure order stronger
    than the success order raises the success order.
