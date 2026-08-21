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
#if !defined(_WIN32)
#include <sys/wait.h>
#endif

// ========== Cleanup Scope Stack ==========
// Per-block stack tracking vars with __attribute__((cleanup(fn))).
// Parallels the parse-time cleanup_scope_depth. Shared file-scope state
// (see ticket #139 note above about thread safety).

// CleanupScopeEntry is declared in codegen_internal.h (shared with
// codegen_stmt.c and codegen_func.c); defined here.
CleanupScopeEntry *g_cleanup_scope = NULL;

// Emit address-of a local variable into dest_reg.
static void emit_local_addr(VirtualMachine *vm, Obj *var, int dest_reg) {
    emit_lea3(vm, dest_reg, var->offset);
}

// Call cv->cleanup_fn(&cv->var). The cleanup fn returns void so REG_A0 is
// clobbered. Uses the standard CALL+patch mechanism (same as ND_FUNCALL for
// CCCC functions).
static void emit_one_cleanup(VirtualMachine *vm, CleanupVar *cv) {
    int r_addr = alloc_temp_reg();
    emit_local_addr(vm, cv->var, r_addr);
    emit_mov3(vm, REG_A0, r_addr);
    free_temp_reg(r_addr);
    emit(vm, CALL);
    Pc patch            = emit_word_ptr(vm);
    vm->text_seg[patch] = 0;
    PATCH_GROW(vm, call_patches, num_call_patches, call_patches_cap);
    vm->compiler.call_patches[vm->compiler.num_call_patches].location = patch;
    vm->compiler.call_patches[vm->compiler.num_call_patches].function =
        cv->cleanup_fn;
    vm->compiler.num_call_patches++;
    reset_temp_regs();
}

// Emit cleanup calls for one scope's vars (iterate in stored order = LIFO).
void emit_scope_cleanups(VirtualMachine *vm, CleanupScopeEntry *scope) {
    for (CleanupVar *cv = scope->vars; cv; cv = cv->next)
        emit_one_cleanup(vm, cv);
}

// Emit cleanup calls for all active scopes with depth > target_depth (innermost
// first).
void emit_cleanups_to_depth(VirtualMachine *vm, int target_depth) {
    for (CleanupScopeEntry *s = g_cleanup_scope; s && s->depth > target_depth;
         s                    = s->outer)
        emit_scope_cleanups(vm, s);
}

// ========== Forward Declarations ==========

// ========== Inline Assembly Passthru ==========

// Compile `asm_str` into a shared library exporting `sym_name` and return the
// resolved function pointer.  The .so file is unlinked immediately after
// dlopen so the library lives only in memory.  Calls error() on failure.
#if !defined(_WIN32)
static void *compile_asm_to_funcptr(const char *sym_name, const char *asm_str) {
    // Create temp source file path (use mkstemp + rename to get .c extension)
    char src_template[] = "/tmp/cccc-asm-XXXXXX";
    int  src_fd         = mkstemp(src_template);
    if (src_fd < 0)
        error("--asm-passthru: failed to create temp source file");
    close(src_fd);
    size_t src_len  = strlen(src_template) + 3;
    char  *src_path = malloc(src_len);
    if (!src_path)
        error("--asm-passthru: malloc failed");
    snprintf(src_path, src_len, "%s.c", src_template);
    if (rename(src_template, src_path) != 0) {
        unlink(src_template);
        free(src_path);
        error("--asm-passthru: failed to rename temp source file");
    }

    // Write C wrapper file with escaped asm string
    FILE *f = fopen(src_path, "w");
    if (!f) {
        unlink(src_path);
        free(src_path);
        error("--asm-passthru: failed to write temp source file");
    }
    fprintf(f, "void %s() { asm(\"", sym_name);
    for (const char *p = asm_str; *p; p++) {
        if (*p == '\\')
            fputs("\\\\", f);
        else if (*p == '"')
            fputs("\\\"", f);
        else if (*p == '\n')
            fputs("\\n", f);
        else
            fputc(*p, f);
    }
    fprintf(f, "\"); }\n");
    fclose(f);

    // Find system C compiler
    char *cc = cccc_find_native_cc();
    if (!cc) {
        unlink(src_path);
        free(src_path);
        error("--asm-passthru: no native C compiler found");
    }

    // Create temp output path for shared library
    char so_template[] = "/tmp/cccc-asm-XXXXXX";
    int  so_fd         = mkstemp(so_template);
    if (so_fd < 0) {
        unlink(src_path);
        free(src_path);
        free(cc);
        error("--asm-passthru: failed to create temp output file");
    }
    close(so_fd);
    size_t so_len  = strlen(so_template) + 7;
    char  *so_path = malloc(so_len);
    if (!so_path) {
        unlink(src_path);
        free(src_path);
        free(cc);
        unlink(so_template);
        error("--asm-passthru: malloc failed");
    }
#if defined(__APPLE__)
    snprintf(so_path, so_len, "%s.dylib", so_template);
#else
    snprintf(so_path, so_len, "%s.so", so_template);
#endif
    if (rename(so_template, so_path) != 0) {
        unlink(src_path);
        unlink(so_template);
        free(src_path);
        free(so_path);
        free(cc);
        error("--asm-passthru: failed to rename temp output file");
    }

    // Compile source to shared library
    pid_t pid = fork();
    if (pid < 0) {
        unlink(src_path);
        unlink(so_path);
        free(src_path);
        free(so_path);
        free(cc);
        error("--asm-passthru: fork failed");
    }
    if (pid == 0) {
#if defined(__APPLE__)
        execlp(cc, cc, "-shared", "-o", so_path, src_path, (char *)NULL);
#else
        execlp(cc, cc, "-shared", "-fPIC", "-o", so_path, src_path,
               (char *)NULL);
#endif
        _exit(127);
    }
    int status;
    waitpid(pid, &status, 0);
    unlink(src_path);
    free(src_path);
    free(cc);

    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        unlink(so_path);
        free(so_path);
        error("--asm-passthru: native compilation failed");
    }

    // dlopen the shared library
    void *handle = dlopen(so_path, RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
        unlink(so_path);
        free(so_path);
        error("--asm-passthru: dlopen failed: %s", dlerror());
    }

    // dlsym the function
    void *func_ptr = dlsym(handle, sym_name);
    if (!func_ptr) {
        dlclose(handle);
        unlink(so_path);
        free(so_path);
        error("--asm-passthru: dlsym failed: %s", dlerror());
    }

    // Unlink the .so now that it's loaded in memory
    unlink(so_path);
    free(so_path);
    return func_ptr;
}
#endif // !_WIN32

void cccc_default_asm_passthru(VirtualMachine *vm, const char *asm_str) {
#if !defined(_WIN32)
    static int asm_counter = 0;
    char       sym_name[128];
    int        n = asm_counter++;
    snprintf(sym_name, sizeof(sym_name), "__cccc_asm_passthru_%d", n);

    void *func_ptr = compile_asm_to_funcptr(sym_name, asm_str);

    // Register as FFI function (0 args, no double return)
    cc_register_cfunc(vm, sym_name, func_ptr, 0, 0);

    // Find FFI index and annotate for .c4 rehydration
    int ffi_idx = find_ffi_function(vm, sym_name);
    if (ffi_idx < 0)
        error("--asm-passthru: FFI registration failed");
    vm->compiler.ffi_table[ffi_idx].is_asm_passthru = 1;
    vm->compiler.ffi_table[ffi_idx].asm_src         = strdup(asm_str);

    // Emit CALLF with 0 args
    emit(vm, CALLF);
    emit_word(vm, ffi_idx);
    emit_word(vm, 0);
    emit_i64(vm, 0);
    emit_i64(vm, 0);
#else
    (void)asm_str;
    error("--asm-passthru is not supported on Windows");
#endif
}

// Recompile any asm-passthru FFI entries whose func_ptr was lost during .c4
// serialization.  Called from the .c4 load path after stdlib/library
// resolution. Respects the FFI allow/deny policy; denied entries are left with
// func_ptr=NULL (CALLF will emit the "not resolved" error at execution time).
// Returns 0 on success, -1 on hard failure.
int cc_rehydrate_asm_passthru(VirtualMachine *vm) {
#if !defined(_WIN32)
    for (int i = 0; i < vm->compiler.ffi_count; i++) {
        ForeignFunc *ff = &vm->compiler.ffi_table[i];
        if (!ff->is_asm_passthru || ff->func_ptr)
            continue;

        // Respect disable_all_ffi
        if (vm->disable_all_ffi)
            continue;

        // Respect allow list: if set, symbol must appear in it
        if (vm->ffi_allow_count > 0 &&
            !cccc_ffi_name_in_list(vm->ffi_allow_list, vm->ffi_allow_count,
                                   ff->name))
            continue;

        // Respect deny list: if no allow list and symbol is denied, skip
        if (vm->ffi_allow_count == 0 &&
            cccc_ffi_name_in_list(vm->ffi_deny_list, vm->ffi_deny_count,
                                  ff->name))
            continue;

        if (!ff->asm_src) {
            fprintf(stderr,
                    "error: asm-passthru FFI entry '%s' has no source to "
                    "rehydrate\n",
                    ff->name ? ff->name : "(null)");
            return -1;
        }

        ff->func_ptr = compile_asm_to_funcptr(ff->name, ff->asm_src);
    }
    return 0;
#else
    (void)vm;
    return 0;
#endif
}

// ========== Nested Function Helpers ==========

// Find the __static_link local variable in a nested function's locals
Obj *find_static_link_var(Obj *fn) {
    if (!fn || !fn->is_nested)
        return NULL;
    for (Obj *var = fn->locals; var; var = var->next) {
        if (strncmp(var->name, "__static_link", sizeof("__static_link")) == 0)
            return var;
    }
    return NULL;
}

// Byte delta from a frame's bp to its own __static_link slot. It is always
// the first param (new_lvar prepends -> head of the locals list -> receives
// A0, src/parse_decl.c), so its slot is structurally -1, shifted one lower
// (-2) when ENT3 reserves bp[-1] for the stack canary (#445,
// assign_lvar_offsets's canary_bias, src/codegen_stmt.c). Identical to what
// that frame's own static_link->offset carries, without depending on any
// particular ancestor's codegen order -- #1082: every ancestor beyond the
// first used to hardcode -8 here, which silently read the wrong slot (and
// corrupted the chase, SIGSEGV) whenever --stack-canaries/-3 shifted every
// frame one slot lower.
int static_link_hop_bytes(VirtualMachine *vm) {
    return -(1 + ((vm->flags & CCCC_STACK_CANARIES) ? 1 : 0)) * 8;
}

// Return the index of var in block_fn->captures (0-based), or -1 if not
// captured. Descriptor slot = (index + 1) * 8 so that slot 0 (offset 0) stays
// the invoke ptr.
int find_capture_index(Obj *block_fn, Obj *var) {
    for (int i = 0; i < block_fn->num_captures; i++)
        if (block_fn->captures[i] == var)
            return i;
    return -1;
}

// Ensure parent->local_set is populated with all its locals and params.
// Keyed by (long long)(intptr_t)var so membership is O(1). (#165)
static void ensure_local_set(Obj *parent) {
    if (parent->local_set_built)
        return;
    for (Obj *local = parent->locals; local; local = local->next)
        hashmap_put_int(&parent->local_set, (long long)(intptr_t)local, local);
    for (Obj *param = parent->params; param; param = param->next)
        hashmap_put_int(&parent->local_set, (long long)(intptr_t)param, param);
    parent->local_set_built = true;
}

// Check if a variable belongs to an outer (enclosing) function.
// Returns the owning function, or NULL if it belongs to the current function.
// O(depth) using per-function lazy hash sets instead of O(depth * locals).
// (#165)
Obj *belongs_to_outer_function(Obj *current_fn, Obj *var) {
    if (!current_fn || !current_fn->is_nested || !var || !var->is_local)
        return NULL;

    for (Obj *parent = current_fn->parent_fn; parent;
         parent      = parent->parent_fn) {
        ensure_local_set(parent);
        if (hashmap_get_int(&parent->local_set, (long long)(intptr_t)var))
            return parent;
    }
    return NULL;
}

// Calculate how many static chain links to follow to reach the owning function
static int calculate_chain_depth(Obj *current_fn, Obj *owner_fn) {
    int depth = 0;
    for (Obj *fn = current_fn; fn && fn != owner_fn; fn = fn->parent_fn) {
        depth++;
    }
    return depth;
}

// Emit dest_reg = address of `var`, owned by `owner_fn`, an ancestor of
// `current_fn` reached by walking the static-link chain. `current_fn` must be
// genuinely nested (have a __static_link local). #1076: factored out of
// gen_addr's own ND_VAR case (below) so the block-literal capture-population
// loop (codegen_expr.c's ND_BLOCK_LITERAL case) can reuse the exact same
// chase for a capture owned by the enclosing *nested function's* own
// ancestor, rather than a second hand-written copy drifting from this one --
// the same failure mode #994 already warned about for the capture-offset
// layout.
void emit_static_chain_var_addr(VirtualMachine *vm, Obj *current_fn,
                                Obj *owner_fn, Obj *var, int dest_reg) {
    Obj *static_link = find_static_link_var(current_fn);
    if (!static_link)
        error("nested function missing __static_link");
    // Compiler-internal chase (#676): every intermediate address here feeds
    // an immediate load, never escapes as a user-visible pointer.
    emit_lea3_internal(vm, dest_reg, static_link->offset); // &__static_link
    emit_rr(vm, LDR_D, dest_reg, dest_reg); // load static_link (parent's bp)

    int depth = calculate_chain_depth(current_fn, owner_fn);
    // First link already loaded above, so start from 1.
    int hop = static_link_hop_bytes(vm);
    // #1081: `anc` is the ancestor whose bp dest_reg currently holds --
    // current_fn's immediate parent, right after the load above. Every hop
    // below reads THAT ancestor's own __static_link slot: for a genuine
    // nested function that slot holds its own parent's bp (the assumption
    // every hop here used to make unconditionally), but for a BLOCK it
    // holds that block's own descriptor pointer instead (ND_BLOCK_CALL,
    // codegen_expr.c, always passes the descriptor as its callee's A0/
    // __static_link). Reading it and continuing to add var->offset*8, as
    // if it were a frame bp, produced this ticket's own garbage address.
    // Since #1081's parse-side fix (block_literal()'s transitive-capture
    // climb now also walks every nested_children function defined directly
    // inside a block, parse_blocks.c) guarantees `var` is captured by the
    // first block ancestor on this chain (if any), the chase terminates
    // there instead: read the descriptor (the same load that used to be
    // misread as a bp) and index into it exactly like gen_addr's own
    // cap_idx >= 0 arm above does for a plain in-block reference. The
    // single-hop case (owner_fn == current_fn's immediate parent, depth ==
    // 1) needs no such check -- the loop below never runs for it -- and
    // already worked before this fix (confirmed: a block's own param, read
    // directly by a nested function defined inside it, is an ordinary
    // frame-relative access into the block's very real stack frame).
    Obj *anc = current_fn->parent_fn;
    for (int i = 1; i < depth; i++) {
        if (anc->is_block) {
            int cap_idx = find_capture_index(anc, var);
            if (cap_idx < 0)
                error("internal error: static-link chase reached a block "
                      "ancestor that never captured '%s' (#1081)",
                      var->name);
            long cap_offset = cc_block_capture_offset(anc, cap_idx);
            emit_addi3(vm, dest_reg, dest_reg, hop);
            emit_rr(vm, LDR_D, dest_reg,
                    dest_reg); // load anc's own descriptor ptr
            emit_addi3(vm, dest_reg, dest_reg, cap_offset);
            if (var->is_block_var)
                emit_rr(vm, LDR_D, dest_reg,
                        dest_reg); // heap ptr from slot
            return;
        }
        // Each parent also has its own __static_link, at hop bytes below its
        // own bp (#1082: bp-1 without canaries, bp-2 with them -- a bare -8
        // here silently read the wrong slot under --stack-canaries/-3 once
        // depth reached 2).
        emit_addi3(vm, dest_reg, dest_reg, hop);
        emit_rr(vm, LDR_D, dest_reg, dest_reg); // load grandparent's bp
        anc = anc->parent_fn;
    }

    // dest_reg now holds owner_fn's bp; add the variable's offset. Variable
    // offsets are in slots, so multiply by 8 bytes.
    emit_addi3(vm, dest_reg, dest_reg, var->offset * 8);
}

// Returns true when a ND_VAR node can be loaded/stored with a fused
// LDR_LOCAL/STR_LOCAL opcode instead of a LEA3+LDR/STR pair.
// Requires: node->kind == ND_VAR.
bool is_simple_local_scalar(VirtualMachine *vm, Node *node) {
    if (!node->var->is_local)
        return false;
    if (node->var->is_block_var)
        return false;
    // A variable captured by the enclosing block lives in the block descriptor
    // (reached via __static_link), not at its own frame offset.  It must go
    // through gen_addr's capture path, never the fused direct-frame load.
    if (vm->compiler.current_fn && vm->compiler.current_fn->is_block &&
        find_capture_index(vm->compiler.current_fn, node->var) >= 0)
        return false;
    if (belongs_to_outer_function(vm->compiler.current_fn, node->var))
        return false;
    // Volatile locals must go through the generic LEA3+LDR/STR path so that
    // watchpoint checks fire on every access (C11 §6.7.3p7).
    if (node->ty->is_volatile)
        return false;
    if (node->var->is_param &&
        (node->ty->kind == TY_STRUCT || node->ty->kind == TY_UNION))
        return false;
    return node->ty->kind != TY_ARRAY && node->ty->kind != TY_STRUCT &&
           node->ty->kind != TY_UNION && node->ty->kind != TY_COMPLEX &&
           !is_wide_bitint(node->ty) &&
           !is_decimal(node->ty); // #402: address-based, same as wide _BitInt
}

// True iff gen_addr(node) is guaranteed to produce a bp-relative address of
// the CURRENT function's own live frame -- i.e. built entirely through
// emit_lea3_var's plain local-offset branch, with no intervening pointer
// *value* load (captured-var descriptor, static-link chain, by-pointer
// aggregate param, __block heap wrapper, or an actual ND_DEREF). Mirrors the
// base-case conditions in gen_addr's ND_VAR handling (see above) and
// is_simple_local_scalar's predicate cluster, generalized to aggregates and
// to member-access chains of arbitrary depth (#740).
//
// Deliberately NOT gated on var->addr_escapes: accessing your own local from
// within your own still-running frame is safe whether or not its address
// escapes elsewhere -- escaping only matters when a *different* frame
// dereferences a pointer *value* it was handed, which is exactly the
// ND_DEREF case below (always declined, stays CHKP3-checked).
//
// #740: a non-escaping local struct/union's member access (t.a) always falls
// to gen_addr()+emit_load/emit_store like a scalar dereference, since
// is_simple_local_scalar above excludes aggregates from the fused frame-load
// path. Without this classifier, CHKP3 unconditionally runs on that freshly
// computed bp+offset address -- and can find a stale stack_ptr_epochs/
// stack_intervals tag left by an unrelated dead sibling frame's own escaping
// local that happened to reuse the same physical stack slot, false-positivin
// a dangling-pointer report on a plainly-live access to the current frame's
// own memory. <stdarg.h>'s va_arg/va_start hit this same pattern (ap.reg_ptr,
// ap.stack_ptr are ND_MEMBER on a local va_list), which is why the original
// #740 ticket's va_arg-specific diagnosis was actually one instance of this
// more general bug, not a vector/variadic-specific mechanism.
//
// Scoped to struct/union member chains only, not array/vector indexing
// (arr[i] on a local array reaches the same unchecked emit_load/store surface
// under -3, since match_indexed_addr's fused fast path is disabled whenever
// CCCC_POINTER_CHECKS is set -- but distinguishing an array/vector-decay base
// from a pointer-variable base in that ND_ADD indexing chain needs its own
// classifier and has no known reproducer yet; tracked separately).
// Is `node` a member access directly on a union expression (`u.m`, not a
// struct nested inside a union or vice versa -- only the immediate parent
// matters, since that's the object whose bytes might legally carry more
// than one C11 effective type)? Used to gate CHKT3 emission around union
// member loads/stores so legal punning doesn't false-positive (#653).
bool is_union_member_access(Node *node) {
    return node && node->kind == ND_MEMBER && node->lhs && node->lhs->ty &&
           node->lhs->ty->kind == TY_UNION;
}

bool addr_is_local_frame(VirtualMachine *vm, Node *node) {
    switch (node->kind) {
        case ND_VAR: {
            Obj *var = node->var;
            if (var->is_function || !var->is_local || var->is_block_var)
                return false;
            Obj *current_fn = vm->compiler.current_fn;
            if (current_fn && current_fn->is_block &&
                find_capture_index(current_fn, var) >= 0)
                return false;
            if (belongs_to_outer_function(current_fn, var))
                return false;
            if (var->is_param &&
                (var->ty->kind == TY_STRUCT || var->ty->kind == TY_UNION ||
                 var->ty->kind == TY_VECTOR || is_wide_bitint(var->ty) ||
                 is_decimal(var->ty))) // #402: passed by pointer too
                return false;
            return true;
        }
        case ND_MEMBER:
            return addr_is_local_frame(vm, node->lhs);
        case ND_COMMA:
            return addr_is_local_frame(vm, node->rhs);
        default:
            return false; // ND_DEREF (pointer value) and everything else: keep
                          // checked
    }
}

// ========== Safety Instrumentation Helpers ==========

// Register stack variable metadata for runtime instrumentation.
// Keyed by (scope_id, offset) via stack_var_meta_key(), not offset alone --
// two different functions whose locals land at the same bp-relative offset
// (the common case) must not collide in the table (#671).
void add_stack_var_meta(VirtualMachine *vm, const char *name, long long offset,
                        Type *ty, int scope_id) {
    if (!(vm->flags & CCCC_STACK_INSTR))
        return;
    StackVarMeta *meta = calloc(1, sizeof(StackVarMeta));
    if (!meta)
        error("failed to allocate stack variable metadata");
    meta->name     = (char *)name;
    meta->offset   = offset;
    meta->ty       = ty;
    meta->scope_id = scope_id;
    meta->is_alive = 0;
    hashmap_put_int(&vm->stack_var_meta, stack_var_meta_key(scope_id, offset),
                    meta);
}

// Emit SCOPEIN for scope_id.
void emit_scopein(VirtualMachine *vm, int scope_id) {
    emit(vm, SCOPEIN);
    emit_word(vm, scope_id);
}

// Emit SCOPEOUT for scope_id.
void emit_scopeout(VirtualMachine *vm, int scope_id) {
    emit(vm, SCOPEOUT);
    emit_word(vm, scope_id);
}

// Emit HMRK for a VLA-declaring block's heap-reclamation watermark (#981).
void emit_hmrk(VirtualMachine *vm, int depth) {
    emit(vm, HMRK);
    emit_word(vm, depth);
}

// Emit HREL, the matching block-exit release for emit_hmrk above.
void emit_hrel(VirtualMachine *vm, int depth) {
    emit(vm, HREL);
    emit_word(vm, depth);
}

// Emit CHKI (check initialized) for local at bp+offset.
void emit_chki(VirtualMachine *vm, long long offset) {
    emit(vm, CHKI);
    emit_i64(vm, offset);
}

// Emit MARKI (mark initialized) for local at bp+offset.
void emit_marki(VirtualMachine *vm, long long offset) {
    emit(vm, MARKI);
    emit_i64(vm, offset);
}

// Emit CHKL (check liveness) for local at bp+offset, declared in
// vm->current_function_scope_id. The runtime liveness check itself is keyed
// by actual address (bp+offset); the scope_id operand is only used to look
// up the declaration record (name/type) for the error message when the
// check fails (#671) -- see stack_var_meta_key().
void emit_chkl(VirtualMachine *vm, long long offset) {
    emit(vm, CHKL);
    emit_i64(vm, offset);
    emit_word(vm, vm->current_function_scope_id);
}

// Emit MARKR (mark read) for local at bp+offset. No scope_id operand needed:
// the runtime looks this up by address (bp+offset) in vm->stack_var_active,
// which directly yields the correct StackVarMeta* for whichever activation
// is currently live at that address.
void emit_markr(VirtualMachine *vm, long long offset) {
    emit(vm, MARKR);
    emit_i64(vm, offset);
}

// Emit MARKW (mark write) for local at bp+offset. See emit_markr() re: no
// scope_id operand needed.
void emit_markw(VirtualMachine *vm, long long offset) {
    emit(vm, MARKW);
    emit_i64(vm, offset);
}

// Emit MARKP (mark provenance).
// rs_ptr and rs_base hold the pointer and its allocation base.
void emit_markp(VirtualMachine *vm, int rs_ptr, int rs_base, int origin_type,
                size_t size) {
    emit(vm, MARKP);
    emit_word(vm, ENCODE_RR(rs_ptr, rs_base));
    emit_word(vm, origin_type);
    emit_i64(vm, (long long)size);
}

// Emit CHKR (checked-pointer range check, #770/#482-484), or CHKRO (#942)
// when `optional` -- identical wire format, CHKRO just treats the
// [lo=-1, hi=0) sentinel as a no-op instead of a violation. `optional` comes
// straight from the deref node's checked_bounds_optional flag (true only for
// a checked-bounds-propagation candidate that's checked-rooted on some but
// not all paths, per src/parse.c's propagate_checked_bounds()) -- a direct
// declared-checked access or a fully-rooted (#919/#941) propagation
// candidate always passes false here, so its codegen is unchanged from
// before #942.
// rs_addr/rs_lo/rs_hi are caller-computed registers: the accessed address and
// the checked pointer's declared bounds (from its count()/byte_count()/
// bounds() attribute, desugared to [lo, hi) by the caller). access_size is
// the size in bytes of the access being checked (sizeof of the pointee for a
// scalar deref). No-op at runtime unless CCCC_CHECKED_BOUNDS is set -- callers
// gate emission on that flag so the default build never emits this opcode.
static void emit_chkr(VirtualMachine *vm, int rs_addr, int rs_lo, int rs_hi,
                      long long access_size, bool optional) {
    emit_rrrs_i(vm, optional ? CHKRO : CHKR, rs_addr, rs_lo, rs_hi, 0,
                access_size);
}

// Emit CHKNT (checked-pointer null-terminator guard, #923).
// rs_addr/rs_hi/rs_val are caller-computed registers: the stored-to address,
// the checked pointer's own already-widened upper bound (the same rs_hi CHKR
// just range-checked addr against), and the value being stored. elem_size is
// the pointee's size in bytes. Traps iff addr == hi - elem_size && val != 0
// -- a non-null write into the terminator slot CHKR's +1 widening opened up.
// No-op at runtime unless CCCC_CHECKED_BOUNDS is set, same as CHKR; callers
// gate emission on that flag (and on node->checked_nt_terminator) so the
// default build never emits this opcode.
void emit_chknt(VirtualMachine *vm, int rs_addr, int rs_hi, int rs_val,
                long long elem_size) {
    emit_rrrs_i(vm, CHKNT, rs_addr, rs_hi, rs_val, 0, elem_size);
}

// Emit CHKNTZ (checked-pointer null-terminator guard for memcpy-lowered
// pointees, #939). Same shape as emit_chknt() above, but rs_src is the
// SOURCE ADDRESS being memcpy'd from (not a value register) -- struct/union
// and wide _BitInt/_Decimal ntarray stores never hold their value in a
// single register, so CHKNT itself cannot check them. Emitted before the
// MCPY it guards. No-op at runtime unless CCCC_CHECKED_BOUNDS is set, same
// as CHKNT; callers gate emission on that flag (and on
// node->checked_nt_terminator) so the default build never emits this
// opcode.
void emit_chkntz(VirtualMachine *vm, int rs_addr, int rs_hi, int rs_src,
                 long long elem_size) {
    emit_rrrs_i(vm, CHKNTZ, rs_addr, rs_hi, rs_src, 0, elem_size);
}

// #945/#939: every CHKNT/CHKNTZ emission site needs `deref`'s own
// already-widened upper bound in a fresh register, re-running the
// object-expression hoist init first (idempotent even if some other site
// already ran it for this same access -- see the #945 comments at each call
// site). Shared here so the three sites (ND_ASSIGN's scalar CHKNT guard,
// ND_ASSIGN's memcpy-path CHKNTZ guard, and ND_CAS's CHKNT guard) don't each
// carry their own copy. Caller owns freeing the returned register.
int gen_checked_nt_hi(VirtualMachine *vm, Node *deref) {
    int r_hi = alloc_temp_reg();
    if (deref->checked_bounds_obj_init)
        gen_expr(vm, deref->checked_bounds_obj_init, r_hi);
    gen_expr(vm, deref->checked_bounds_hi, r_hi);
    return r_hi;
}

// Emit CHKAB (checked-pointer assignment-time bounds implication, #944).
// rs_val is the target's own declared bound being checked (lo when
// `is_hi` is false, hi when true); rs_slo/rs_shi are the source's snapshot
// bounds, already evaluated into registers by the caller. Traps unless
// rs_slo <= rs_val <= rs_shi. No-op at runtime unless CCCC_CHECKED_BOUNDS is
// set, same as CHKR/CHKNT; callers gate emission on that flag.
void emit_chkab(VirtualMachine *vm, int rs_val, int rs_slo, int rs_shi,
                bool is_hi) {
    emit_rrrs_i(vm, CHKAB, rs_val, rs_slo, rs_shi, 0, is_hi ? 1 : 0);
}

// ========== Address Generation ==========

// Generate address of an lvalue into dest_reg
void gen_addr(VirtualMachine *vm, Node *node, int dest_reg) {
    switch (node->kind) {
        case ND_VAR:
            if (node->var->is_function) {
                // Function address - emit placeholder and record patch
                Pc addr_loc = emit_lta3(vm, dest_reg, 0); // Placeholder

                PATCH_GROW(vm, func_addr_patches, num_func_addr_patches,
                           func_addr_patches_cap);
                vm->compiler
                    .func_addr_patches[vm->compiler.num_func_addr_patches]
                    .location = addr_loc;
                vm->compiler
                    .func_addr_patches[vm->compiler.num_func_addr_patches]
                    .function = node->var;
                vm->compiler.num_func_addr_patches++;
            } else if (node->var->is_local) {
                // Check if this is a captured variable accessed from within a
                // block
                Obj *current_fn = vm->compiler.current_fn;

                int  cap_idx = (current_fn && current_fn->is_block)
                                   ? find_capture_index(current_fn, node->var)
                                   : -1;
                if (cap_idx >= 0) {
                    // Access captured variable from block descriptor via
                    // __static_link. Offset is computed per-block to avoid
                    // collisions when the same variable is captured at
                    // different positions in nested descriptors.
                    Obj *static_link = find_static_link_var(current_fn);
                    if (!static_link)
                        error("block function missing __static_link");
                    // #994: slot width/offset varies with the capture's type
                    // (a by-value aggregate wider than 8 bytes gets a wider
                    // slot) -- shared with parse.c so the layout can't drift.
                    long cap_offset =
                        cc_block_capture_offset(current_fn, cap_idx);
                    // Compiler-internal: this LEA3 only materializes
                    // __static_link's own slot address to immediately load the
                    // descriptor pointer out of it (#676) -- the slot address
                    // itself never survives past the next instruction.
                    emit_lea3_internal(vm, dest_reg,
                                       static_link->offset); // &__static_link
                    emit_rr(vm, LDR_D, dest_reg,
                            dest_reg); // Load descriptor ptr
                    emit_addi3(vm, dest_reg, dest_reg, cap_offset);
                    if (node->var->is_block_var)
                        emit_rr(vm, LDR_D, dest_reg,
                                dest_reg); // heap ptr from slot
                } else {
                    // Check if this variable belongs to an outer function
                    // (nested function access)
                    Obj *owner_fn =
                        belongs_to_outer_function(current_fn, node->var);

                    if (owner_fn) {
                        // Accessing outer function's variable via static chain.
                        // #1076: shared with the block-literal capture loop
                        // (codegen_expr.c) via emit_static_chain_var_addr.
                        emit_static_chain_var_addr(vm, current_fn, owner_fn,
                                                   node->var, dest_reg);
                    } else {
                        // Normal local variable access
                        // For struct/union (and vector, #714) parameters, the
                        // slot contains a pointer to the value. We need to load
                        // that pointer, not the slot address.
                        if (node->var->is_param &&
                            (node->ty->kind == TY_STRUCT ||
                             node->ty->kind == TY_UNION ||
                             node->ty->kind == TY_VECTOR ||
                             is_wide_bitint(node->ty) ||
                             is_decimal(node->ty))) {
                            // Compiler-internal: slot address only feeds the
                            // immediate load below (#676), not the struct's own
                            // data address.
                            emit_lea3_internal(
                                vm, dest_reg,
                                node->var->offset); // Slot address
                            emit_rr(vm, LDR_D, dest_reg,
                                    dest_reg);      // Load pointer from slot
                        } else if (node->var->is_block_var) {
                            // __block variable: slot contains pointer to
                            // heap-allocated wrapper. Compiler-internal: slot
                            // address only feeds the immediate load (#676).
                            emit_lea3_internal(
                                vm, dest_reg,
                                node->var->offset); // Slot address
                            emit_rr(vm, LDR_D, dest_reg,
                                    dest_reg); // Load heap pointer from slot
                            // dest_reg now points to actual storage on heap
                        } else {
                            // The address returned here IS this var's own base
                            // address, handed to whatever wanted it (&var,
                            // array/struct decay, member/subscript base, ...)
                            // -- skip recording iff #676's escape analysis
                            // proved it never leaves this frame.
                            emit_lea3_var(vm, dest_reg, node->var);
                        }
                    }
                }
            } else {
                // Global variable (TLS or shared). #957: mark it referenced
                // here (codegen, not parse-time) so `extern int g; sizeof(g);`
                // still compiles without a definition -- sizeof never reaches
                // gen_addr.
                node->var->is_referenced = true;
                if (node->var->is_tls)
                    emit_ldtls3(vm, dest_reg, node->var->offset);
                else
                    emit_lda3(vm, dest_reg, node->var->offset);
            }
            return;

        case ND_DEREF:
            // Address of *ptr is just ptr
            gen_expr(vm, node->lhs, dest_reg);
            // Checked-pointer bounds check (#770/#484). Runs on both the load
            // and the store path -- both reach the accessed address through
            // this single site (x = p[i] via gen_expr's ND_DEREF load case
            // calling gen_addr, p[i] = x via the store path doing the same) --
            // and even when dest_reg == REG_ZERO (a discarded-value deref must
            // still trap, matching emit_load_safety_checks's convention above).
            // Gated on CCCC_CHECKED_BOUNDS so the default build emits nothing;
            // node->checked_bounds_lo/hi are only non-NULL when
            // set_checked_deref_bounds() (parse.c) found a checked pointer with
            // resolvable bounds at this access site, so no per-access flag
            // check is needed beyond the one on CCCC_CHECKED_BOUNDS itself.
            if ((vm->flags & CCCC_CHECKED_BOUNDS) && node->checked_bounds_lo &&
                node->checked_bounds_hi) {
                mark_temp_reg_used(
                    dest_reg); // protect the just-computed address
                int r_lo = alloc_temp_reg();
                int r_hi = alloc_temp_reg();
                // #945: re-run the member-access object-expression hoist init
                // (if any) before reading lo/hi -- they may read it back
                // through
                // `*t`. Result discarded (r_lo is free scratch); only the store
                // to `t` matters.
                if (node->checked_bounds_obj_init)
                    gen_expr(vm, node->checked_bounds_obj_init, r_lo);
                gen_expr(vm, node->checked_bounds_lo, r_lo);
                gen_expr(vm, node->checked_bounds_hi, r_hi);
                emit_chkr(vm, dest_reg, r_lo, r_hi, node->checked_access_size,
                          node->checked_bounds_optional);
                free_temp_reg(r_hi);
                free_temp_reg(r_lo);
            }
            return;

        case ND_MEMBER:
            // Address of struct.member = &struct + member_offset
            gen_addr(vm, node->lhs, dest_reg);
            if (node->member->offset != 0) {
                emit_addi3(vm, dest_reg, dest_reg, node->member->offset);
            }
            return;

        case ND_COMMA:
            gen_expr(vm, node->lhs, REG_ZERO); // Discard result
            gen_addr(vm, node->rhs, dest_reg);
            return;

        case ND_VLA_PTR:
            // VLA: get the address of the pointer variable itself (for storing
            // into it) NOT the pointer value - that's for gen_expr when
            // accessing the array
            if (node->var->is_local) {
                emit_lea3(vm, dest_reg,
                          node->var->offset); // Address of the pointer variable
            } else {
                error_tok(vm, node->tok, "VLA must be local");
            }
            return;

        default:
            error_tok(vm, node->tok, "not an lvalue");
    }
}

static int complex_part_offset(Type *ty) {
    return ty && ty->kind == TY_COMPLEX && ty->base ? ty->base->size : 8;
}

static int complex_load_op(Type *ty) {
    return ty && ty->kind == TY_COMPLEX && ty->base &&
                   ty->base->kind == TY_FLOAT
               ? FLDR_F32
               : FLDR;
}

static int complex_store_op(Type *ty) {
    return ty && ty->kind == TY_COMPLEX && ty->base &&
                   ty->base->kind == TY_FLOAT
               ? FSTR_F32
               : FSTR;
}

static void emit_float_zero(VirtualMachine *vm, int freg) {
    int r_zero = alloc_temp_reg();
    emit_li3(vm, r_zero, 0);
    emit_rr(vm, I2F3, freg, r_zero);
    free_temp_reg(r_zero);
}

static void emit_complex_load(VirtualMachine *vm, Type *ty, int real_reg,
                              int imag_reg, int addr_reg) {
    int op = complex_load_op(ty);
    emit_rr(vm, op, real_reg, addr_reg);
    int r_imag_addr = alloc_temp_reg();
    emit_addi3(vm, r_imag_addr, addr_reg, complex_part_offset(ty));
    emit_rr(vm, op, imag_reg, r_imag_addr);
    free_temp_reg(r_imag_addr);
}

static void emit_complex_store(VirtualMachine *vm, Type *ty, int real_reg,
                               int imag_reg, int addr_reg) {
    int op = complex_store_op(ty);
    emit_rr(vm, op, real_reg, addr_reg);
    int r_imag_addr = alloc_temp_reg();
    emit_addi3(vm, r_imag_addr, addr_reg, complex_part_offset(ty));
    emit_rr(vm, op, imag_reg, r_imag_addr);
    free_temp_reg(r_imag_addr);
}

void gen_complex_expr(VirtualMachine *vm, Node *node, int real_reg,
                      int imag_reg) {
    if (!node)
        error("codegen: null complex expression node");

    if (!is_complex(node->ty)) {
        gen_expr(vm, node, real_reg);
        emit_float_zero(vm, imag_reg);
        return;
    }

    switch (node->kind) {
        case ND_COMPLEX:
            if (node->val == 0) {
                gen_expr(vm, node->lhs, real_reg);
                gen_expr(vm, node->rhs, imag_reg);
                return;
            }
            if (node->val == 3) {
                gen_complex_expr(vm, node->lhs, real_reg, imag_reg);
                emit_frr(vm, fop_for_type(node->ty->base, FNEG3), imag_reg,
                         imag_reg);
                return;
            }
            break;
        case ND_CAST:
            if (is_complex(node->lhs->ty)) {
                gen_complex_expr(vm, node->lhs, real_reg, imag_reg);
            } else {
                gen_expr(vm, node->lhs, real_reg);
                if (!is_flonum(node->lhs->ty))
                    emit_rr(
                        vm,
                        fop_for_type(node->ty->base,
                                     is_u64_int(node->lhs->ty) ? U2F3 : I2F3),
                        real_reg, real_reg);
                emit_float_zero(vm, imag_reg);
            }
            if (node->ty->base && node->ty->base->kind == TY_FLOAT) {
                emit_fround_f32(vm, real_reg, real_reg);
                emit_fround_f32(vm, imag_reg, imag_reg);
            }
            return;
        case ND_VAR:
        case ND_DEREF:
        case ND_MEMBER: {
            int r_addr = alloc_temp_reg();
            gen_addr(vm, node, r_addr);
            emit_complex_load(vm, node->ty, real_reg, imag_reg, r_addr);
            free_temp_reg(r_addr);
            return;
        }
        case ND_ASSIGN: {
            gen_complex_expr(vm, node->rhs, real_reg, imag_reg);
            int r_addr = alloc_temp_reg();
            gen_addr(vm, node->lhs, r_addr);
            emit_complex_store(vm, node->ty, real_reg, imag_reg, r_addr);
            free_temp_reg(r_addr);
            return;
        }
        case ND_COMMA:
            gen_expr(vm, node->lhs, REG_ZERO);
            gen_complex_expr(vm, node->rhs, real_reg, imag_reg);
            return;
        case ND_NEG:
            gen_complex_expr(vm, node->lhs, real_reg, imag_reg);
            emit_frr(vm, fop_for_type(node->ty->base, FNEG3), real_reg,
                     real_reg);
            emit_frr(vm, fop_for_type(node->ty->base, FNEG3), imag_reg,
                     imag_reg);
            return;
        case ND_ADD:
        case ND_SUB:
        case ND_MUL:
        case ND_DIV: {
            // #968: br/bi and t0-t2 used to be *fixed* registers (T5-T9), but
            // this function recurses -- a right-hand-nested complex binop
            // (`a + (b + c)`, or the canonical `20.0 + 22.0 * I`) generated its
            // RHS into T5/T6 and then the nested call immediately reused T5/T6
            // for its own operands, destroying the outer RHS. Left nesting
            // (`(a + b) + c`) survived only by accident, since the outer
            // real_reg/imag_reg happened to differ from T5/T6.
            //
            // Simply drawing br/bi/t0-t2 from alloc_temp_reg() is not enough on
            // its own: T0-T10 are *all* caller-saved, so a function call
            // anywhere in the RHS subtree clobbers real_reg/imag_reg regardless
            // of which temp they land in, and holding br/bi live across the RHS
            // recursion (the natural allocator-only fix) accumulates two
            // registers per nesting level, exhausting the pool around depth 5
            // on a right-nested chain. So instead this unconditionally spills
            // real_reg/imag_reg's bits to the stack before recursing into the
            // RHS and reloads them after (same FR2R/R2FR-through-PSH3/POP3
            // idiom the scalar float binop path uses under pressure, see
            // TEMP_REG_SPILL_THRESHOLD above) -- the RHS recursion then reuses
            // real_reg/imag_reg directly and holds zero extra pool registers
            // live across itself, however deep the tree, at the cost of two
            // push/pop pairs per node (negligible next to the rest of complex
            // arithmetic).
            gen_complex_expr(vm, node->lhs, real_reg, imag_reg);
            // The LHS may itself have gone through this same path and reset the
            // temp pool; re-mark real_reg/imag_reg as in-use so the allocations
            // below can't hand back a register this level (or an ancestor
            // level, for real_reg/imag_reg that are themselves an outer call's
            // br/bi) still needs live. No-op for FREG_A* inputs, which aren't
            // part of the temp pool.
            mark_temp_reg_used(real_reg);
            mark_temp_reg_used(imag_reg);

            int fadd  = fop_for_type(node->ty->base, FADD3);
            int fsub  = fop_for_type(node->ty->base, FSUB3);
            int fmul  = fop_for_type(node->ty->base, FMUL3);
            int fdiv  = fop_for_type(node->ty->base, FDIV3);
            int fr2r  = fop_for_type(node->ty->base, FR2R);
            int r2fr  = fop_for_type(node->ty->base, R2FR);

            int r_tmp = alloc_temp_reg();
            emit_rr(vm, fr2r, r_tmp, real_reg); // real bits -> int reg
            emit_psh3(vm, r_tmp);               // save on the stack
            emit_rr(vm, fr2r, r_tmp, imag_reg); // imag bits -> int reg
            emit_psh3(vm, r_tmp);
            free_temp_reg(r_tmp); // nothing held across the RHS recursion

            gen_complex_expr(vm, node->rhs, real_reg, imag_reg);

            int br = alloc_temp_reg();
            int bi = alloc_temp_reg();
            emit_fmov3(vm, br, real_reg); // RHS result -> its own registers
            emit_fmov3(vm, bi, imag_reg);

            int r_tmp2 = alloc_temp_reg();
            emit_pop3(vm, r_tmp2); // LIFO: imag was pushed last
            emit_rr(vm, r2fr, imag_reg,
                    r_tmp2);       // int bits -> LHS imag register
            emit_pop3(vm, r_tmp2);
            emit_rr(vm, r2fr, real_reg,
                    r_tmp2);       // int bits -> LHS real register
            free_temp_reg(r_tmp2);

            int t0 = 0, t1 = 0, t2 = 0;
            if (node->kind == ND_MUL || node->kind == ND_DIV) {
                t0 = alloc_temp_reg();
                t1 = alloc_temp_reg();
                if (node->kind == ND_DIV)
                    t2 = alloc_temp_reg();
            }

            if (node->kind == ND_ADD) {
                emit_frrr(vm, fadd, real_reg, real_reg, br);
                emit_frrr(vm, fadd, imag_reg, imag_reg, bi);
            } else if (node->kind == ND_SUB) {
                emit_frrr(vm, fsub, real_reg, real_reg, br);
                emit_frrr(vm, fsub, imag_reg, imag_reg, bi);
            } else if (node->kind == ND_MUL) {
                emit_frrr(vm, fmul, t0, real_reg, br);
                emit_frrr(vm, fmul, t1, imag_reg, bi);
                emit_frrr(vm, fsub, t0, t0, t1);
                emit_frrr(vm, fmul, t1, real_reg, bi);
                emit_frrr(vm, fmul, imag_reg, imag_reg, br);
                emit_frrr(vm, fadd, imag_reg, t1, imag_reg);
                emit_fmov3(vm, real_reg, t0);
            } else {
                emit_frrr(vm, fmul, t0, br, br);
                emit_frrr(vm, fmul, t1, bi, bi);
                emit_frrr(vm, fadd, t0, t0, t1);
                emit_frrr(vm, fmul, t1, real_reg, br);
                emit_frrr(vm, fmul, t2, imag_reg, bi);
                emit_frrr(vm, fadd, t1, t1, t2);
                emit_frrr(vm, fdiv, t1, t1, t0);
                emit_frrr(vm, fmul, imag_reg, imag_reg, br);
                emit_frrr(vm, fmul, t2, real_reg, bi);
                emit_frrr(vm, fsub, imag_reg, imag_reg, t2);
                emit_frrr(vm, fdiv, imag_reg, imag_reg, t0);
                emit_fmov3(vm, real_reg, t1);
            }

            if (node->ty->base && node->ty->base->kind == TY_FLOAT) {
                emit_fround_f32(vm, real_reg, real_reg);
                emit_fround_f32(vm, imag_reg, imag_reg);
            }

            if (node->kind == ND_MUL || node->kind == ND_DIV) {
                if (node->kind == ND_DIV)
                    free_temp_reg(t2);
                free_temp_reg(t1);
                free_temp_reg(t0);
            }
            free_temp_reg(bi);
            free_temp_reg(br);
            return;
        }
        default:
            break;
    }

    error_tok(vm, node->tok, "unsupported complex expression");
}
