#ifndef CCCC_COMPTIME_POLICY_H
#define CCCC_COMPTIME_POLICY_H

#include <stdbool.h>
#include <stddef.h>

typedef struct VirtualMachine VirtualMachine;
typedef struct Node           Node;
typedef struct Type           Type;
typedef struct CcBuiltin      CcBuiltin;

// Capability classes for native calls made while comptime code runs.
// CT_SAFE is always granted; the rest need --comptime-allow.
typedef enum {
    CT_SAFE = 0,
    CT_FS,
    CT_EXEC,
    CT_NET,
    CT_ENV,
    CT_DLOPEN,
    CT_THREAD,
    CT_SIGNAL,
    CT_IPC,
    CT_CLASS_COUNT,
} ComptimeClass;

// Bit 31 of a grant mask also admits names that no class lists.
#define COMPTIME_ALLOW_UNCLASSIFIED (1u << 31)
#define COMPTIME_ALLOW_ALL          (~0u)

// Class of a native function name, or -1 when no class lists it.
int cccc_comptime_class_of(const char *name);

const char *cccc_comptime_class_name(ComptimeClass cls);

// Parses a comma-separated --comptime-allow list into *mask (OR-ed in).
// Returns false and sets *bad to the offending item on an unknown class.
bool cccc_comptime_parse_classes(const char *list, unsigned *mask, char *bad,
                                 int bad_size);

// True when `mask` grants `name`. *cls_out receives the name's class (-1 when
// unclassified).
bool cccc_comptime_name_allowed(unsigned mask, const char *name, int *cls_out);

// Stops compilation with an error when comptime code is running and its grant
// mask does not cover `name`. No-op outside comptime.
void cccc_comptime_enforce(VirtualMachine *vm, const char *name);

// True when `name` is a <math.h> function, including its `f`/`l` variants.
bool cccc_comptime_is_math_name(const char *name, size_t len);

// --- Pointer validation (comptime_args.c) ---

// Pointer-argument spec for a safe-class native function, "" when it takes no
// pointers, NULL when the name has no spec.
const char *cccc_comptime_arg_spec(const char *name);

// While comptime code runs: checks a native call's pointer arguments against
// VM memory. Prints a diagnostic and returns -1 on a violation.
int cccc_comptime_check_args(VirtualMachine *vm, const char *name,
                             long long *args, int nargs);

// Rewrites a native call's result: host strings and structs become VM-heap
// copies and opaque handles are remembered.
void cccc_comptime_after_call(VirtualMachine *vm, const char *name,
                              long long *args, int nargs, long long *result);

// Host allocations and handles comptime code may pass back to libc.
void cccc_comptime_host_ptr_add(VirtualMachine *vm, void *p);
bool cccc_comptime_host_ptr_take(VirtualMachine *vm, void *p);

// The per-VM handle and host-pointer maps; freed by cc_destroy.
void cccc_comptime_registry_init(VirtualMachine *vm);
void cccc_comptime_registry_free(VirtualMachine *vm);

// Host memory libc keeps owning but comptime code may read (and write when
// `writable`) until the release call drops its `owner`. A new range replaces
// any range it overlaps.
void cccc_comptime_host_range_add(const void *base, size_t len,
                                  const void *owner, bool writable);
void cccc_comptime_host_range_drop(const void *owner);
size_t cccc_comptime_host_range_left(const void *p, bool write);

// A tsearch/tfind node (its key slot) libc owns; `tree` is the root variable
// passed to the call. `tree` NULL in _known matches a node of any tree.
void cccc_comptime_host_node_add(const void *node, const void *tree);
bool cccc_comptime_host_node_known(const void *node, const void *tree);

// Range helpers for callback trampolines, which hand libc-owned memory to guest
// code: a `struct dirent *`, an `FTSENT *`, a NUL-terminated string.
void cccc_comptime_host_dirent(const void *e, const void *owner);
void cccc_comptime_host_ftsent(const void *e, const void *owner);
void cccc_comptime_host_string(const void *s, const void *owner);

// True when `p` lies in a VM segment (host ranges do not count).
bool cccc_comptime_in_vm(VirtualMachine *vm, const void *p);

// VM-memory containment (ops.c). `p` must lie wholly inside a VM segment or a
// registered host range.
bool cccc_comptime_addr_ok(VirtualMachine *vm, const void *p, size_t n,
                           bool write);
long long cccc_comptime_cstr_len(VirtualMachine *vm, const void *p,
                                 size_t limit);
int cccc_comptime_fault(VirtualMachine *vm, const char *what, const void *p,
                        size_t n, bool write);

int cccc_comptime_abort(VirtualMachine *vm, const char *msg);

// Opaque compiler handles (Node *, Type *, ...) handed to comptime code. A
// builtin rejects any handle it was not given. NULL is always accepted.
void cccc_comptime_handle_add(VirtualMachine *vm, const void *p, int kind);
bool cccc_comptime_handle_ok(VirtualMachine *vm, const void *p, int kind);
void cccc_comptime_handle_add_nodes(VirtualMachine *vm, Node *first);

// Reads builtin `name`'s handle, string and buffer parameters from its
// reflection.h prototype, declared in `file`.
void cccc_builtin_note_signature(const char *name, const Type *fn_ty,
                                 const char *file);
// Checks a builtin's handle arguments; prints a diagnostic and returns -1 on a
// forged one.
int cccc_comptime_check_builtin(VirtualMachine *vm, const CcBuiltin *b,
                                long long *args, int nargs);
// Remembers the handles a builtin returns.
void cccc_comptime_note_builtin_result(VirtualMachine *vm, const CcBuiltin *b,
                                       long long result);

#endif
