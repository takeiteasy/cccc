/*
 CCCC: Comprehensiev C Compensation Compiler

 Copyright (C) 2025 George Watson

 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

// Kernel execution model: the cccc_* work-item builtins and the launcher
// behind cccc_launch (include/cccc/kernel.h). Work-groups run one at a time.
// A kernel without barriers runs its work-items as a loop; one with barriers
// runs a VM thread per work-item of the group.

#include "../cccc.h"
#include "../internal.h"

#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define KERNEL_DEFAULT_MAX_GROUP 256
#define KERNEL_LOCAL_ALIGN       16

// Guest layout of cccc_range.
typedef struct {
    unsigned dims;
    size_t   size[3];
} GuestRange;

typedef struct {
    unsigned dims;
    size_t   global[3];
    size_t   local[3];
    size_t   groups[3];
} KernelLaunch;

typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t  cond;
    int             active;
    int             arrived;
    unsigned        generation;
} KernelBarrier;

typedef struct {
    const KernelLaunch *launch;
    size_t              global_id[3];
    size_t              local_id[3];
    size_t              group_id[3];
    char               *group_local;
    KernelBarrier      *barrier;
} KernelItem;

static void kernel_fatal(const char *fmt, ...) {
    va_list ap;
    fflush(stdout);
    fprintf(stderr, "error: cccc_launch: ");
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    exit(255);
}

static KernelItem *current_item(VirtualMachine *vm) {
    KernelItem *item = cccc_thread_kernel_item(vm);
    return item ? item : vm->kernel_loop_item;
}

// Outside a launch a kernel is a single work-item: ids 0, sizes 1.
static long long item_query(size_t (*get)(const KernelItem *, unsigned),
                            long long dim, long long outside) {
    KernelItem *item = current_item(cccc_current_ffi_vm());
    if (!item)
        return outside;
    return (long long)get(item, (unsigned)dim);
}

#define DEFINE_ITEM_QUERY(NAME, EXPR, OUTSIDE)                                 \
    static size_t get_##NAME(const KernelItem *item, unsigned dim) {           \
        const KernelLaunch *launch = item->launch;                             \
        (void)launch;                                                          \
        return dim < 3 ? (EXPR) : (OUTSIDE);                                   \
    }                                                                          \
    static long long wrap_cccc_##NAME(long long dim) {                         \
        return item_query(get_##NAME, dim, OUTSIDE);                           \
    }

static inline size_t in_dims(const KernelLaunch *launch, unsigned dim,
                             size_t value, size_t otherwise) {
    return dim < launch->dims ? value : otherwise;
}

DEFINE_ITEM_QUERY(global_id, in_dims(launch, dim, item->global_id[dim], 0), 0)
DEFINE_ITEM_QUERY(local_id, in_dims(launch, dim, item->local_id[dim], 0), 0)
DEFINE_ITEM_QUERY(group_id, in_dims(launch, dim, item->group_id[dim], 0), 0)
DEFINE_ITEM_QUERY(global_size, in_dims(launch, dim, launch->global[dim], 1), 1)
DEFINE_ITEM_QUERY(local_size, in_dims(launch, dim, launch->local[dim], 1), 1)
DEFINE_ITEM_QUERY(num_groups, in_dims(launch, dim, launch->groups[dim], 1), 1)

static long long wrap_cccc_work_dim(void) {
    KernelItem *item = current_item(cccc_current_ffi_vm());
    return item ? (long long)item->launch->dims : 1;
}

static void barrier_wait(void *ctx) {
    KernelBarrier *barrier = ctx;
    pthread_mutex_lock(&barrier->mutex);
    unsigned generation = barrier->generation;
    if (++barrier->arrived == barrier->active) {
        barrier->arrived = 0;
        barrier->generation++;
        pthread_cond_broadcast(&barrier->cond);
    } else {
        while (generation == barrier->generation)
            pthread_cond_wait(&barrier->cond, &barrier->mutex);
    }
    pthread_mutex_unlock(&barrier->mutex);
}

// A work-item that returns counts as arrived at every later barrier, so a
// kernel whose items diverge does not deadlock the group.
static void item_exit(void *ctx) {
    KernelBarrier *barrier = ((KernelItem *)ctx)->barrier;
    pthread_mutex_lock(&barrier->mutex);
    barrier->active--;
    if (barrier->active > 0 && barrier->arrived == barrier->active) {
        barrier->arrived = 0;
        barrier->generation++;
        pthread_cond_broadcast(&barrier->cond);
    }
    pthread_mutex_unlock(&barrier->mutex);
}

static long long wrap_cccc_barrier(long long fences) {
    (void)fences;
    VirtualMachine *vm   = cccc_current_ffi_vm();
    KernelItem     *item = current_item(vm);
    if (item && item->barrier)
        cccc_without_gil(vm, barrier_wait, item->barrier);
    return 0;
}

static size_t align_up(size_t n, size_t a) {
    return (n + a - 1) & ~(a - 1);
}

static void read_range(const GuestRange *range, const char *what,
                       unsigned dims[1], size_t out[3]) {
    if (!range || range->dims < 1 || range->dims > 3)
        kernel_fatal("%s range must have 1 to 3 dimensions", what);
    *dims = range->dims;
    for (unsigned d = 0; d < 3; d++) {
        out[d] = d < range->dims ? range->size[d] : 1;
        if (d < range->dims && range->size[d] == 0)
            kernel_fatal("%s range has a zero size in dimension %u", what, d);
    }
}

static void set_item(KernelItem *item, const KernelLaunch *launch,
                     const size_t group[3], size_t flat, char *group_local,
                     KernelBarrier *barrier) {
    item->launch      = launch;
    item->group_local = group_local;
    item->barrier     = barrier;
    for (int d = 0; d < 3; d++) {
        item->group_id[d]   = group[d];
        item->local_id[d]   = flat % launch->local[d];
        flat               /= launch->local[d];
        item->global_id[d]  = group[d] * launch->local[d] + item->local_id[d];
    }
}

static void run_group_loop(VirtualMachine *vm, long long fn,
                           const long long *iargs, int nint,
                           const double *fargs, int nflt,
                           const KernelLaunch *launch, const size_t group[3],
                           size_t group_items, char *group_local) {
    KernelItem item;
    for (size_t flat = 0; flat < group_items; flat++) {
        set_item(&item, launch, group, flat, group_local, NULL);
        vm->kernel_loop_item = &item;
        long long ret;
        int       rc =
            cccc_call_guest_callback_ex(vm, fn, iargs, nint, fargs, nflt, &ret);
        vm->kernel_loop_item = NULL;
        if (rc != 0)
            kernel_fatal("work-item (%zu,%zu,%zu) faulted", item.global_id[0],
                         item.global_id[1], item.global_id[2]);
    }
}

static void run_group_threads(VirtualMachine *vm, long long fn,
                              const long long *iargs, int nint,
                              const double *fargs, int nflt,
                              const KernelLaunch *launch, const size_t group[3],
                              size_t group_items, char *group_local) {
    KernelItem   *items = calloc(group_items, sizeof(*items));
    void        **ptrs  = calloc(group_items, sizeof(*ptrs));
    KernelBarrier barrier;
    if (!items || !ptrs)
        kernel_fatal("out of memory");
    pthread_mutex_init(&barrier.mutex, NULL);
    pthread_cond_init(&barrier.cond, NULL);
    barrier.active     = (int)group_items;
    barrier.arrived    = 0;
    barrier.generation = 0;
    for (size_t flat = 0; flat < group_items; flat++) {
        set_item(&items[flat], launch, group, flat, group_local, &barrier);
        ptrs[flat] = &items[flat];
    }
    int rc = cccc_run_kernel_threads(vm, fn, iargs, nint, fargs, nflt, ptrs,
                                     (int)group_items, item_exit);
    pthread_cond_destroy(&barrier.cond);
    pthread_mutex_destroy(&barrier.mutex);
    free(ptrs);
    free(items);
    if (rc != 0)
        kernel_fatal("a work-item of group (%zu,%zu,%zu) faulted", group[0],
                     group[1], group[2]);
}

// __cccc_launch(kernel, global, local, int_args, float_args, nint, nfloat,
//               local_arg_mask)
// Emitted by __builtin_kernel_launch. A set bit i of local_arg_mask marks
// int_args[i] as a CCCC_LOCAL size to be replaced by a pointer into the
// work-group's local memory.
static long long wrap_launch(long long kernel, long long globalp,
                             long long localp, long long int_argsp,
                             long long float_argsp, long long nint,
                             long long nflt, long long local_mask) {
    VirtualMachine *vm   = cccc_current_ffi_vm();
    KernelMeta     *meta = hashmap_get_int(&vm->compiler.kernel_meta, kernel);
    if (!meta)
        kernel_fatal("launching a function that is not a [[cccc::kernel]]");

    KernelLaunch launch;
    unsigned     local_dims;
    read_range((const GuestRange *)globalp, "global", &launch.dims,
               launch.global);
    read_range((const GuestRange *)localp, "local", &local_dims, launch.local);
    if (local_dims != launch.dims)
        kernel_fatal("global range has %u dimension(s), local range has %u",
                     launch.dims, local_dims);

    size_t group_items = 1;
    for (int d = 0; d < 3; d++) {
        if (launch.global[d] % launch.local[d])
            kernel_fatal("local size %zu does not divide global size %zu in "
                         "dimension %d",
                         launch.local[d], launch.global[d], d);
        launch.groups[d]  = launch.global[d] / launch.local[d];
        group_items      *= launch.local[d];
    }
    size_t max_group = vm->kernel_max_group > 0 ? (size_t)vm->kernel_max_group
                                                : KERNEL_DEFAULT_MAX_GROUP;
    if (group_items > max_group)
        kernel_fatal("work-group of %zu items exceeds the limit of %zu "
                     "(--kernel-max-group-size)",
                     group_items, max_group);

    long long iargs[8];
    double    fargs[8];
    memcpy(iargs, (void *)int_argsp, sizeof(long long) * (size_t)nint);
    memcpy(fargs, (void *)float_argsp, sizeof(double) * (size_t)nflt);

    size_t total = align_up((size_t)meta->local_bytes, KERNEL_LOCAL_ALIGN);
    for (int i = 0; i < nint; i++)
        if (local_mask & (1LL << i))
            total += align_up((size_t)iargs[i], KERNEL_LOCAL_ALIGN);
    char *group_local = NULL;
    if (total) {
        group_local = cccc_vm_heap_malloc(vm, (long long)total);
        if (!group_local)
            kernel_fatal("out of memory for work-group local memory");
    }

    size_t offset = align_up((size_t)meta->local_bytes, KERNEL_LOCAL_ALIGN);
    for (int i = 0; i < nint; i++) {
        if (!(local_mask & (1LL << i)))
            continue;
        size_t size  = align_up((size_t)iargs[i], KERNEL_LOCAL_ALIGN);
        iargs[i]     = (long long)(group_local + offset);
        offset      += size;
    }

    size_t group[3];
    for (group[2] = 0; group[2] < launch.groups[2]; group[2]++)
        for (group[1] = 0; group[1] < launch.groups[1]; group[1]++)
            for (group[0] = 0; group[0] < launch.groups[0]; group[0]++) {
                if (group_local)
                    memset(group_local, 0, total);
                if (meta->uses_barrier)
                    run_group_threads(vm, kernel, iargs, (int)nint, fargs,
                                      (int)nflt, &launch, group, group_items,
                                      group_local);
                else
                    run_group_loop(vm, kernel, iargs, (int)nint, fargs,
                                   (int)nflt, &launch, group, group_items,
                                   group_local);
            }

    if (group_local)
        cccc_vm_heap_free(vm, group_local);
    return 0;
}

static void **direct_blocks;
static size_t direct_block_count;

static void free_direct_blocks(void) {
    for (size_t i = 0; i < direct_block_count; i++)
        free(direct_blocks[i]);
    free(direct_blocks);
}

// Address of a [[cccc::local]] object. Outside a launch (a kernel called like
// an ordinary function) each object gets its own block, freed at exit.
static long long wrap_local_base(long long off, long long size, long long id) {
    VirtualMachine *vm   = cccc_current_ffi_vm();
    KernelItem     *item = current_item(vm);
    if (item && item->group_local)
        return (long long)(item->group_local + off);
    char *block = hashmap_get_int(&vm->compiler.kernel_direct_local, id);
    if (!block) {
        block = calloc(1, (size_t)size);
        void **grown =
            realloc(direct_blocks, (direct_block_count + 1) * sizeof(void *));
        if (!block || !grown)
            kernel_fatal("out of memory for local memory");
        if (!direct_blocks)
            atexit(free_direct_blocks);
        direct_blocks                       = grown;
        direct_blocks[direct_block_count++] = block;
        hashmap_put_int(&vm->compiler.kernel_direct_local, id, block);
    }
    return (long long)block;
}

void cccc_kernel_register_meta(VirtualMachine *vm, long long fn_value,
                               bool uses_barrier, int local_bytes) {
    KernelMeta *meta = hashmap_get_int(&vm->compiler.kernel_meta, fn_value);
    if (!meta) {
        meta = calloc(1, sizeof(*meta));
        hashmap_put_int(&vm->compiler.kernel_meta, fn_value, meta);
    }
    meta->uses_barrier = uses_barrier;
    meta->local_bytes  = local_bytes;
}

void register_kernel_functions(VirtualMachine *vm) {
    cc_register_cfunc(vm, "cccc_global_id", (void *)wrap_cccc_global_id, 1, 0);
    cc_register_cfunc(vm, "cccc_local_id", (void *)wrap_cccc_local_id, 1, 0);
    cc_register_cfunc(vm, "cccc_group_id", (void *)wrap_cccc_group_id, 1, 0);
    cc_register_cfunc(vm, "cccc_global_size", (void *)wrap_cccc_global_size, 1,
                      0);
    cc_register_cfunc(vm, "cccc_local_size", (void *)wrap_cccc_local_size, 1,
                      0);
    cc_register_cfunc(vm, "cccc_num_groups", (void *)wrap_cccc_num_groups, 1,
                      0);
    cc_register_cfunc(vm, "cccc_work_dim", (void *)wrap_cccc_work_dim, 0, 0);
    cc_register_cfunc(vm, "cccc_barrier", (void *)wrap_cccc_barrier, 1, 0);
    cc_register_cfunc(vm, "__cccc_launch", (void *)wrap_launch, 8, 0);
    cc_register_cfunc(vm, "__cccc_local_base", (void *)wrap_local_base, 3, 0);
}
