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

static long long wrap_qsort(long long base, long long nmemb, long long size, long long compar) {
    long long saved_compar = g_qsort_compar_value;
    int saved_faulted = g_qsort_compar_faulted;
    g_qsort_compar_value = compar;
    g_qsort_compar_faulted = 0;

    qsort((void *)base, (size_t)nmemb, (size_t)size, qsort_compar_trampoline);

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

static long long wrap_exit(long long status) {
    VirtualMachine *vm = cccc_current_ffi_vm();
    if (vm)
        drain_exit_handlers_nested(vm, &vm->atexit_handlers, &vm->atexit_count);
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

// Wrappers for int-returning functions that can return negative values
static long long wrap_atoi(long long s)                      { return (long long)atoi((const char *)s); }
static long long wrap_system(long long cmd)                  { return (long long)system((const char *)cmd); }
static long long wrap_mblen(long long s, long long n)        { return (long long)mblen((const char *)s, (size_t)n); }
static long long wrap_mbtowc(long long pwc, long long s, long long n) { return (long long)mbtowc((wchar_t *)pwc, (const char *)s, (size_t)n); }
static long long wrap_wctomb(long long s, long long wc)      { return (long long)wctomb((char *)s, (wchar_t)wc); }

// C23: free_sized/free_aligned_sized - a conforming implementation may
// simply call free(), ignoring the size/alignment hints.
static void cccc_free_sized(void *ptr, size_t size) {
    (void)size;
    free(ptr);
}

static void cccc_free_aligned_sized(void *ptr, size_t alignment, size_t size) {
    (void)alignment;
    (void)size;
    free(ptr);
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

    // Random number generation
    cc_register_cfunc(vm, "rand", (void*)rand, 0, 0);
    cc_register_cfunc(vm, "srand", (void*)srand, 1, 0);

    // Apple Blocks extension: heap-copy a block descriptor for escape
    cc_register_cfunc(vm, "__cccc_block_copy_impl", (void*)cccc_block_copy_impl, 1, 0);

    // Memory allocation functions
#ifdef CCCC_HAVE_NATIVE_ALIGNED_ALLOC
    cc_register_cfunc(vm, "aligned_alloc", (void*)aligned_alloc, 2, 0);
#else
    cc_register_cfunc(vm, "aligned_alloc", (void*)cccc_aligned_alloc, 2, 0);
#endif
    cc_register_cfunc(vm, "calloc", (void*)calloc, 2, 0);
    cc_register_cfunc(vm, "free", (void*)free, 1, 0);
    cc_register_cfunc(vm, "free_sized", (void*)cccc_free_sized, 2, 0);
    cc_register_cfunc(vm, "free_aligned_sized", (void*)cccc_free_aligned_sized, 3, 0);
    cc_register_cfunc(vm, "malloc", (void*)malloc, 1, 0);
    cc_register_cfunc(vm, "realloc", (void*)cccc_realloc, 2, 0);  // Use wrapper for C11 semantics
    cc_register_cfunc(vm, "reallocarray", (void*)cccc_reallocarray, 3, 0);  // Portable polyfill (#699)
    cc_register_cfunc(vm, "posix_memalign", (void*)posix_memalign, 3, 0);
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
