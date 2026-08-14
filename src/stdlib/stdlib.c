// stdlib.h stdlib function registration
#include "../cccc.h"
#include "../internal.h"
#include <wchar.h>
#include <string.h>
#include <errno.h>

#if defined(__APPLE__)
#include <Availability.h>
#endif

// aligned_alloc native availability: macOS 10.15+ or glibc 2.16+
#if defined(__APPLE__) && defined(__MAC_OS_X_VERSION_MIN_REQUIRED) && \
    __MAC_OS_X_VERSION_MIN_REQUIRED >= __MAC_10_15
#define CCCC_HAVE_NATIVE_ALIGNED_ALLOC 1
#elif defined(__GLIBC__)
#include <features.h>
#if __GLIBC_PREREQ(2, 16)
#define CCCC_HAVE_NATIVE_ALIGNED_ALLOC 1
#endif
#endif

#ifndef CCCC_HAVE_NATIVE_ALIGNED_ALLOC
static void *cccc_aligned_alloc(size_t alignment, size_t size) {
    void *ptr = NULL;
    if (alignment == 0 || (alignment & (alignment - 1)) != 0)
        return NULL;
    if (posix_memalign(&ptr, alignment, size) != 0)
        return NULL;
    return ptr;
}
#endif

// Block_copy implementation: heap-duplicate a stack-allocated block descriptor.
// Descriptor layout: [invoke_ptr(0) | byte_size(8) | captures...]
// Reads byte_size from slot 1, mallocs that many bytes, and copies the descriptor.
static void *cccc_block_copy_impl(void *desc) {
    if (!desc) return NULL;
    size_t size = (size_t)((long long *)desc)[1];
    void *copy = malloc(size);
    if (copy) memcpy(copy, desc, size);
    return copy;
}

// ---------------------------------------------------------------------------
// qsort/bsearch comparator callback (#738)
//
// qsort()/bsearch() invoke their compar argument synchronously, once per
// comparison, from inside the host call -- exactly the pattern that used to
// crash (a raw guest function-pointer value handed straight to a host API
// that calls it as real machine code). qsort_compar_trampoline is a genuine
// host C function with the right ABI; it reads which guest callback to
// invoke from a thread-local slot and drives it via cccc_call_guest_callback
// (see internal.h / vm.c), which re-enters vm_eval on this same C stack.
//
// The slot is saved/restored around each host call rather than written
// once, because a guest comparator that itself calls qsort()/bsearch() (a
// nested sort) would otherwise clobber the outer call's callback mid-sort.
// If the guest comparator faults, qsort_compar_trampoline can't propagate
// that through qsort()'s void return, so it latches the failure and returns
// 0 (keeping the sort well-defined) and wrap_qsort/wrap_bsearch surface it
// via errno after the host call returns.
static _Thread_local long long g_qsort_compar_value;
static _Thread_local int g_qsort_compar_faulted;

static int qsort_compar_trampoline(const void *a, const void *b) {
    VirtualMachine *vm = cccc_current_ffi_vm();
    long long args[2] = { (long long)(intptr_t)a, (long long)(intptr_t)b };
    long long result = 0;
    if (!vm || cccc_call_guest_callback(vm, g_qsort_compar_value, args, 2, &result) != 0) {
        g_qsort_compar_faulted = 1;
        return 0;
    }
    return (int)result;
}

// #769: preserve the #653 type shadow across a host qsort() call instead of
// unconditionally clearing it (the pre-#769 default via ffi_shadow_backstop,
// still what happens for any array that doesn't qualify below). qsort only
// reorders whole `size`-byte elements -- it never rewrites their bytes -- so
// if every element's shadow byte pattern already matches element 0's before
// the call, any permutation the host applies leaves that property intact
// and the shadow needs no clear at all; a range that isn't uniform going in
// instead gets a pre-clear narrowed to [base, base+nmemb*size) (the exact
// range a host qsort() can touch -- everything the comparator itself might
// write is guest-tracked separately, see below).
//
// The post-call check runs *unconditionally*, not just when the pre-check
// found a uniform range: the comparator runs guest code via
// qsort_compar_trampoline/cccc_call_guest_callback above, and a comparator
// that writes through its `const void *` arguments mid-sort stamps the
// shadow at that element's *pre-move* position -- including on the
// pre-cleared (all-TY_VOID, and therefore trivially "uniform") path, where
// skipping the post-check would let that stray stamp survive at a stale
// position after qsort relocates the bytes. Checking unconditionally costs
// nothing extra in the common well-behaved-comparator case: a pre-cleared,
// untouched range reads back all-TY_VOID, which is itself uniform, so the
// post-check finds nothing to clear.
//
// Residual (accepted, LOWPRI ticket): a comparator that writes a
// differently-typed value through its arguments and is then itself called
// again before the sort completes could see a stale shadow for the
// remainder of that sort -- the clear only happens once, after wrap_qsort
// returns. Not worth a CHKT3 suppression range on the hot path for this.
static long long wrap_qsort(long long base, long long nmemb, long long size, long long compar) {
    long long saved_compar = g_qsort_compar_value;
    int saved_faulted = g_qsort_compar_faulted;
    g_qsort_compar_value = compar;
    g_qsort_compar_faulted = 0;

    VirtualMachine *vm = cccc_current_ffi_vm();
    if (vm && !cc_type_shadow_elements_uniform(vm, (void *)base, (size_t)nmemb, (size_t)size))
        cc_type_shadow_clear_range(vm, (void *)base, (size_t)nmemb * (size_t)size);

    qsort((void *)base, (size_t)nmemb, (size_t)size, qsort_compar_trampoline);

    if (vm && !cc_type_shadow_elements_uniform(vm, (void *)base, (size_t)nmemb, (size_t)size))
        cc_type_shadow_clear_range(vm, (void *)base, (size_t)nmemb * (size_t)size);

    int faulted = g_qsort_compar_faulted;
    g_qsort_compar_value = saved_compar;
    g_qsort_compar_faulted = saved_faulted;
    if (faulted)
        errno = EFAULT;
    return 0;
}

static long long wrap_bsearch(long long key, long long base, long long nmemb,
                              long long size, long long compar) {
    long long saved_compar = g_qsort_compar_value;
    int saved_faulted = g_qsort_compar_faulted;
    g_qsort_compar_value = compar;
    g_qsort_compar_faulted = 0;

    void *result = bsearch((void *)key, (void *)base, (size_t)nmemb, (size_t)size,
                           qsort_compar_trampoline);

    int faulted = g_qsort_compar_faulted;
    g_qsort_compar_value = saved_compar;
    g_qsort_compar_faulted = saved_faulted;
    if (faulted)
        errno = EFAULT;
    return (long long)(intptr_t)result;
}

// ---------------------------------------------------------------------------
// atexit()/at_quick_exit() (#738)
//
// atexit/at_quick_exit used to be raw passthroughs to the real host
// atexit()/at_quick_exit(), which stashed the guest function-pointer value
// straight into the HOST's own libc exit table. That guest value is not a
// real host-callable pointer, so it crashed the moment libc's real exit
// sequence (real process exit -- which always eventually happens, whether
// or not the guest program itself calls exit()) invoked it as machine code.
//
// Handlers are now tracked in a VM-owned list (vm->atexit_handlers /
// at_quick_exit_handlers in cccc.h) and drained explicitly, in LIFO order,
// by wrap_exit/wrap_quick_exit below via cccc_call_guest_callback -- safe
// here because wrap_exit/wrap_quick_exit run mid-vm_eval, GIL held, same as
// any other FFI call. Normal return from main() (no explicit exit() call)
// is a DIFFERENT context -- the GIL has already been released by the time
// cc_run_at(main) returns -- so that path (vm.c, cc_run) drains the same
// list with a top-level cc_run_at cycle per handler instead, matching how
// constructors/destructors already run.
static int push_exit_handler(long long **list, int *count, int *cap, long long fn_value) {
    if (*count >= *cap) {
        int new_cap = *cap ? *cap * 2 : 8;
        long long *grown = realloc(*list, (size_t)new_cap * sizeof(long long));
        if (!grown)
            return -1;
        *list = grown;
        *cap = new_cap;
    }
    (*list)[(*count)++] = fn_value;
    return 0;
}

static long long wrap_atexit(long long fn_value) {
    VirtualMachine *vm = cccc_current_ffi_vm();
    if (!vm)
        return -1;
    return push_exit_handler(&vm->atexit_handlers, &vm->atexit_count,
                             &vm->atexit_cap, fn_value);
}

static long long wrap_at_quick_exit(long long fn_value) {
    VirtualMachine *vm = cccc_current_ffi_vm();
    if (!vm)
        return -1;
    return push_exit_handler(&vm->at_quick_exit_handlers, &vm->at_quick_exit_count,
                             &vm->at_quick_exit_cap, fn_value);
}

// Drain a handler list in LIFO (reverse registration) order via a nested,
// mid-vm_eval guest callback -- the caller (wrap_exit/wrap_quick_exit) is
// itself running as an FFI call, GIL held, so this is exactly the context
// cccc_call_guest_callback is for.
//
// A running handler may itself register another one (e.g. atexit() called
// from inside an atexit handler) -- both *count and *list_ptr are
// re-dereferenced every iteration rather than captured once, since
// push_exit_handler can realloc() the array out from under a stale local
// copy of the pointer, and the freshly-pushed entry must become the new top
// of the list and run next -- matching real glibc's observed order
// (verified against a native build) and cc_run_atexit_entries's (vm.c)
// identical top-level-context version of this same loop.
static void drain_exit_handlers_nested(VirtualMachine *vm, long long **list_ptr, int *count) {
    while (*count > 0) {
        long long fn_value = (*list_ptr)[--*count];
        long long ignored;
        cccc_call_guest_callback(vm, fn_value, NULL, 0, &ignored);
        /* A faulting handler doesn't abort the drain -- the remaining
           handlers still deserve a chance to run, matching how a real
           atexit handler that segfaults doesn't retroactively un-register
           the ones registered before it. */
    }
}

// #680: run __attribute__((destructor)) functions from an explicit guest
// exit() call, the same way cc_run_init_entries(dtor_list) does on normal
// return from main() (vm.c) -- except here each destructor is invoked via
// cccc_call_guest_callback's nested-reentry path since wrap_exit is itself
// running mid-vm_eval with the GIL held (same reasoning as
// drain_exit_handlers_nested above). Guarded by vm->run_started so
// -t/--testing and -r/--repl -- which never call cc_run(), so constructors
// never ran -- don't run destructors either. Guarded by vm->dtors_drained,
// set *before* the loop runs, so a destructor that itself calls exit()
// re-enters wrap_exit, finds the flag already set, and falls straight
// through to the real host exit() instead of re-draining.
static void drain_destructors_nested(VirtualMachine *vm) {
    if (!vm->run_started || vm->dtors_drained)
        return;
    vm->dtors_drained = true;
    CCCCInitEntry *list = vm->compiler.dtor_list;
    int count = vm->compiler.dtor_count;
    for (int i = 0; i < count; i++) {
        long long ignored;
        // code_addr is already a Pc (text_seg index, see codegen.c's
        // fn->code_addr = vm->text_ptr + 1) -- cccc_call_guest_callback
        // expects a guest function-pointer VALUE and converts it back to a
        // Pc via cc_byte_offset_to_pc internally, so it must be re-encoded
        // as a byte offset here first.
        cccc_call_guest_callback(vm, cc_pc_to_byte_offset((Pc)list[i].code_addr),
                                 NULL, 0, &ignored);
        /* A faulting destructor doesn't abort the drain, matching
           drain_exit_handlers_nested's identical policy above. */
    }
}

static long long wrap_exit(long long status) {
    VirtualMachine *vm = cccc_current_ffi_vm();
    if (vm) {
        drain_exit_handlers_nested(vm, &vm->atexit_handlers, &vm->atexit_count);
        drain_destructors_nested(vm);
    }
    exit((int)status);
    return 0; /* unreachable */
}

static long long wrap_quick_exit(long long status) {
    VirtualMachine *vm = cccc_current_ffi_vm();
    if (vm)
        drain_exit_handlers_nested(vm, &vm->at_quick_exit_handlers, &vm->at_quick_exit_count);
    quick_exit((int)status);
    return 0; /* unreachable */
}

// Wrapper for realloc that matches C11 semantics
static void *cccc_realloc(void *ptr, size_t size) {
    if (size == 0) {
        free(ptr);
        return NULL;
    }
    return realloc(ptr, size);
}

// Portable reallocarray polyfill (#699): not every host libc has it (e.g.
// this SDK's macOS -- glibc >= 2.26 and some BSDs do). The overflow check on
// nmemb*size is the entire point of reallocarray over realloc(ptr,
// nmemb*size); relying on a native symbol that may not exist would either
// fail to link or (worse) silently drop that check on hosts that lack it.
// Used for the --no-vm-heap FFI path; the VM-heap path (default) instead
// routes through the overflow-checked REALCA opcode in ops.c.
static void *cccc_reallocarray(void *ptr, size_t nmemb, size_t size) {
    if (size != 0 && nmemb > (SIZE_MAX / size)) {
        errno = ENOMEM;
        return NULL;
    }
    return cccc_realloc(ptr, nmemb * size);
}

// #865: malloc/free/calloc/realloc/reallocarray/aligned_alloc/
// posix_memalign taken as function-pointer VALUES (rather than called
// directly by name) bypass codegen's VM-heap opcode routing
// (is_extern_func_name's special-casing in codegen.c, guarded by
// CCCC_VM_HEAP) and previously resolved to the raw host libc function
// registered below -- fatal for free/realloc, since the bytes preceding a
// VM-heap pointer are cccc's own AllocHeader, not a real libmalloc chunk,
// and real free()/realloc() abort on it. These wrappers restore parity with
// the direct-call path: same CCCC_VM_HEAP flag check codegen uses for the
// allocating functions, and the same VM-heap-vs-host-pointer fallback
// cccc_vm_heap_free/cccc_vm_heap_realloc (ops.c) already perform for free/
// realloc regardless of the flag. cccc_current_ffi_vm() returns NULL outside
// any VM context, which can't happen for a call reached through the VM's own
// dispatch loop, but is handled defensively (falls back to the host
// allocator) rather than assumed away.
static void *cccc_ffi_malloc(size_t size) {
    VirtualMachine *vm = cccc_current_ffi_vm();
    if (vm && (vm->flags & CCCC_VM_HEAP))
        return cccc_vm_heap_malloc(vm, (long long)size);
    return malloc(size);
}

static void cccc_ffi_free(void *ptr) {
    VirtualMachine *vm = cccc_current_ffi_vm();
    if (!vm) {
        free(ptr);
        return;
    }
    // cccc_vm_heap_free already falls back to the real free() for any
    // pointer outside the VM heap arena or without a valid AllocHeader, so
    // no separate CCCC_VM_HEAP flag check is needed here -- unlike the
    // allocating functions, whose result depends on the flag.
    if (cccc_vm_heap_free(vm, ptr) != 0)
        // A double-free/canary-corruption detected via the direct MFRE
        // opcode path traps into the debugger (auto-debug-on-crash) or
        // returns a VM error cleanly; from here -- several native C frames
        // below the FFI dispatch, with no such return path available --
        // the best available option is a hard exit with the diagnostic
        // cccc_vm_heap_free already printed above.
        error("free(): heap safety violation detected via an indirect call (see diagnostic above)");
}

static void *cccc_ffi_calloc(size_t nmemb, size_t size) {
    VirtualMachine *vm = cccc_current_ffi_vm();
    if (vm && (vm->flags & CCCC_VM_HEAP))
        return cccc_vm_heap_calloc(vm, (long long)nmemb, (long long)size);
    return calloc(nmemb, size);
}

static void *cccc_ffi_realloc(void *ptr, size_t size) {
    VirtualMachine *vm = cccc_current_ffi_vm();
    if (vm && (vm->flags & CCCC_VM_HEAP))
        return cccc_vm_heap_realloc(vm, ptr, (long long)size);
    return cccc_realloc(ptr, size);
}

static void *cccc_ffi_reallocarray(void *ptr, size_t nmemb, size_t size) {
    VirtualMachine *vm = cccc_current_ffi_vm();
    if (vm && (vm->flags & CCCC_VM_HEAP))
        return cccc_vm_heap_reallocarray(vm, ptr, (long long)nmemb, (long long)size);
    return cccc_reallocarray(ptr, nmemb, size);
}

static void *cccc_ffi_aligned_alloc(size_t alignment, size_t size) {
    VirtualMachine *vm = cccc_current_ffi_vm();
    if (vm && (vm->flags & CCCC_VM_HEAP))
        return cccc_vm_heap_malloc_aligned(vm, (long long)size, alignment);
#ifdef CCCC_HAVE_NATIVE_ALIGNED_ALLOC
    return aligned_alloc(alignment, size);
#else
    return cccc_aligned_alloc(alignment, size);
#endif
}

static int cccc_ffi_posix_memalign(void **memptr, size_t alignment, size_t size) {
    VirtualMachine *vm = cccc_current_ffi_vm();
    if (vm && (vm->flags & CCCC_VM_HEAP))
        return cccc_vm_heap_posix_memalign(vm, memptr, alignment, (long long)size);
    return posix_memalign(memptr, alignment, size);
}

// Wrappers for int-returning functions that can return negative values
static long long wrap_atoi(long long s)                      { return (long long)atoi((const char *)s); }
static long long wrap_system(long long cmd)                  { return (long long)system((const char *)cmd); }
static long long wrap_setenv(long long name, long long value, long long overwrite) { return (long long)setenv((const char *)name, (const char *)value, (int)overwrite); }
static long long wrap_unsetenv(long long name)                { return (long long)unsetenv((const char *)name); }
static long long wrap_putenv(long long string)                { return (long long)putenv((char *)string); }
static long long wrap_mblen(long long s, long long n)        { return (long long)mblen((const char *)s, (size_t)n); }
static long long wrap_mbtowc(long long pwc, long long s, long long n) { return (long long)mbtowc((wchar_t *)pwc, (const char *)s, (size_t)n); }
static long long wrap_wctomb(long long s, long long wc)      { return (long long)wctomb((char *)s, (wchar_t)wc); }

// C23: free_sized/free_aligned_sized - a conforming implementation may
// simply call free(), ignoring the size/alignment hints. Taken as a
// function-pointer value and called indirectly, these have the exact same
// #865 exposure as free() itself (codegen's direct-call routing to MFRE is
// bypassed), so they go through cccc_ffi_free rather than raw free().
static void cccc_free_sized(void *ptr, size_t size) {
    (void)size;
    cccc_ffi_free(ptr);
}

static void cccc_free_aligned_sized(void *ptr, size_t alignment, size_t size) {
    (void)alignment;
    (void)size;
    cccc_ffi_free(ptr);
}

// C23: memalignment - the largest power-of-two alignment satisfied by p.
static size_t cccc_memalignment(const void *p) {
    if (!p)
        return 0;
    return (size_t)1 << __builtin_ctzll((unsigned long long)(uintptr_t)p);
}

// C23: strtol/strtoll/strtoul/strtoull accept an optional "0b"/"0B" prefix
// when base is 0 or 2.
static long long cccc_strtoll(const char *nptr, char **endptr, int base) {
    if (base == 0 || base == 2) {
        const char *p = nptr;
        while (isspace((unsigned char)*p)) p++;
        const char *digits = p;
        if (*digits == '+' || *digits == '-') digits++;
        if (digits[0] == '0' && (digits[1] == 'b' || digits[1] == 'B') &&
            (digits[2] == '0' || digits[2] == '1')) {
            size_t prefix_len = (size_t)(digits - p);
            size_t rest_len = strlen(digits + 2);
            char *tmp = malloc(prefix_len + rest_len + 1);
            memcpy(tmp, p, prefix_len);
            memcpy(tmp + prefix_len, digits + 2, rest_len + 1);
            char *tmp_end;
            long long result = strtoll(tmp, &tmp_end, 2);
            if (endptr)
                *endptr = (char *)p + (size_t)(tmp_end - tmp) + 2;
            free(tmp);
            return result;
        }
    }
    return strtoll(nptr, endptr, base);
}

static unsigned long long cccc_strtoull(const char *nptr, char **endptr, int base) {
    if (base == 0 || base == 2) {
        const char *p = nptr;
        while (isspace((unsigned char)*p)) p++;
        const char *digits = p;
        if (*digits == '+' || *digits == '-') digits++;
        if (digits[0] == '0' && (digits[1] == 'b' || digits[1] == 'B') &&
            (digits[2] == '0' || digits[2] == '1')) {
            size_t prefix_len = (size_t)(digits - p);
            size_t rest_len = strlen(digits + 2);
            char *tmp = malloc(prefix_len + rest_len + 1);
            memcpy(tmp, p, prefix_len);
            memcpy(tmp + prefix_len, digits + 2, rest_len + 1);
            char *tmp_end;
            unsigned long long result = strtoull(tmp, &tmp_end, 2);
            if (endptr)
                *endptr = (char *)p + (size_t)(tmp_end - tmp) + 2;
            free(tmp);
            return result;
        }
    }
    return strtoull(nptr, endptr, base);
}

static long cccc_strtol(const char *nptr, char **endptr, int base) {
    return (long)cccc_strtoll(nptr, endptr, base);
}

static unsigned long cccc_strtoul(const char *nptr, char **endptr, int base) {
    return (unsigned long)cccc_strtoull(nptr, endptr, base);
}

// #832: strtod32/64/128's FFI trampoline. `long long`-uniform, like every
// other decimal shim entry point (src/stdlib/decimal.c's cccc_dec_strtod) --
// no BID type appears in a VM header. include/stdlib.h's strtod32/64/128
// static inline wrappers call this directly; no new opcode, following the
// #828 <decimal_math.h> precedent.
static long long wrap_dec_strtod(long long w, long long dst, long long s, long long endp) {
    return cccc_dec_strtod((int)w, (void *)(intptr_t)dst, (const char *)(intptr_t)s,
                           (char **)(intptr_t)endp, CCCC_DEC_ENV_DYNAMIC);
}

// Register all stdlib.h functions
void register_stdlib_functions(VirtualMachine *vm) {
    // Conversion functions
    cc_register_cfunc(vm, "atof", (void*)atof, 1, 1);       // returns double
    cc_register_cfunc(vm, "atoi", (void*)wrap_atoi, 1, 0);
    cc_register_cfunc(vm, "atol", (void*)atol, 1, 0);       // returns long
    cc_register_cfunc(vm, "atoll", (void*)atoll, 1, 0);     // returns long long
    cc_register_cfunc(vm, "strtod", (void*)strtod, 2, 1);   // returns double
    cc_register_cfunc(vm, "strtof", (void*)strtof, 2, 2);   // returns float
    cc_register_cfunc(vm, "strtold", (void*)strtold, 2, 1); // returns long double
    cc_register_cfunc(vm, "strtol", (void*)cccc_strtol, 3, 0);
    cc_register_cfunc(vm, "strtoll", (void*)cccc_strtoll, 3, 0);
    cc_register_cfunc(vm, "strtoul", (void*)cccc_strtoul, 3, 0);
    cc_register_cfunc(vm, "strtoull", (void*)cccc_strtoull, 3, 0);
    cc_register_cfunc(vm, "__cccc_dec_strtod", (void*)wrap_dec_strtod, 4, 0); // #832

    // Random number generation
    cc_register_cfunc(vm, "rand", (void*)rand, 0, 0);
    cc_register_cfunc(vm, "srand", (void*)srand, 1, 0);

    // Apple Blocks extension: heap-copy a block descriptor for escape
    cc_register_cfunc(vm, "__cccc_block_copy_impl", (void*)cccc_block_copy_impl, 1, 0);

    // Memory allocation functions -- registered under the cccc_ffi_* wrappers
    // (not the raw host functions) so that a bare malloc/free/calloc/
    // realloc/reallocarray/aligned_alloc/posix_memalign value taken as a
    // function pointer and called indirectly gets the same VM-heap-aware
    // behavior as the direct-call opcode path (MALC/MFRE/CALC/REALC/REALCA/
    // MALCA/PMEMA in ops.c), instead of crashing/silently diverging (#865).
    cc_register_cfunc(vm, "aligned_alloc", (void*)cccc_ffi_aligned_alloc, 2, 0);
    cc_register_cfunc(vm, "calloc", (void*)cccc_ffi_calloc, 2, 0);
    cc_register_cfunc(vm, "free", (void*)cccc_ffi_free, 1, 0);
    cc_register_cfunc(vm, "free_sized", (void*)cccc_free_sized, 2, 0);
    cc_register_cfunc(vm, "free_aligned_sized", (void*)cccc_free_aligned_sized, 3, 0);
    cc_register_cfunc(vm, "malloc", (void*)cccc_ffi_malloc, 1, 0);
    cc_register_cfunc(vm, "realloc", (void*)cccc_ffi_realloc, 2, 0);
    cc_register_cfunc(vm, "reallocarray", (void*)cccc_ffi_reallocarray, 3, 0);  // Portable polyfill (#699)
    cc_register_cfunc(vm, "posix_memalign", (void*)cccc_ffi_posix_memalign, 3, 0);
    cc_register_cfunc(vm, "memalignment", (void*)cccc_memalignment, 1, 0);

    // Process control
    cc_register_cfunc(vm, "abort", (void*)abort, 0, 0);
    cc_register_cfunc(vm, "exit", (void*)wrap_exit, 1, 0);
    cc_register_cfunc(vm, "_Exit", (void*)_Exit, 1, 0);
    cc_register_cfunc(vm, "atexit", (void*)wrap_atexit, 1, 0);
    cc_register_cfunc(vm, "at_quick_exit", (void*)wrap_at_quick_exit, 1, 0);
    cc_register_cfunc(vm, "quick_exit", (void*)wrap_quick_exit, 1, 0);

    // Environment
    cc_register_cfunc(vm, "getenv", (void*)getenv, 1, 0);
    cc_register_cfunc(vm, "setenv", (void*)wrap_setenv, 3, 0);
    cc_register_cfunc(vm, "unsetenv", (void*)wrap_unsetenv, 1, 0);
    cc_register_cfunc(vm, "putenv", (void*)wrap_putenv, 1, 0);
    cc_register_cfunc(vm, "system", (void*)wrap_system, 1, 0);

    // Search and sort
    cc_register_cfunc(vm, "bsearch", (void*)wrap_bsearch, 5, 0);
    cc_register_cfunc(vm, "qsort", (void*)wrap_qsort, 4, 0);

    // Integer arithmetic
    cc_register_cfunc(vm, "abs", (void*)abs, 1, 0);     // returns int (#777: was incorrectly 1/double)
    cc_register_cfunc(vm, "labs", (void*)labs, 1, 0);   // returns long
    cc_register_cfunc(vm, "llabs", (void*)llabs, 1, 0); // returns long long
    cc_register_cfunc(vm, "div", (void*)div, 2, 0);
    cc_register_cfunc(vm, "ldiv", (void*)ldiv, 2, 0);
    cc_register_cfunc(vm, "lldiv", (void*)lldiv, 2, 0);

    // Multibyte/wide character conversion
    cc_register_cfunc(vm, "mblen", (void*)wrap_mblen, 2, 0);
    cc_register_cfunc(vm, "mbtowc", (void*)wrap_mbtowc, 3, 0);
    cc_register_cfunc(vm, "wctomb", (void*)wrap_wctomb, 2, 0);
    cc_register_cfunc(vm, "mbstowcs", (void*)mbstowcs, 3, 0);
    cc_register_cfunc(vm, "wcstombs", (void*)wcstombs, 3, 0);
}
