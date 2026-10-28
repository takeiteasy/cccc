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

#include "./codegen_internal.h"
#include "./host_ffi_family.h"

// ========== FFI Helper ==========

int find_ffi_function(VirtualMachine *vm, const char *name) {
    if (!vm || !name)
        return -1;

    // Exact match only: compare using the cached name_len to avoid repeated
    // strlen. (#164)
    //
    // This used to also fall back to stripping a trailing digit suffix and
    // matching a registered *variadic* base name (e.g. "printf2" ->
    // "printf"), intended to support libc's "overloaded" names like
    // execl/execle/execlp resolving through one variadic slot. That
    // fallback wasn't scoped to those names -- any guest-defined function
    // whose name happened to end in a digit and match a registered
    // variadic base name (a guest's own `printf2`, `open2`, etc.) was
    // silently rebound to the host function instead of the guest's own
    // definition (#876). Removed: nothing in the standard headers or
    // stdlib declares a digit-suffixed name over a variadic base, so
    // there's nothing left relying on it.
    size_t len = strlen(name);
    for (int i = 0; i < vm->compiler.ffi_count; i++) {
        if (vm->compiler.ffi_table[i].name_len == len &&
            memcmp(vm->compiler.ffi_table[i].name, name, len) == 0) {
            return i;
        }
    }

    return -1;
}

const char *obj_external_name(Obj *obj) {
    return obj && obj->asm_label ? obj->asm_label : obj ? obj->name : NULL;
}

static bool is_host_family_name(const char *name) {
    static const char *const family[] = {
#define ROW(n) #n,
        CCCC_HOST_FFI_FAMILY(ROW) CCCC_HOST_FFI_FAMILY_LINUX(ROW)
#undef ROW
    };
    for (size_t i = 0; i < sizeof family / sizeof *family; i++)
        if (strcmp(name, family[i]) == 0)
            return true;
    return false;
}

// The registered native an asm label (`__asm("...")` in a real header) stands
// for: the Mach-O underscore and `$DARWIN_EXTSN`-style variant suffixes on
// macOS, glibc's `__isoc99_`/`__isoc23_` scanf and strtol redirects on Linux.
static const char *asm_label_base_name(const char *name, char *buf,
                                       size_t size) {
#ifdef __APPLE__
    if (name[0] == '_')
        name++;
    const char *suffix = strchr(name, '$');
    if (suffix && (size_t)(suffix - name) < size) {
        memcpy(buf, name, suffix - name);
        buf[suffix - name] = '\0';
        return buf;
    }
#else
    (void)buf;
    (void)size;
    if (!strncmp(name, "__isoc99_", 9))
        return name + 9;
    if (!strncmp(name, "__isoc23_", 9))
        return name + 9;
#endif
    return name;
}

// FFI slot a call to `obj` binds to. Under --use-system-headers a translated
// native (host_ffi_family.h) declared by a real, non-bundled header binds to
// its `__cccc_host_` passthrough, since that program already holds the host's
// numbers. The first declaration decides (Obj.tok), so a user prototype ahead
// of the header counts as non-bundled. Name-only lookups (find_ffi_function)
// stay canonical.
int ffi_index_for_obj(VirtualMachine *vm, Obj *obj) {
    const char *name = obj_external_name(obj);
    if (!name)
        return -1;
    char base[64];
    if (obj->asm_label && find_ffi_function(vm, name) < 0)
        name = asm_label_base_name(name, base, sizeof base);
    if (vm->compiler.use_system_headers && obj->tok && obj->tok->file &&
        is_host_family_name(name) &&
        !cc_file_is_cccc_bundled(vm, obj->tok->file->name)) {
        char host_name[64];
        snprintf(host_name, sizeof host_name, CCCC_HOST_FFI_PREFIX "%s", name);
        return find_ffi_function(vm, host_name);
    }
    return find_ffi_function(vm, name);
}

// Index of the registered host data object an undefined extern global binds
// to, or -1. Definition flags are final by codegen: a same-TU definition
// reuses the Obj and cc_link_progs copies the flags onto every alias.
int cc_cdata_index_for_obj(VirtualMachine *vm, Obj *var) {
    if (!vm->compiler.cdata_count || var->is_function || var->is_static ||
        var->is_tls || var->is_definition || var->is_tentative ||
        var->init_data)
        return -1;
    const char *name = obj_external_name(var);
    if (!name)
        return -1;
    size_t name_len = strlen(name);
#ifdef __APPLE__
    // SDK headers spell asm labels with the Mach-O underscore.
    bool stripped = var->asm_label && name[0] == '_';
#endif
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < vm->compiler.cdata_count; i++) {
            HostData *hd = &vm->compiler.cdata_table[i];
            if (hd->name_len != name_len ||
                memcmp(hd->name, name, name_len) != 0)
                continue;
            if (var->ty && var->ty->size > 0 &&
                (size_t)var->ty->size > hd->size)
                error_tok(vm, var->tok,
                          "extern '%s' is %d bytes; the host object is %zu",
                          var->name, var->ty->size, hd->size);
            return i;
        }
#ifdef __APPLE__
        if (!stripped)
            break;
        name++;
        name_len--;
#else
        break;
#endif
    }
    return -1;
}

// A guest program can define its own function whose name happens to match
// a registered FFI symbol -- e.g. `int printf(const char *fmt, ...) { ... }`
// wrapping the real one. find_ffi_function's exact-name match doesn't know
// about the guest's own definition, so calls to that name were compiled as
// CALLF to the *host* printf instead of CALL to the guest's own body,
// silently calling the wrong code (#880). A bare declaration (the ordinary
// libc case -- no body) must still resolve to FFI; only a body wins.
int ffi_index_for_callee(VirtualMachine *vm, Obj *callee) {
    if (callee && callee->body)
        return -1;
    return ffi_index_for_obj(vm, callee);
}

// First object per name, matching a front-to-back scan of the list.
static void index_global_objs(HashMap *index, Obj *prog) {
    for (Obj *obj = prog; obj; obj = obj->next)
        if (obj->name && !hashmap_get(index, obj->name))
            hashmap_put_borrowed(index, obj->name, obj);
}

bool is_extern_func_name(Node *node, const char *name) {
    if (!node || node->kind != ND_VAR || !node->var ||
        !node->var->is_function || node->var->is_definition || !node->var->name)
        return false;
    return strlen(node->var->name) == strlen(name) &&
           memcmp(node->var->name, name, strlen(name)) == 0;
}

static void add_data_reloc(VirtualMachine *vm, long long data_offset,
                           int target_segment, long long target_offset,
                           long long addend) {
    PATCH_GROW(vm, data_relocs, num_data_relocs, data_relocs_cap);
    vm->compiler.data_relocs[vm->compiler.num_data_relocs].data_offset =
        data_offset;
    vm->compiler.data_relocs[vm->compiler.num_data_relocs].target_segment =
        target_segment;
    vm->compiler.data_relocs[vm->compiler.num_data_relocs].target_offset =
        target_offset;
    vm->compiler.data_relocs[vm->compiler.num_data_relocs].addend = addend;
    vm->compiler.num_data_relocs++;
}

static void add_tls_reloc(VirtualMachine *vm, long long tls_offset,
                          int target_segment, long long target_offset,
                          long long addend) {
    PATCH_GROW(vm, tls_relocs, num_tls_relocs, tls_relocs_cap);
    vm->compiler.tls_relocs[vm->compiler.num_tls_relocs].tls_offset =
        tls_offset;
    vm->compiler.tls_relocs[vm->compiler.num_tls_relocs].target_segment =
        target_segment;
    vm->compiler.tls_relocs[vm->compiler.num_tls_relocs].target_offset =
        target_offset;
    vm->compiler.tls_relocs[vm->compiler.num_tls_relocs].addend = addend;
    vm->compiler.num_tls_relocs++;
}

// Persistent label map -- declared in codegen_internal.h (shared with
// codegen_call.c and codegen_func.c); defined here.
GlobalLabelEntry *global_label_map  = NULL;
int               num_global_labels = 0;
int               global_labels_cap = 0;

void apply_global_relocations(VirtualMachine *vm, Obj *prog, Obj *stop) {
    HashMap index   = {0};
    bool    indexed = false;
    for (Obj *var = prog; var != stop; var = var->next) {
        if (var->is_function || var->reuse_of)
            continue;

        for (Relocation *rel = var->rel; rel; rel = rel->next) {
            if (!rel->label || !*rel->label)
                error("invalid global relocation");

            long long target_offset;
            long long value;
            int       segment;

            if (!indexed) {
                index_global_objs(&index, prog);
                indexed = true;
            }
            Obj *target = hashmap_get(&index, *rel->label);
            if (target && target->reuse_of)
                target = reuse_root(target);
            if (!target) {
                // Not a global object — try the persistent label map.  This
                // handles &&label stored in a static/global initialiser, where
                // the label lives in the text segment rather than the data
                // segment (#573).
                Pc label_pc = 0;
                for (int li = 0; li < num_global_labels; li++) {
                    if (strcmp(global_label_map[li].name, *rel->label) == 0) {
                        label_pc = global_label_map[li].offset;
                        break;
                    }
                }
                if (!label_pc)
                    error("undefined relocation target: %s", *rel->label);
                segment       = 1;
                target_offset = cc_pc_to_byte_offset(label_pc);
                value         = target_offset + rel->addend;
            } else if (target->is_function) {
                Pc stub = 0;
                if (!target->body) {
                    // Undefined function: if it is an FFI/extern function,
                    // store the FFI dispatch token (CCCC_FFI_TOKEN_BASE - idx)
                    // directly, mirroring the runtime function-address path
                    // (CALLN/JMPI recognise the token).  This is how static
                    // initialisers that take the address of a libc/POSIX
                    // function resolve, e.g. SQLite's unix VFS structs full of
                    // { close, read, write, ... }
                    // (#589).  The token is segment-independent, so no data/tls
                    // reloc is recorded (it survives .c4 round-trips verbatim,
                    // exactly like the text-segment FFI case).
                    int ffi_idx = ffi_index_for_obj(vm, target);
                    if (ffi_idx < 0) {
                        // VM-lowered intrinsics (signal, longjmp, dlopen...)
                        // point at their stub instead.
                        stub = cc_intrinsic_stub_addr(vm, target);
                        if (!stub &&
                            (obj_is_reserved_builtin(target, "setjmp") ||
                             obj_is_reserved_builtin(target, "_setjmp") ||
                             obj_is_reserved_builtin(target, "sigsetjmp")))
                            error("cannot take the address of '%s'",
                                  target->name);
                        if (!stub)
                            error("unsupported relocation to undefined "
                                  "function: %s",
                                  target->name);
                    } else {
                        long long slot  = var->offset + rel->offset;
                        long long token = CCCC_FFI_TOKEN_BASE - ffi_idx;
                        if (var->is_tls)
                            *(long long *)(vm->tls_template + slot) = token;
                        else
                            *(long long *)(vm->data_seg + slot) = token;
                        continue;
                    }
                }
                segment       = 1;
                target_offset = cc_pc_to_byte_offset(
                    stub ? stub : (Pc)fn_canonical(target)->code_addr);
                value = target_offset + rel->addend;
            } else {
                // #957: a static initializer taking &g needs the same
                // defined-or-deferred check as an ordinary reference (see
                // the fourth pass in gen()) -- find_global_obj resolves by
                // name regardless of whether target is ever defined, so
                // without this an extern-declared-but-never-defined global
                // silently gets an address into an inert zero slot.
                int cd = cc_cdata_index_for_obj(vm, target);
                if (cd >= 0) {
                    long long host =
                        (long long)(intptr_t)vm->compiler.cdata_table[cd].addr +
                        rel->addend;
                    long long slot = var->offset + rel->offset;
                    if (var->is_tls)
                        *(long long *)(vm->tls_template + slot) = host;
                    else
                        *(long long *)(vm->data_seg + slot) = host;
                    continue;
                }
                if (!target->is_definition && !target->is_tentative &&
                    !target->init_data && !vm->compiler.compile_only)
                    error("undefined global: %s", target->name);
                segment       = 0;
                target_offset = target->offset;
                value = (long long)(vm->data_seg + target_offset + rel->addend);
            }

            long long slot_offset = var->offset + rel->offset;
            if (var->is_tls) {
                // TLS pointer initialiser: patch into tls_template and record
                // the reloc so it can be re-applied after .c4 load (#493).
                *(long long *)(vm->tls_template + slot_offset) = value;
                add_tls_reloc(vm, slot_offset, segment, target_offset,
                              rel->addend);
            } else {
                *(long long *)(vm->data_seg + slot_offset) = value;
                add_data_reloc(vm, slot_offset, segment, target_offset,
                               rel->addend);
            }
        }
    }
    hashmap_deinit_borrowed(&index);
}

Obj *fn_canonical(Obj *fn) {
    while (fn->reload_of)
        fn = fn->reload_of;
    return fn;
}

Obj *reuse_root(Obj *obj) {
    while (obj->reuse_of)
        obj = obj->reuse_of;
    return obj;
}

// A name two static functions share maps to this instead of either of them.
static Obj ambiguous_static_def;

void add_static_function_def(HashMap *static_defs, Obj *fn) {
    Obj *prev = hashmap_get(static_defs, fn->name);
    hashmap_put(static_defs, fn->name, prev ? &ambiguous_static_def : fn);
}

// A static target is resolved by identity. A body-less static prototype (code
// macro-generated against the TU's own static function names it that way)
// falls back to the unique static definition of that name.
Obj *find_function_definition_for_patch(HashMap *fn_defs, HashMap *static_defs,
                                        Obj *target) {
    if (target->reuse_of)
        return fn_canonical(reuse_root(target));
    Obj *def;
    if (target->is_static && target->body)
        def = target;
    else if (target->is_static) {
        def = hashmap_get(static_defs, target->name);
        if (def == &ambiguous_static_def)
            def = NULL;
    } else
        def = hashmap_get(fn_defs, target->name);
    return def ? fn_canonical(def) : NULL;
}

void add_debug_symbol(VirtualMachine *vm, char *name, long long offset,
                      Type *ty, int is_local, Obj *owner_fn, int align_log) {
    if (!(vm->flags & CCCC_ENABLE_DEBUGGER) || !name || !*name)
        return;
    if (vm->dbg.num_debug_symbols == vm->dbg.debug_symbols_cap) {
        vm->dbg.debug_symbols_cap =
            vm->dbg.debug_symbols_cap ? vm->dbg.debug_symbols_cap * 2 : 256;
        vm->dbg.debug_symbols =
            realloc(vm->dbg.debug_symbols,
                    (size_t)vm->dbg.debug_symbols_cap * sizeof(DebugSymbol));
        if (!vm->dbg.debug_symbols)
            error("out of memory for debug symbols");
    }

    DebugSymbol *sym = &vm->dbg.debug_symbols[vm->dbg.num_debug_symbols++];
    sym->name        = name;
    sym->offset      = offset;
    sym->ty          = ty;
    sym->is_local    = is_local;
    sym->scope_depth = 0;
    sym->owner_fn    = owner_fn;
    sym->align_log   = align_log;
}
// ========== Register Allocator ==========
// Simple bitmap allocator for temporary registers T0-T10

// PLACEHOLDER: file-scope statics (temp_reg_in_use, num_label_defs,
// num_label_patches, label_defs[], label_patches[]) are shared across all
// CCCC instances compiled in the same process. Move them onto the CCCC/Compiler
// struct as part of the thread-safety work tracked in ticket #139 so two CCCC
// instances (or two threads) can compile in parallel.
// Ticket: https://todo.sr.ht/~takeiteasy/cccc/161
static unsigned int temp_reg_in_use = 0;

static const int    temp_reg_map[]  = {REG_T0, REG_T1, REG_T2, REG_T3,
                                       REG_T4, REG_T5, REG_T6, REG_T7,
                                       REG_T8, REG_T9, REG_T10};
#define NUM_TEMP_REGS 11

int alloc_temp_reg(void) {
    for (int i = 0; i < NUM_TEMP_REGS; i++) {
        if (!(temp_reg_in_use & (1 << i))) {
            temp_reg_in_use |= (1 << i);
            return temp_reg_map[i];
        }
    }
    error("codegen: out of temporary registers");
    return -1;
}

void free_temp_reg(int reg) {
    for (int i = 0; i < NUM_TEMP_REGS; i++) {
        if (temp_reg_map[i] == reg) {
            temp_reg_in_use &= ~(1 << i);
            return;
        }
    }
}

// Mark a specific register as in-use (needed after function calls reset temps)
void mark_temp_reg_used(int reg) {
    for (int i = 0; i < NUM_TEMP_REGS; i++) {
        if (temp_reg_map[i] == reg) {
            temp_reg_in_use |= (1 << i);
            return;
        }
    }
}

void reset_temp_regs(void) {
    temp_reg_in_use = 0;
}

// Number of temp registers currently free. Used by the binary-op codegen to
// decide when to spill the LHS to the stack instead of holding a live temp
// across the RHS recursion (ticket #587 — bounds peak register use on deeply
// nested / right-leaning expression trees).
int temp_regs_free(void) {
    int used = 0;
    for (int i = 0; i < NUM_TEMP_REGS; i++)
        if (temp_reg_in_use & (1 << i))
            used++;
    return NUM_TEMP_REGS - used;
}

// When free temps drop to this many, the binary-op path stops reserving a
// register for the RHS result (which would stay live across the RHS subtree
// recursion → O(depth) peak) and instead spills the LHS to the stack, reusing
// dest_reg for the RHS. Leaves headroom for ops that need 1-2 temps at once
// (e.g. the float push/pop branch). TEMP_REG_SPILL_THRESHOLD itself is
// declared in codegen_internal.h (used from codegen_expr.c too).

// ========== Scalar/FP Local Promotion ==========
//
// The hottest scalar locals of a function that loops live in callee-saved
// registers (S0-S3, FS0-FS3) for the whole function instead of in their frame
// slot. Only a local that is referenced inside a loop and whose address is
// never taken qualifies, so its slot is dead and needs no write-back. The
// prologue saves the registers and the epilogue restores them; setjmp/longjmp
// save the S registers (op_SETJMP_fn).

typedef struct {
    Obj *var;
    int  score;
    bool in_loop;
    bool escapes;
} PromoCand;

// `tmp = &x` from the `x op= y` / `x++` desugar (parse_expr.c to_assign).
typedef struct {
    Obj *tmp;
    Obj *target;
} PromoAlias;

typedef struct {
    PromoCand  *cands;
    int         ncands;
    PromoAlias *aliases;
    int         naliases, aliases_cap;
    Node       *alias_addr; // the `&x` of the alias assignment being walked
    Obj       **bad_tmps;   // unnamed locals used other than as `*tmp`
    int         nbad, bad_cap;
} PromoScan;

int promoted_local_index(VirtualMachine *vm, Obj *var) {
    for (int i = 0; i < vm->compiler.promoted_count; i++)
        if (vm->compiler.promoted_locals[i] == var)
            return i;
    return -1;
}

bool is_promoted_local(VirtualMachine *vm, Obj *var) {
    return promoted_local_index(vm, var) >= 0;
}

int promoted_local_reg(VirtualMachine *vm, Obj *var) {
    int idx = promoted_local_index(vm, var);
    return idx >= 0 ? vm->compiler.promoted_regs[idx] : -1;
}

static int fp_promoted_local_index(VirtualMachine *vm, Obj *var) {
    for (int i = 0; i < vm->compiler.fp_promoted_count; i++)
        if (vm->compiler.fp_promoted_locals[i] == var)
            return i;
    return -1;
}

bool is_fp_promoted_local(VirtualMachine *vm, Obj *var) {
    return fp_promoted_local_index(vm, var) >= 0;
}

int fp_promoted_local_reg(VirtualMachine *vm, Obj *var) {
    int idx = fp_promoted_local_index(vm, var);
    return idx >= 0 ? vm->compiler.fp_promoted_regs[idx] : -1;
}

// The promoted variable `*node` stands for, when `node` is `*tmp` for a
// synthetic `tmp = &x` whose x is promoted.
Obj *promoted_deref_target(VirtualMachine *vm, Node *node) {
    if (!node || node->kind != ND_DEREF || !node->lhs ||
        node->lhs->kind != ND_VAR || !node->lhs->var)
        return NULL;
    Obj *target = node->lhs->var->promoted_alias;
    if (!target ||
        (!is_promoted_local(vm, target) && !is_fp_promoted_local(vm, target)))
        return NULL;
    return target;
}

static bool is_scalar_promotion_type(Type *ty) {
    if (!ty || ty->is_volatile || ty->is_atomic || ty->size > 8)
        return false;
    switch (ty->kind) {
        case TY_BOOL:
        case TY_CHAR:
        case TY_SHORT:
        case TY_INT:
        case TY_LONG:
        case TY_ENUM:
        case TY_PTR:
        case TY_BITINT:
            return true;
        default:
            return false;
    }
}

static bool is_float_promotion_type(Type *ty) {
    if (!ty || ty->is_volatile || ty->is_atomic || ty->size > 8)
        return false;
    return ty->kind == TY_FLOAT || ty->kind == TY_DOUBLE ||
           ty->kind == TY_LDOUBLE;
}

static bool promotion_candidate_ok(VirtualMachine *vm, Obj *fn, Obj *var) {
    (void)vm;
    if (!var || !var->is_local || var->is_block_var || var->is_captured ||
        var->is_static || var == fn->va_area || var == fn->alloca_bottom)
        return false;
    if (!is_scalar_promotion_type(var->ty) && !is_float_promotion_type(var->ty))
        return false;
    if (belongs_to_outer_function(fn, var))
        return false;
    if (var->name && strncmp(var->name, "__static_link", 14) == 0)
        return false;
    // The cleanup call takes the variable's address.
    if (var->cleanup_fn)
        return false;
    // Unnamed locals are the compiler's own temporaries.
    if (!var->name || !var->name[0])
        return false;
    return true;
}

static PromoCand *promo_find(PromoScan *s, Obj *var) {
    for (int i = 0; i < s->ncands; i++)
        if (s->cands[i].var == var)
            return &s->cands[i];
    return NULL;
}

static void promo_note_bad_tmp(PromoScan *s, Obj *var) {
    if (!var->is_local || (var->name && var->name[0]))
        return;
    for (int i = 0; i < s->nbad; i++)
        if (s->bad_tmps[i] == var)
            return;
    if (s->nbad == s->bad_cap) {
        s->bad_cap  = s->bad_cap ? s->bad_cap * 2 : 8;
        s->bad_tmps = realloc(s->bad_tmps, (size_t)s->bad_cap * sizeof(Obj *));
        if (!s->bad_tmps)
            error("out of memory");
    }
    s->bad_tmps[s->nbad++] = var;
}

static void promo_add_alias(PromoScan *s, Obj *tmp, Obj *target) {
    if (s->naliases == s->aliases_cap) {
        s->aliases_cap = s->aliases_cap ? s->aliases_cap * 2 : 8;
        s->aliases =
            realloc(s->aliases, (size_t)s->aliases_cap * sizeof(PromoAlias));
        if (!s->aliases)
            error("out of memory");
    }
    s->aliases[s->naliases++] = (PromoAlias){tmp, target};
}

// The `&x` of a `tmp = (T *)&x` assignment (the shape to_assign emits for
// `x op= y` / `x++`), or NULL when `assign` is not one.
Node *promo_alias_addr(Node *assign) {
    if (!assign || assign->kind != ND_ASSIGN)
        return NULL;
    Node *l = assign->lhs;
    if (!l || l->kind != ND_VAR || !l->var || !l->var->is_local ||
        (l->var->name && l->var->name[0]))
        return NULL;
    Node *r = assign->rhs;
    while (r && r->kind == ND_CAST)
        r = r->lhs;
    if (!r || r->kind != ND_ADDR || !r->lhs || r->lhs->kind != ND_VAR ||
        !r->lhs->var)
        return NULL;
    return r;
}

// Every variable under `node` is read or written by address.
static void promo_escape_all(PromoScan *s, Node *node) {
    for (; node; node = node->next) {
        if (node->kind == ND_VAR && node->var) {
            PromoCand *c = promo_find(s, node->var);
            if (c)
                c->escapes = true;
        }
        promo_escape_all(s, node->lhs);
        promo_escape_all(s, node->rhs);
        promo_escape_all(s, node->cond);
        promo_escape_all(s, node->then);
        promo_escape_all(s, node->els);
        promo_escape_all(s, node->args);
    }
}

// Walks every node reachable from `node`. The `next` chain is iterated, not
// recursed on, so the walk stays linear in the number of statements.
static void promo_scan_node(PromoScan *s, Node *node, Node *parent,
                            int loop_depth) {
    for (; node; node = node->next) {
        if (node->kind == ND_VAR && node->var) {
            PromoCand *c = promo_find(s, node->var);
            if (c) {
                c->score += 1 + loop_depth * 4;
                if (loop_depth > 0)
                    c->in_loop = true;
            }
            // `*tmp` is the only way an alias temporary may be used; its
            // assignment is the one other (the walk reaches that lhs too).
            bool deref_use =
                parent && parent->kind == ND_DEREF && parent->lhs == node;
            bool assign_lhs =
                parent && parent->lhs == node && promo_alias_addr(parent);
            if (!deref_use && !assign_lhs)
                promo_note_bad_tmp(s, node->var);
        }

        if (node->kind == ND_ASSIGN) {
            Node *addr = promo_alias_addr(node);
            if (addr && promo_find(s, addr->lhs->var)) {
                promo_add_alias(s, node->lhs->var, addr->lhs->var);
                s->alias_addr = addr;
            }
        }
        if (node->kind == ND_ADDR && node != s->alias_addr && node->lhs &&
            node->lhs->kind == ND_VAR && node->lhs->var) {
            PromoCand *c = promo_find(s, node->lhs->var);
            if (c)
                c->escapes = true;
        }
        // `&*tmp` needs tmp's value, which an aliased tmp never holds.
        if (node->kind == ND_ADDR && node->lhs && node->lhs->kind == ND_DEREF &&
            node->lhs->lhs && node->lhs->lhs->kind == ND_VAR &&
            node->lhs->lhs->var)
            promo_note_bad_tmp(s, node->lhs->lhs->var);

        // Only ND_VAR reads and writes a variable through its register; any
        // other node naming one (ND_MEMZERO, ND_VLA_PTR, ...) uses its slot.
        Obj *named[] = {node->kind != ND_VAR ? node->var : NULL,
                        node->ret_buffer, node->atomic_addr,
                        node->block_desc_var};
        for (size_t i = 0; i < sizeof named / sizeof *named; i++) {
            PromoCand *c = named[i] ? promo_find(s, named[i]) : NULL;
            if (c)
                c->escapes = true;
        }

        // Locals reached through these are read by address.
        if (node->kind == ND_ASM) {
            for (Node *a = node->args; a; a = a->next)
                promo_escape_all(s, a);
        }
        if (node->va_ap || node->va_src) {
            promo_escape_all(s, node->va_ap);
            promo_escape_all(s, node->va_src);
        }

        int d = loop_depth + (node->kind == ND_FOR || node->kind == ND_DO);
        promo_scan_node(s, node->lhs, node, d);
        promo_scan_node(s, node->rhs, node, d);
        promo_scan_node(s, node->cond, node, d);
        promo_scan_node(s, node->then, node, d);
        promo_scan_node(s, node->els, node, d);
        promo_scan_node(s, node->init, node, d);
        promo_scan_node(s, node->inc, node, d);
        promo_scan_node(s, node->body, node, d);
        promo_scan_node(s, node->cas_addr, node, d);
        promo_scan_node(s, node->cas_old, node, d);
        promo_scan_node(s, node->cas_new, node, d);
        promo_scan_node(s, node->atomic_expr, node, d);
        promo_scan_node(s, node->va_ap, node, d);
        promo_scan_node(s, node->va_src, node, d);
        promo_scan_node(s, node->rmw_lvalue, node, d);
        for (Node *a = node->args; a; a = a->next)
            promo_scan_node(s, a, node, d);
    }
}

static int promo_cmp(const void *a, const void *b) {
    const PromoCand *ca = a, *cb = b;
    if (ca->escapes != cb->escapes)
        return ca->escapes ? 1 : -1;
    return cb->score - ca->score;
}

// Whether a setjmp-family call is anywhere in the body: its locals must stay
// in memory, since longjmp does not restore registers a function has changed.
static bool body_calls_setjmp(Node *node) {
    for (; node; node = node->next) {
        if (node->kind == ND_FUNCALL && node->lhs &&
            node->lhs->kind == ND_VAR && node->lhs->var &&
            node->lhs->var->name &&
            (strstr(node->lhs->var->name, "setjmp") ||
             strstr(node->lhs->var->name, "savectx")))
            return true;
        if (body_calls_setjmp(node->lhs) || body_calls_setjmp(node->rhs) ||
            body_calls_setjmp(node->cond) || body_calls_setjmp(node->then) ||
            body_calls_setjmp(node->els) || body_calls_setjmp(node->init) ||
            body_calls_setjmp(node->inc) || body_calls_setjmp(node->body) ||
            body_calls_setjmp(node->args) ||
            body_calls_setjmp(node->atomic_expr))
            return true;
    }
    return false;
}

// Promotion stays off whenever a safety check or an explicit debugger would
// observe a local's frame slot.
static bool promotion_enabled(VirtualMachine *vm) {
    if (vm->flags & CCCC_FUSION_UNSAFE_FLAGS)
        return false;
    if ((vm->flags & CCCC_ENABLE_DEBUGGER) && !vm->dbg.crash_debug_auto)
        return false;
    return true;
}

static void promotion_reset(VirtualMachine *vm) {
    vm->compiler.promoted_count    = 0;
    vm->compiler.fp_promoted_count = 0;
}

void end_local_promotion(VirtualMachine *vm) {
    promotion_reset(vm);
}

// Picks the locals and parameters to promote, then reserves their save slots
// below `base_stack_size`. Returns the number of slots reserved.
int prepare_local_promotion(VirtualMachine *vm, Obj *fn, int base_stack_size) {
    promotion_reset(vm);
    if (!promotion_enabled(vm) || !fn->body)
        return 0;
    if (body_calls_setjmp(fn->body))
        return 0;

    int n = 0;
    for (Obj *var = fn->locals; var; var = var->next)
        if (promotion_candidate_ok(vm, fn, var))
            n++;
    if (n == 0)
        return 0;

    PromoScan s = {.cands = calloc((size_t)n, sizeof(PromoCand)), .ncands = n};
    if (!s.cands)
        error("out of memory");
    int idx = 0;
    for (Obj *var = fn->locals; var; var = var->next)
        if (promotion_candidate_ok(vm, fn, var))
            s.cands[idx++].var = var;

    promo_scan_node(&s, fn->body, NULL, 0);

    // An alias temporary used any other way than `*tmp` makes its target a
    // plain memory variable.
    for (int i = 0; i < s.naliases; i++) {
        for (int j = 0; j < s.nbad; j++) {
            if (s.bad_tmps[j] != s.aliases[i].tmp)
                continue;
            PromoCand *c = promo_find(&s, s.aliases[i].target);
            if (c)
                c->escapes = true;
        }
    }

    qsort(s.cands, (size_t)n, sizeof(PromoCand), promo_cmp);

    static const int sregs[]  = {REG_S0, REG_S1, REG_S2, REG_S3};
    static const int fsregs[] = {FREG_S0, FREG_S1, FREG_S2, FREG_S3};
    int              slots    = 0;
    for (int i = 0; i < n; i++) {
        PromoCand *c = &s.cands[i];
        if (c->escapes)
            break;
        if (!c->in_loop)
            continue;
        Compiler *cc = &vm->compiler;
        if (is_float_promotion_type(c->var->ty)) {
            if (cc->fp_promoted_count >= PROMOTE_MAX_FP)
                continue;
            int q                           = cc->fp_promoted_count++;
            cc->fp_promoted_locals[q]       = c->var;
            cc->fp_promoted_regs[q]         = fsregs[q];
            cc->fp_promoted_save_offsets[q] = -(base_stack_size + ++slots);
        } else {
            if (cc->promoted_count >= PROMOTE_MAX_INT)
                continue;
            int p                        = cc->promoted_count++;
            cc->promoted_locals[p]       = c->var;
            cc->promoted_regs[p]         = sregs[p];
            cc->promoted_save_offsets[p] = -(base_stack_size + ++slots);
        }
    }
    for (int i = 0; i < s.naliases; i++)
        if (is_promoted_local(vm, s.aliases[i].target) ||
            is_fp_promoted_local(vm, s.aliases[i].target))
            s.aliases[i].tmp->promoted_alias = s.aliases[i].target;

    free(s.cands);
    free(s.aliases);
    free(s.bad_tmps);
    return slots;
}
