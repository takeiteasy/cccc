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
#include "./parse_internal.h"
#include "./serialize_internal.h"
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

// Emit address-of a local variable into dest_reg. Untagged: the frame may not
// push an epoch, and a recorded tag would then name a stale one.
static void emit_local_addr(VirtualMachine *vm, Obj *var, int dest_reg) {
    emit_lea3_var_internal(vm, dest_reg, var);
}

// Records where each cleanup handler is owed (see CleanupRange). Positions are
// text offsets, so they hold for the life of the function's code.
static bool cleanup_var_in_scope(Obj *var) {
    for (CleanupScopeEntry *s = g_cleanup_scope; s; s = s->outer)
        for (CleanupVar *cv = s->vars; cv; cv = cv->next)
            if (cv->var == var)
                return true;
    return false;
}

void cleanup_range_open(VirtualMachine *vm, Obj *var) {
    if (!cleanup_var_in_scope(var))
        return; // no exit code runs its handler
    CleanupRange *r    = arena_alloc(&vm->compiler.parser_arena, sizeof(*r));
    r->var             = var;
    r->start           = vm->text_ptr + 1;
    r->end             = -1;
    Obj *fn            = vm->compiler.current_fn;
    r->next            = obj_attr(fn)->cleanup_ranges;
    obj_attr_mut(vm, fn)->cleanup_ranges = r;
    obj_attr_mut(vm, var)->cleanup_cur   = r;
}

static void cleanup_range_close(VirtualMachine *vm, Obj *var, bool reopen) {
    CleanupRange *r = obj_attr(var)->cleanup_cur;
    if (!r)
        return;
    r->end           = vm->text_ptr + 1;
    r->reopen        = reopen;
    obj_attr_mut(vm, var)->cleanup_cur = NULL;
}

// Code after an early exit sequence is reachable only through labels, where
// the variables it closed are live again.
void cleanup_ranges_reopen(VirtualMachine *vm) {
    Obj *fn = vm->compiler.current_fn;
    for (CleanupRange *r = obj_attr(fn)->cleanup_ranges; r; r = r->next) {
        if (!r->reopen)
            continue;
        r->reopen = false;
        cleanup_range_open(vm, r->var);
    }
}

// Call cv->cleanup_fn(&cv->var). The cleanup fn returns void so REG_A0 is
// clobbered. Uses the standard CALL+patch mechanism (same as ND_FUNCALL for
// CCCC functions). An early exit (`early`) is followed by more code in which
// the variable stays live.
static void emit_one_cleanup(VirtualMachine *vm, CleanupVar *cv, bool early) {
    int r_addr = alloc_temp_reg();
    emit_local_addr(vm, cv->var, r_addr);
    // A VLA's slot holds the alloca pointer and a __block var's the heap box
    // pointer; the cleanup gets the storage's own address.
    if (cv->var->ty->kind == TY_VLA || cv->var->is_block_var)
        emit_rr(vm, LDR_D, r_addr, r_addr);
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
    cleanup_range_close(vm, cv->var, early);
    reset_temp_regs();
}

// Emit cleanup calls for one scope's vars (iterate in stored order = LIFO).
void emit_scope_cleanups(VirtualMachine *vm, CleanupScopeEntry *scope) {
    for (CleanupVar *cv = scope->vars; cv; cv = cv->next)
        emit_one_cleanup(vm, cv, false);
}

// Emit cleanup calls for all active scopes with depth > target_depth (innermost
// first), skipping vars a jump with this seq_limit precedes (0: no limit).
void emit_cleanups_to_depth(VirtualMachine *vm, int target_depth,
                            int seq_limit) {
    for (CleanupScopeEntry *s = g_cleanup_scope; s && s->depth > target_depth;
         s                    = s->outer)
        for (CleanupVar *cv = s->vars; cv; cv = cv->next)
            if (!seq_limit || obj_attr(cv->var)->cleanup_seq < seq_limit)
                emit_one_cleanup(vm, cv, true);
}

// ========== Forward Declarations ==========

// ========== Inline Assembly Passthru ==========

// Each asm statement becomes a host-C thunk taking its operands by address:
//   void __cccc_asm_passthru_N(void **ops) { __asm__(... : "=r"(*(int *)ops[0])
//   ...); }
// Thunks accumulate in vm->compiler.asm_passthru_src and are compiled into one
// shared library by cc_asm_passthru_flush at the end of each codegen pass.

#define PASSTHRU_MAX_OPS 32

typedef struct {
    bool  is_const[PASSTHRU_MAX_OPS];
    long long val[PASSTHRU_MAX_OPS];
    char  type[PASSTHRU_MAX_OPS][48]; // host spelling of the operand's type
    bool  is_input[PASSTHRU_MAX_OPS];
} PassthruOps;

#if !defined(_WIN32)

// True for `m`, `o`, `Q`...: constraints the host satisfies from an address.
static bool constraint_is_memory_only(const char *c) {
    c += strspn(c, "=+&%*#?!");
    return *c && strspn(c, "moVQ<>") == strlen(c);
}

// Writes the host spelling of a scalar operand type into `out`.
static bool passthru_scalar_type(Type *ty, char *out, size_t cap) {
    switch (ty->kind) {
        case TY_BOOL:
            snprintf(out, cap, "_Bool");
            return true;
        case TY_PTR:
            snprintf(out, cap, "void *");
            return true;
        case TY_FLOAT:
            snprintf(out, cap, "float");
            return true;
        case TY_DOUBLE:
            snprintf(out, cap, "double");
            return true;
        case TY_CHAR:
        case TY_SHORT:
        case TY_INT:
        case TY_LONG:
        case TY_ENUM: {
            const char *base = ty->size == 1   ? "char"
                               : ty->size == 2 ? "short"
                               : ty->size == 4 ? "int"
                               : ty->size == 8 ? "long long"
                                               : NULL;
            if (!base)
                return false;
            snprintf(out, cap, "%s %s", ty->is_unsigned ? "unsigned" : "signed",
                     base);
            return true;
        }
        default:
            return false;
    }
}

static bool passthru_is_bitfield(Node *n) {
    return n->kind == ND_MEMBER && n->member->is_bitfield;
}

static bool passthru_is_lvalue(Node *n) {
    if (n->kind == ND_MEMBER)
        return !n->member->is_bitfield;
    return n->kind == ND_VAR || n->kind == ND_DEREF;
}

static void passthru_write_operand(FILE *f, Node *arg, int idx, void *ud) {
    (void)arg;
    PassthruOps *ops = ud;
    if (ops->is_const[idx])
        fprintf(f, "(%s)%lldLL", ops->type[idx], ops->val[idx]);
    else if (strchr(ops->type[idx], '['))
        fprintf(f, "*(%sunsigned char (*)%s)ops[%d]",
                ops->is_input[idx] ? "const " : "",
                strchr(ops->type[idx], '['), idx);
    else
        fprintf(f, "*(%s%s *)ops[%d]", ops->is_input[idx] ? "const " : "",
                ops->type[idx], idx);
}

// Classifies every operand, rejecting what the host thunk cannot express.
// Aggregates keep their address only; arrays fed to a register constraint
// decay to a pointer.
static void passthru_classify(VirtualMachine *vm, Node *node, Node **args,
                              PassthruOps *ops) {
    int n_out = node_asm(node)->asm_n_outputs;
    int n_ops = n_out + node_asm(node)->asm_n_inputs;
    for (int i = 0; i < n_ops; i++) {
        Node *a   = args[i];
        Type *ty  = a->ty;
        bool  out = i < n_out;
        ops->is_input[i] = !out;
        bool aggregate = ty->kind == TY_STRUCT || ty->kind == TY_UNION ||
                         ty->kind == TY_ARRAY;
        bool mem_only =
            constraint_is_memory_only(node_asm(node)->asm_ops[i].constraint);

        if (mem_only && passthru_is_bitfield(a))
            error_tok(vm, a->tok, "asm memory operand cannot be a bit-field");
        if (!out &&!aggregate && is_integer(ty) && is_const_expr(vm, a) &&
            passthru_scalar_type(ty, ops->type[i], sizeof(ops->type[i]))) {
            ops->is_const[i] = true;
            ops->val[i]      = cc_eval(vm, a);
            continue;
        }
        if (aggregate && (mem_only || ty->kind != TY_ARRAY)) {
            if (!mem_only)
                error_tok(vm, a->tok,
                          "aggregate asm operand needs a memory constraint "
                          "with --asm-passthru");
            if (!passthru_is_lvalue(a))
                error_tok(vm, a->tok,
                          "asm memory operand is not addressable with "
                          "--asm-passthru");
            snprintf(ops->type[i], sizeof(ops->type[i]), "unsigned char[%d]",
                     ty->size);
            continue;
        }
        if (aggregate) // array decaying to a pointer
            snprintf(ops->type[i], sizeof(ops->type[i]), "void *");
        else if (!passthru_scalar_type(ty, ops->type[i],
                                       sizeof(ops->type[i])))
            error_tok(vm, a->tok,
                      "asm operand type is not supported with --asm-passthru");
        if (out && !passthru_is_lvalue(a) && !passthru_is_bitfield(a))
            error_tok(vm, a->tok, "asm output operand is not addressable with "
                                  "--asm-passthru");
    }
}

// error() plus the "N error generated." trailer the test runner (and users)
// recognise as a compile failure; gcc, unlike clang, prints none of its own.
static _Noreturn void passthru_fail(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n\n1 error generated.\n");
    exit(1);
}

// Runs cc once over `src`, dlopens the result and unlinks it. Errors out
// (host compiler diagnostics reach stderr unchanged) on failure.
static void *compile_passthru_so(const char *src, size_t len) {
    char src_template[] = "/tmp/cccc-asm-XXXXXX";
    int  src_fd         = mkstemp(src_template);
    if (src_fd < 0)
        passthru_fail("--asm-passthru: failed to create temp source file");
    close(src_fd);
    char src_path[sizeof(src_template) + 3];
    snprintf(src_path, sizeof(src_path), "%s.c", src_template);
    if (rename(src_template, src_path) != 0) {
        unlink(src_template);
        passthru_fail("--asm-passthru: failed to rename temp source file");
    }
    FILE *f = fopen(src_path, "w");
    if (!f || fwrite(src, 1, len, f) != len) {
        if (f)
            fclose(f);
        unlink(src_path);
        passthru_fail("--asm-passthru: failed to write temp source file");
    }
    fclose(f);

    char *cc = cccc_find_native_cc();
    if (!cc) {
        unlink(src_path);
        passthru_fail("--asm-passthru: no native C compiler found");
    }

    char so_template[] = "/tmp/cccc-asm-XXXXXX";
    int  so_fd         = mkstemp(so_template);
    if (so_fd < 0) {
        unlink(src_path);
        free(cc);
        passthru_fail("--asm-passthru: failed to create temp output file");
    }
    close(so_fd);

    pid_t pid = fork();
    if (pid < 0) {
        unlink(src_path);
        unlink(so_template);
        free(cc);
        passthru_fail("--asm-passthru: fork failed");
    }
    if (pid == 0) {
#if defined(__APPLE__)
        execlp(cc, cc, "-shared", "-o", so_template, src_path, (char *)NULL);
#else
        execlp(cc, cc, "-shared", "-fPIC", "-o", so_template, src_path,
               (char *)NULL);
#endif
        _exit(127);
    }
    int status;
    waitpid(pid, &status, 0);
    unlink(src_path);
    free(cc);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        unlink(so_template);
        passthru_fail("--asm-passthru: native compilation failed");
    }

    void *handle = dlopen(so_template, RTLD_NOW | RTLD_LOCAL);
    unlink(so_template);
    if (!handle)
        passthru_fail("--asm-passthru: dlopen failed: %s", dlerror());
    return handle;
}

static void passthru_append(VirtualMachine *vm, const char *buf, size_t blen) {
    size_t need  = vm->compiler.asm_passthru_len + blen + 1;
    char  *grown = realloc(vm->compiler.asm_passthru_src, need);
    if (!grown)
        passthru_fail("--asm-passthru: out of memory");
    memcpy(grown + vm->compiler.asm_passthru_len, buf, blen + 1);
    vm->compiler.asm_passthru_src = grown;
    vm->compiler.asm_passthru_len += blen;
}
#endif // !_WIN32

// A file-scope asm("...") joins the thunk batch verbatim, so the host shared
// library defines whatever symbols it declares.
void cc_asm_passthru_add_file_asm(VirtualMachine *vm, const char *text) {
#if !defined(_WIN32)
    char  *buf  = NULL;
    size_t blen = 0;
    FILE  *f    = open_memstream(&buf, &blen);
    if (!f)
        passthru_fail("--asm-passthru: out of memory");
    fprintf(f, "__asm__(");
    serialize_string_n(f, text, (int)strlen(text));
    fprintf(f, ");\n");
    fclose(f);
    passthru_append(vm, buf, blen);
    free(buf);
#else
    (void)vm;
    (void)text;
#endif
}

void cccc_default_asm_passthru(VirtualMachine *vm, Node *node) {
#if !defined(_WIN32)
    int n_out = node_asm(node)->asm_n_outputs;
    int n_ops = n_out + node_asm(node)->asm_n_inputs;
    if (node->asm_flags & ASM_GOTO)
        error_tok(vm, node->tok, "asm goto is not supported with "
                                 "--asm-passthru");
    if (n_ops > PASSTHRU_MAX_OPS)
        error_tok(vm, node->tok, "too many asm operands for --asm-passthru");

    Node       *args[PASSTHRU_MAX_OPS];
    PassthruOps ops = {0};
    int         k   = 0;
    for (Node *a = node->args; a && k < n_ops; a = a->next)
        args[k++] = a;
    passthru_classify(vm, node, args, &ops);

    char sym_name[64];
    snprintf(sym_name, sizeof(sym_name), "__cccc_asm_passthru_%d",
             vm->compiler.asm_passthru_count++);

    // Thunk source, appended to the pending batch.
    char  *buf = NULL;
    size_t blen = 0;
    FILE  *f   = open_memstream(&buf, &blen);
    if (!f)
        passthru_fail("--asm-passthru: out of memory");
    fprintf(f, "void %s(%s) {\n", sym_name, n_ops ? "void **ops" : "void");
    fprintf(f, "#line %d ", node->tok->line_no);
    serialize_string_n(f, node->tok->file->name,
                       (int)strlen(node->tok->file->name));
    fprintf(f, "\n    ");
    serialize_asm_stmt(f, vm, NULL, node, passthru_write_operand, &ops);
    fprintf(f, ";\n}\n");
    fclose(f);

    passthru_append(vm, buf, blen);
    free(buf);

    int ffi_idx = cc_register_asm_passthru_slot(vm, sym_name);

    // Scratch layout: operand pointers, value slots, then the container
    // address of each bit-field output (needed again after the call).
    long long base = n_ops ? alloc_wide_bitint_temp(vm, 3 * n_ops) : 0;
    if (n_ops) {
        int r_arr = alloc_temp_reg();
        for (int i = 0; i < n_ops; i++) {
            if (ops.is_const[i])
                continue;
            Node *a    = args[i];
            int   r_p  = alloc_temp_reg();
            bool  rval = !(passthru_is_lvalue(a) &&
                           (!(a->ty->kind == TY_ARRAY) ||
                            constraint_is_memory_only(
                                node_asm(node)->asm_ops[i].constraint)));
            if (i < n_out && passthru_is_bitfield(a)) {
                long long slot = base + n_ops + i;
                int       r_t  = alloc_temp_reg();
                gen_addr(vm, a, r_p);
                emit_lea3_aligned(vm, r_t, base + 2 * n_ops + i, 0, true);
                emit_store(vm, ty_ulong, r_p, r_t);
                if (node_asm(node)->asm_ops[i].constraint[0] == '+') {
                    int r_v = alloc_temp_reg();
                    emit_bitfield_load(vm, a->member, r_v, r_p,
                                       !addr_is_local_frame(vm, a));
                    emit_lea3_aligned(vm, r_t, slot, 0, true);
                    emit_store(vm, a->ty, r_v, r_t);
                    free_temp_reg(r_v);
                }
                emit_lea3_aligned(vm, r_p, slot, 0, true);
                free_temp_reg(r_t);
            } else if (!rval) {
                gen_addr(vm, a, r_p);
            } else {
                // Value operand: materialize into a scratch slot.
                long long slot = base + n_ops + i;
                int       r_v  = alloc_temp_reg();
                if (a->ty->kind == TY_ARRAY) {
                    gen_addr(vm, a, r_v);
                    emit_lea3_aligned(vm, r_p, slot, 0, true);
                    emit_store(vm, ty_ulong, r_v, r_p);
                } else if (is_flonum(a->ty)) {
                    int fs = gen_flonum_arg_to_scratch(vm, a);
                    emit_lea3_aligned(vm, r_p, slot, 0, true);
                    emit_store(vm, a->ty, fs, r_p);
                    free_temp_reg(fs);
                } else {
                    gen_expr(vm, a, r_v);
                    emit_lea3_aligned(vm, r_p, slot, 0, true);
                    emit_store(vm, a->ty, r_v, r_p);
                }
                free_temp_reg(r_v);
            }
            emit_lea3_aligned(vm, r_arr, base + i, 0, true);
            emit_store(vm, ty_ulong, r_p, r_arr);
            free_temp_reg(r_p);
        }
        emit_lea3_aligned(vm, REG_A0, base, 0, true);
        free_temp_reg(r_arr);
    }

    emit(vm, CALLF);
    emit_word(vm, ffi_idx);
    emit_word(vm, n_ops ? 1 : 0);
    emit_i64(vm, 0);
    emit_i64(vm, 0);

    for (int i = 0; i < n_out; i++) {
        Node *a = args[i];
        if (!passthru_is_bitfield(a))
            continue;
        int r_t = alloc_temp_reg();
        int r_p = alloc_temp_reg();
        int r_v = alloc_temp_reg();
        emit_lea3_aligned(vm, r_t, base + 2 * n_ops + i, 0, true);
        emit_load(vm, ty_ulong, r_p, r_t);
        emit_lea3_aligned(vm, r_t, base + n_ops + i, 0, true);
        emit_load(vm, a->ty, r_v, r_t);
        emit_bitfield_store(vm, a->member, r_v, r_p,
                            !addr_is_local_frame(vm, a));
        free_temp_reg(r_v);
        free_temp_reg(r_p);
        free_temp_reg(r_t);
    }
    reset_temp_regs();
#else
    (void)node;
    error("--asm-passthru is not supported on Windows");
#endif
}

// Compiles every pending thunk into one shared library and binds the FFI
// slots. Called at the end of each codegen pass, before anything can run.
void cc_asm_passthru_flush(VirtualMachine *vm) {
#if !defined(_WIN32)
    if (!vm->compiler.asm_passthru_len)
        return;
    void *handle = compile_passthru_so(vm->compiler.asm_passthru_src,
                                       vm->compiler.asm_passthru_len);
    for (int i = 0; i < vm->compiler.ffi_count; i++) {
        ForeignFunc *ff = &vm->compiler.ffi_table[i];
        if (!ff->is_asm_passthru || ff->func_ptr)
            continue;
        ff->func_ptr = dlsym(handle, ff->name);
        if (!ff->func_ptr)
            error("--asm-passthru: dlsym failed: %s", dlerror());
    }
    vm->compiler.asm_passthru_len = 0;
    if (vm->compiler.asm_passthru_src)
        vm->compiler.asm_passthru_src[0] = '\0';
#else
    (void)vm;
#endif
}

// ========== Nested Function Helpers ==========

// Find the __static_link local variable in a nested function's locals
Obj *find_static_link_var(Obj *fn) {
    if (!fn || !fn->is_nested)
        return NULL;
    for (Obj *var = obj_fn(fn)->locals; var; var = var->next) {
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
    for (int i = 0; i < obj_blk(block_fn)->num_captures; i++)
        if (obj_blk(block_fn)->captures[i] == var)
            return i;
    return -1;
}

// Ensure parent->local_set is populated with all its locals and params.
// Keyed by (long long)(intptr_t)var so membership is O(1). (#165)
static void ensure_local_set(VirtualMachine *vm, Obj *parent) {
    if (parent->local_set_built)
        return;
    for (Obj *local = obj_fn(parent)->locals; local; local = local->next)
        hashmap_put_int(&obj_fn_mut(vm, parent)->local_set, (long long)(intptr_t)local, local);
    for (Obj *param = obj_fn(parent)->params; param; param = param->next)
        hashmap_put_int(&obj_fn_mut(vm, parent)->local_set, (long long)(intptr_t)param, param);
    parent->local_set_built = true;
}

// Check if a variable belongs to an outer (enclosing) function.
// Returns the owning function, or NULL if it belongs to the current function.
// O(depth) using per-function lazy hash sets instead of O(depth * locals).
// (#165)
Obj *belongs_to_outer_function(VirtualMachine *vm, Obj *current_fn, Obj *var) {
    if (!current_fn || !current_fn->is_nested || !var || !var->is_local)
        return NULL;

    for (Obj *parent = obj_fn(current_fn)->parent_fn; parent;
         parent      = obj_fn(parent)->parent_fn) {
        ensure_local_set(vm, parent);
        if (hashmap_get_int(&obj_fn_mut(vm, parent)->local_set, (long long)(intptr_t)var))
            return parent;
    }
    return NULL;
}

// Calculate how many static chain links to follow to reach the owning function
static int calculate_chain_depth(Obj *current_fn, Obj *owner_fn) {
    int depth = 0;
    for (Obj *fn = current_fn; fn && fn != owner_fn; fn = obj_fn(fn)->parent_fn) {
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
    Obj *anc = obj_fn(current_fn)->parent_fn;
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
        anc = obj_fn(anc)->parent_fn;
    }

    // dest_reg now holds owner_fn's bp; add the variable's offset. Variable
    // offsets are in slots, so multiply by 8 bytes.
    emit_addi3(vm, dest_reg, dest_reg, var->offset * 8);
    int align_log = local_align_log(var);
    if (align_log) {
        long long align  = 8LL << align_log;
        int       r_mask = alloc_temp_reg();
        emit_addi3(vm, dest_reg, dest_reg, align - 1);
        emit_li3(vm, r_mask, -align);
        emit_rrr(vm, AND3, dest_reg, dest_reg, r_mask);
        free_temp_reg(r_mask);
    }
}

// Returns true when a ND_VAR node can be loaded/stored with a fused
// LDR_LOCAL/STR_LOCAL opcode instead of a LEA3+LDR/STR pair.
// Requires: node->kind == ND_VAR.
bool is_simple_local_scalar(VirtualMachine *vm, Node *node) {
    if (!node->var->is_local)
        return false;
    if (node->var->is_block_var)
        return false;
    // Over-aligned locals (#1137) need LEA3's align-up; the fused ops address
    // bp+offset directly.
    if (local_align_log(node->var))
        return false;
    // A variable captured by the enclosing block lives in the block descriptor
    // (reached via __static_link), not at its own frame offset.  It must go
    // through gen_addr's capture path, never the fused direct-frame load.
    if (vm->compiler.current_fn && vm->compiler.current_fn->is_block &&
        find_capture_index(vm->compiler.current_fn, node->var) >= 0)
        return false;
    if (belongs_to_outer_function(vm, vm->compiler.current_fn, node->var))
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
// #1449: indexing a local array (`buf[i]`, `m[i][j]`, `s.arr[i]`) is also
// frame-local: the ND_DEREF's pointer is an ND_ADD/ND_SUB chain whose base is
// an array object of the current frame (decay leaves the TY_ARRAY node in
// place). A pointer-typed base (`p[i]`, `char *q = buf; q[i]`) stays checked,
// since only its runtime value says whose frame it points into.
// Is `node` a member access directly on a union expression (`u.m`, not a
// struct nested inside a union or vice versa -- only the immediate parent
// matters, since that's the object whose bytes might legally carry more
// than one C11 effective type)? Used to gate CHKT3 emission around union
// member loads/stores so legal punning doesn't false-positive (#653).
bool is_union_member_access(Node *node) {
    return node && node->kind == ND_MEMBER && node->lhs && node->lhs->ty &&
           node->lhs->ty->kind == TY_UNION;
}

static bool ptr_is_local_array_rooted(VirtualMachine *vm, Node *ptr) {
    while (ptr &&
           (ptr->kind == ND_ADD || ptr->kind == ND_SUB ||
            (ptr->kind == ND_CAST && ptr->ty && ptr->ty->kind == TY_PTR)))
        ptr = ptr->lhs;
    return ptr && ptr->ty && ptr->ty->kind == TY_ARRAY &&
           (ptr->kind == ND_VAR || ptr->kind == ND_MEMBER ||
            ptr->kind == ND_DEREF) &&
           addr_is_local_frame(vm, ptr);
}

bool addr_is_local_frame(VirtualMachine *vm, Node *node) {
    // Defence in depth: a malformed AST (e.g. an ND_MEMBER recovery node with
    // a NULL lhs) must not segfault codegen. false = "not a known local
    // frame address", i.e. keep the runtime check -- the conservative answer.
    if (!node)
        return false;
    switch (node->kind) {
        case ND_VAR: {
            Obj *var = node->var;
            if (var->is_function || !var->is_local || var->is_block_var)
                return false;
            Obj *current_fn = vm->compiler.current_fn;
            if (current_fn && current_fn->is_block &&
                find_capture_index(current_fn, var) >= 0)
                return false;
            if (belongs_to_outer_function(vm, current_fn, var))
                return false;
            if (var->is_param &&
                (var->ty->kind == TY_STRUCT || var->ty->kind == TY_UNION ||
                 var->ty->kind == TY_VECTOR || is_wide_bitint(var->ty) ||
                 is_decimal(var->ty))) // #402: passed by pointer too
                return false;
            return true;
        }
        case ND_VLA_PTR: // the hidden pointer slot of a non-param local VLA
            return true;
        case ND_MEMBER:
            return addr_is_local_frame(vm, node->lhs);
        case ND_COMMA:
            return addr_is_local_frame(vm, node->rhs);
        case ND_DEREF:
            if (node->dbg_frame_local)
                return true;
            return node_chk(node)->rmw_lvalue
                       ? addr_is_local_frame(vm, node_chk(node)->rmw_lvalue)
                       : ptr_is_local_array_rooted(vm, node->lhs);
        default:
            return false; // everything else: keep checked
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

static Node *peel_ptr_casts(Node *n) {
    while (n && n->kind == ND_CAST && n->ty && n->ty->kind == TY_PTR)
        n = n->lhs;
    return n;
}

// Accessing an element through `m[i][j]` also requires row `m[i]` to exist, so
// each enclosing row subscript is an access of the whole row (#1452).
static void mark_row_accesses(Node *add) {
    for (Node *row = peel_ptr_casts(add->lhs);
         row && row->kind == ND_DEREF && row->ty && row->ty->kind == TY_ARRAY &&
         row->lhs && row->lhs->kind == ND_ADD;
         row = peel_ptr_casts(row->lhs->lhs))
        row->lhs->deref_bytes = row->ty->size;
}

// Byte size of the fixed-size array a pointer-add is being formed over (the
// pointer operand is the array object itself, decayed), or -1 when no static
// bound applies: pointer values, unsized arrays, and a struct's trailing
// array, which doubles as the flexible-array idiom (`char data[1]`).
long long static_array_bound(Node *ptr) {
    ptr = peel_ptr_casts(ptr);
    if (!ptr || !ptr->ty || ptr->ty->kind != TY_ARRAY || ptr->ty->size <= 0)
        return -1;
    if (ptr->kind == ND_MEMBER) {
        if (!ptr->member || !ptr->member->next)
            return -1;
    } else if (ptr->kind != ND_VAR && ptr->kind != ND_DEREF)
        return -1;
    return ptr->ty->size;
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
                 long long elem_size, long long scan_size) {
    emit_rrrs_i(vm, CHKNTZ, rs_addr, rs_hi, rs_src, 0, elem_size);
    emit_i64(vm, scan_size);
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
    if (node_chk(deref)->bounds_obj_init)
        gen_expr(vm, node_chk(deref)->bounds_obj_init, r_hi);
    gen_expr(vm, node_chk(deref)->bounds_hi, r_hi);
    return r_hi;
}

// Emit CHKAB (checked-pointer assignment-time bounds implication, #944).
// rs_val is the target's own declared bound being checked (lo when
// `is_hi` is false, hi when true); rs_slo/rs_shi are the source's snapshot
// bounds, already evaluated into registers by the caller. Traps unless
// rs_slo <= rs_val <= rs_shi. No-op at runtime unless CCCC_CHECKED_BOUNDS is
// set, same as CHKR/CHKNT; callers gate emission on that flag. `optional`
// emits CHKABO, which no-ops on the [-1, 0) sentinel source range.
void emit_chkab(VirtualMachine *vm, int rs_val, int rs_slo, int rs_shi,
                bool is_hi, bool optional) {
    emit_rrrs_i(vm, optional ? CHKABO : CHKAB, rs_val, rs_slo, rs_shi, 0,
                is_hi ? 1 : 0);
}

// Emit CHKDC (dynamic_check(cond), #486). rs_val is the already-evaluated
// condition; `line` is carried through only for the trap's diagnostic
// banner, not part of the test. No-op at runtime unless CCCC_CHECKED_BOUNDS
// is set, same as CHKR/CHKAB; caller gates emission on that flag.
void emit_chkdc(VirtualMachine *vm, int rs_val, long long line) {
    emit_rri(vm, CHKDC, rs_val, 0, line);
}

// ========== Address Generation ==========

// Generate address of an lvalue into dest_reg
void gen_addr(VirtualMachine *vm, Node *node, int dest_reg) {
    switch (node->kind) {
        case ND_VAR:
            if (node->var->is_function) {
                cc_reject_setjmp_address(vm, node);
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
                        belongs_to_outer_function(vm, current_fn, node->var);

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
                int cdata = cc_cdata_index_for_obj(vm, node->var);
                if (cdata >= 0)
                    emit_ri(vm, LDHA3, dest_reg, cdata);
                else if (node->var->is_tls)
                    emit_ldtls3(vm, dest_reg, node->var->offset);
                else
                    emit_lda3(vm, dest_reg, node->var->offset);
            }
            return;

        case ND_DEREF:
            // Address of *ptr is just ptr
            if (node->lhs->kind == ND_ADD && !node->no_deref_access &&
                node->ty->kind != TY_ARRAY && node->ty->kind != TY_VLA &&
                node->ty->kind != TY_FUNC) {
                node->lhs->deref_bytes = node->ty->size;
                mark_row_accesses(node->lhs);
            }
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
            if ((vm->flags & CCCC_CHECKED_BOUNDS) &&
                node_chk(node)->bounds_lo && node_chk(node)->bounds_hi) {
                mark_temp_reg_used(
                    dest_reg); // protect the just-computed address
                int r_lo = alloc_temp_reg();
                int r_hi = alloc_temp_reg();
                // #945: re-run the member-access object-expression hoist init
                // (if any) before reading lo/hi -- they may read it back
                // through
                // `*t`. Result discarded (r_lo is free scratch); only the store
                // to `t` matters.
                if (node_chk(node)->bounds_obj_init)
                    gen_expr(vm, node_chk(node)->bounds_obj_init, r_lo);
                gen_expr(vm, node_chk(node)->bounds_lo, r_lo);
                gen_expr(vm, node_chk(node)->bounds_hi, r_hi);
                emit_chkr(vm, dest_reg, r_lo, r_hi, node_chk(node)->access_size,
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
                // Never escapes: also retires a stale tag a dead frame left
                // at this slot (#1447).
                emit_lea3_internal(vm, dest_reg, node->var->offset);
            } else {
                error_tok(vm, node->tok, "VLA must be local");
            }
            return;

        // #1282: `f().member`/`f()->` on a function returning a struct/union
        // by value -- legal C (6.5.2.3p3: a function-call result is not an
        // lvalue, but member access on it is still permitted; you just can't
        // take &f().member's own address beyond this expression's lifetime,
        // which nothing here does). A struct/union-valued ND_FUNCALL already
        // has a caller-allocated ret_buffer (parse_postfix.c's funcall()),
        // and gen_expr's own ND_FUNCALL case already yields that buffer's
        // ADDRESS in dest_reg for a struct/union return (not the bytes --
        // the same by-reference convention every aggregate value uses
        // throughout this file), so there is nothing address-specific left
        // to do here: just run the call. Surfaced by the self-hosting spike
        // (src/macros.c: `comptime_aggregate_cast(cv).kind`).
        case ND_FUNCALL:
            if (node->ty->kind != TY_STRUCT && node->ty->kind != TY_UNION)
                error_tok(vm, node->tok, "not an lvalue");
            gen_expr(vm, node, dest_reg);
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
