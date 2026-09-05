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

// Serialization: expressions -- operator precedence/spelling,
// serialize_expr_raw's statement-expression bridge helpers, string
// literal escaping, block-capture/nested-env references, atomics,
// comma chains, _BitInt masking (#1150).
#include "./codegen_internal.h" // is_extern_func_name (#1155)
#include "./serialize_internal.h"

// Operator precedence (higher = binds tighter)
static int get_precedence(NodeKind kind) {
    switch (kind) {
        case ND_COMMA:
            return 1;
        case ND_ASSIGN:
            return 2;
        case ND_COND:
            return 3;
        case ND_LOGOR:
            return 4;
        case ND_LOGAND:
            return 5;
        case ND_BITOR:
            return 6;
        case ND_BITXOR:
            return 7;
        case ND_BITAND:
            return 8;
        case ND_EQ:
        case ND_NE:
            return 9;
        case ND_LT:
        case ND_LE:
            return 10;
        case ND_SHL:
        case ND_SHR:
            return 11;
        case ND_ADD:
        case ND_SUB:
            return 12;
        case ND_MUL:
        case ND_DIV:
        case ND_MOD:
            return 13;
        case ND_NEG:
        case ND_NOT:
        case ND_BITNOT:
        case ND_ADDR:
        case ND_DEREF:
        case ND_CAST:
            return 14;
        case ND_FUNCALL:
        case ND_MEMBER:
            return 15;
        default:
            return 16;
    }
}

// Get operator string for binary operations
static const char *get_binary_op_str(NodeKind kind) {
    switch (kind) {
        case ND_ADD:
            return "+";
        case ND_SUB:
            return "-";
        case ND_MUL:
            return "*";
        case ND_DIV:
            return "/";
        case ND_MOD:
            return "%";
        case ND_BITAND:
            return "&";
        case ND_BITOR:
            return "|";
        case ND_BITXOR:
            return "^";
        case ND_SHL:
            return "<<";
        case ND_SHR:
            return ">>";
        case ND_EQ:
            return "==";
        case ND_NE:
            return "!=";
        case ND_LT:
            return "<";
        case ND_LE:
            return "<=";
        case ND_LOGAND:
            return "&&";
        case ND_LOGOR:
            return "||";
        case ND_ASSIGN:
            return "=";
        case ND_COMMA:
            return ",";
        default:
            return "?";
    }
}

// Get operator string for unary operations
static const char *get_unary_op_str(NodeKind kind) {
    switch (kind) {
        case ND_NEG:
            return "-";
        case ND_NOT:
            return "!";
        case ND_BITNOT:
            return "~";
        case ND_ADDR:
            return "&";
        case ND_DEREF:
            return "*";
        default:
            return "?";
    }
}

// #918: usual_arith_conv() (src/type.c) casts BOTH operands of a pointer
// +/- integer expression to the same pointer type before new_add()/
// new_sub() (src/parse.c) return -- so by the time serialize_expr() sees an
// ND_ADD/ND_SUB node, node->lhs->ty and node->rhs->ty are indistinguishable
// (both TY_PTR). The scaled byte offset new_add()/new_sub() baked in
// (`rhs *= sizeof(*ptr)`) is only recoverable by peeling back to the
// *pre-cast* operand -- these three helpers do that peeling and classify
// what's underneath. Do not rely on operand position (new_add() canonicalizes
// pointer-to-lhs, but set_checked_deref_bounds() in parse.c builds ND_ADD via
// new_binary() directly, which does not canonicalize).
// #1300: peels exactly ONE cast layer, not an unbounded
// chain -- usual_arith_conv()'s own bogus pointer-typed wrap (the one this
// helper exists to see past) is applied exactly once, directly around the
// scaled offset/pointer operand. A `while` loop here also peels *underneath*
// a second, genuinely meaningful cast the ORIGINAL guest source itself
// wrote -- e.g. `(char *)an_integer_variable`, an explicit integer-to-
// pointer conversion (pthread.c's own `(int *)((char *)once_control +
// offsetof(pthread_once_t, __opaque))`, `once_control` a `long long`
// parameter) -- revealing the *pre-cast* integer operand and misclassifying
// a genuinely pointer-typed operand position as integer-ish, which fell
// through to the plain-arithmetic branch below and printed the RHS's own
// still-bogus pointer-typed cast unstripped: `(char *)x + (char *)(offset)`,
// an "invalid operands" error on two `char *`s (confirmed: reproduces with
// no cccc source involved at all, any `(char *)an_int_expr + offsetof(...)`
// pattern under -c=native).
static Node *strip_casts(Node *n) {
    if (n && n->kind == ND_CAST && n->lhs)
        n = n->lhs;
    return n;
}

static bool node_is_pointerish(Node *n) {
    // #964: TY_VLA decays the same as TY_ARRAY in pointer arithmetic (`v + 1`
    // on a VLA `v`) -- without it here, the ND_ADD/ND_SUB case below falls
    // through to plain binary arithmetic and adds two pointers together.
    return n && n->ty &&
           (n->ty->kind == TY_PTR || n->ty->kind == TY_ARRAY ||
            n->ty->kind == TY_VLA);
}

static bool node_is_integerish(Node *n) {
    return n && n->ty && is_integer(n->ty);
}

// node_is_vla_ptr_assign / node_is_deferred_vla_ptr_init / block_defines_vla
// moved to internal.h (#981) so codegen.c can reuse the identical
// "does this block declare a VLA" check for HMRK/HREL emission -- see
// their comments there.

// Returns true if the node produces no output (effectively a no-op expression).
bool is_noop_expr(Node *node) {
    if (!node)
        return true;
    if (node->kind == ND_NULL_EXPR)
        return true;
    if (node->kind == ND_COMMA)
        return is_noop_expr(node->lhs) && is_noop_expr(node->rhs);
    return false;
}

// #1103: true if `node`'s lvalue-computation subtree bottoms out on a
// direct field access `v.__opaque` (or `v.__opaque[i]`, lowered to pointer
// arithmetic over that field) -- the synthetic reserved-storage member
// convention CCCC's own from_include headers use to hide a platform-
// varying internal layout behind a fixed byte buffer with no counterpart
// in the real host struct (include/wchar.h's mbstate_t, include/fenv.h's
// fenv_t/fexcept_t). This is deliberately narrower than "any store into a
// host-owned-layout type's member" -- an ordinary from_include struct's
// real, POSIX-named members (struct timespec's tv_sec/tv_nsec, struct
// statfs's f_bsize, ...) are genuinely declared under the same name by
// the host header too (member ACCESS already re-resolves correctly for
// those, see type_def_is_from_include_suppressed()'s own comment), so a
// plain member-path store to one of those is fine exactly as emitted --
// see tools/comptime_native_smoke.py's own `struct timespec nap = {0,
// 20000000}` case, which a broader "any member of a host-owned type"
// check wrongly flagged as an unrepresentable non-zero store. `__opaque`
// is CCCC's own invention and never exists on the host, so storing
// through it is the only shape that's actually unrepresentable once the
// real header is in scope.
static bool expr_roots_at_opaque_member(Node *node, Obj *v) {
    if (!node)
        return false;
    if (node->kind == ND_MEMBER)
        return node->lhs && node->lhs->kind == ND_VAR && node->lhs->var == v &&
               node->member && node->member->name &&
               node->member->name->len == 8 &&
               strncmp(node->member->name->loc, "__opaque", 8) == 0;
    return expr_roots_at_opaque_member(node->lhs, v) ||
           expr_roots_at_opaque_member(node->rhs, v);
}

// #1103: true if `node` is a compile-time-constant zero -- the only shape
// a `{0}` (or any all-zero) initializer's per-member stores ever produce
// (see ND_MEMZERO's own sibling stores in a lowered aggregate
// initializer). Unwraps ND_CAST since every such store's rhs is
// value-cast to the member's own type.
static bool expr_is_zero_constant(Node *node) {
    while (node && node->kind == ND_CAST)
        node = node->lhs;
    return node && node->kind == ND_NUM && node->val == 0;
}

// #1103: classifies one link (an ND_ASSIGN or the chain's own trailing
// non-comma tail) of a `{0}`-initializer comma chain rooted at host-owned-
// layout local `v` (see the ND_COMMA case's own comment). Returns:
//   - DROP: an assignment through a synthetic `__opaque` reserved-storage
//     member into `v` storing a constant zero -- redundant with the
//     chain's own leading ND_MEMZERO, and printing it would walk a member
//     name that doesn't exist on the host at all (see
//     expr_roots_at_opaque_member()'s own comment).
//   - ERROR: the same `__opaque` shape storing anything other than a
//     constant zero -- genuinely unrepresentable, so this fails loudly
//     rather than emit a store through a member the host header doesn't
//     declare (matching serialize_init_bytes's own stated policy for the
//     analogous unsupported-shape case).
//   - KEEP: anything else -- a whole-object reference to `v`, a store
//     through one of `v`'s ordinary (real, host-shared-name) members, or a
//     node unrelated to `v` entirely.
typedef enum {
    HOST_OWNED_KEEP,
    HOST_OWNED_DROP,
    HOST_OWNED_ERROR
} HostOwnedZeroInitAction;
static HostOwnedZeroInitAction host_owned_zero_init_classify(Node *node,
                                                             Obj  *v) {
    // #1289: a `{0}`/`{}` initializer whose every member store folds away
    // as redundantly zero (create_lvar_init(), parse_init.c) leaves the
    // chain's tail as a bare ND_NULL_EXPR instead of an ND_ASSIGN --
    // serializes to nothing (is_noop_expr()/serialize_expr's own
    // ND_NULL_EXPR case), so it must be dropped exactly like a
    // HOST_OWNED_DROP link rather than printed as a HOST_OWNED_KEEP value:
    // printing it left a `" , "` separator with an empty operand on both
    // sides (`memset(...) , ;` and `(memset(...) ,  , tmp)`), which the
    // generic ND_COMMA case just below (is_noop_expr guards) already
    // avoids for the non-host-owned path.
    if (is_noop_expr(node))
        return HOST_OWNED_DROP;
    if (!node || node->kind != ND_ASSIGN)
        return HOST_OWNED_KEEP;
    Node *target = node->lhs;
    if (target->kind == ND_VAR && target->var == v)
        return HOST_OWNED_KEEP; // whole-object reference, untouched
    if (!expr_roots_at_opaque_member(target, v))
        return HOST_OWNED_KEEP; // a real, host-shared member name -- fine as-is
    return expr_is_zero_constant(node->rhs) ? HOST_OWNED_DROP
                                            : HOST_OWNED_ERROR;
}

// #1103: walks the right-nested tail of a `{0}`-initializer comma chain
// rooted at host-owned-layout local `v` (called from the ND_COMMA case
// below, on node->rhs, right after that case has already printed the
// chain's own leading ND_MEMZERO) -- drops each HOST_OWNED_DROP link,
// errors loudly on a HOST_OWNED_ERROR link, and otherwise prints the link
// (a HOST_OWNED_KEEP assign, or the chain's own non-comma trailing value)
// with the right number of ` , ` separators regardless of how many
// earlier links got dropped. `*printed` tracks whether anything has been
// printed yet by the caller (the leading memzero counts).
static void serialize_host_owned_zero_init_chain(FILE *f, VirtualMachine *vm,
                                                 SerializeContext *ctx,
                                                 Node *node, Obj *v,
                                                 int node_prec, bool *printed) {
    if (node->kind != ND_COMMA) {
        HostOwnedZeroInitAction action = host_owned_zero_init_classify(node, v);
        if (action == HOST_OWNED_ERROR)
            error("cccc: cannot serialize a non-zero initializer store into "
                  "'%s' in native mode: the member path is CCCC's own "
                  "host-owned-layout projection, not necessarily the host's "
                  "real layout (see #1103)",
                  v->name);
        if (action == HOST_OWNED_DROP)
            return;
        if (*printed)
            fprintf(f, " , ");
        serialize_expr(f, vm, ctx, node, node_prec + 1);
        *printed = true;
        return;
    }
    HostOwnedZeroInitAction action =
        host_owned_zero_init_classify(node->lhs, v);
    if (action == HOST_OWNED_ERROR)
        error("cccc: cannot serialize a non-zero initializer store into "
              "'%s' in native mode: the member path is CCCC's own "
              "host-owned-layout projection, not necessarily the host's "
              "real layout (see #1103)",
              v->name);
    if (action != HOST_OWNED_DROP) {
        if (*printed)
            fprintf(f, " , ");
        serialize_expr(f, vm, ctx, node->lhs, node_prec);
        *printed = true;
    }
    serialize_host_owned_zero_init_chain(f, vm, ctx, node->rhs, v, node_prec,
                                         printed);
}

// Forward declaration
// #1124: serialize_expr's actual switch-on-node->kind body, renamed so the
// public serialize_expr can wrap it with the _BitInt width-mask check below
// without recursing into itself.
static void serialize_expr_raw(FILE *f, VirtualMachine *vm,
                               SerializeContext *ctx, Node *node,
                               int parent_prec);
// #1062/#1085: matches CCCC's own struct va_list structurally (defined
// further down, near its own long comment) -- forward-declared here so
// ND_FUNCALL (inside serialize_expr's own switch, above the definition
// textually) can call it too.
// #1074/#1080: does `var` belong to `fn`'s own locals list? Defined near
// serialize_nested_preamble() (with the rest of the nested-function-upvar
// machinery); forward-declared here so ND_BLOCK_LITERAL's capture-copy
// (serialize_expr, above that machinery in file order) can reuse it.
// #1136: defined near serialize_global_var (with the rest of the
// declaration-printing machinery); forward-declared here so the hoisted-local
// declarator (serialize_function, above that machinery in file order) can
// reuse it too.
// #1103: defined near the other include-provenance predicates (with
// global_is_header_supplied); forward-declared here so
// rename_colliding_static_names() (above that machinery in file order) can
// share the exact same "does this definition actually reach the output"
// test the emitter itself uses, instead of a hand-rolled paraphrase that can
// drift out of sync with it.
// #964: mutually recursive with serialize_stmt -- see the comment on its
// definition, near ND_BLOCK below.

// Print an escaped string literal covering exactly `len` bytes of `str` --
// NOT NUL-terminated iteration. #918: a NUL-terminated for-loop (the
// previous implementation) truncates at the first embedded NUL, silently
// dropping any bytes after it (e.g. `char a[4] = {1,0,2,0};`, legal C with
// no string semantics at all). NUL bytes are always escaped as the 3-digit
// octal form `\000` (never the 1-digit `\0`) -- `\0` immediately followed
// by an ASCII digit in the emitted source (e.g. a NUL followed by the
// character '1') would be misparsed by the host compiler as a 2-digit
// octal escape `\01`; `\000` has no such ambiguity.
void serialize_string_n(FILE *f, const char *str, int len) {
    // A C string literal always gets an implicit trailing NUL from the host
    // compiler regardless of what's inside the quotes, so a `str`/`len` that
    // already ends in one (the common case: an ordinary string literal's
    // array_len includes its own terminator) doesn't need that last byte
    // spelled out explicitly -- the compiler adds an identical one right
    // back. Strip exactly one such trailing NUL, not more: any NUL before
    // it is genuine embedded content (#918) and must stay escaped, and the
    // resulting byte sequence is identical either way once the array
    // declaration's own size (set independently, from the same `len` this
    // caller had before calling in) still governs how many bytes actually
    // land in memory. This was previously spelled out unconditionally --
    // harmless for plain data, but a `\000` this close to the closing quote
    // of a printf-style format-string argument reads to clang's -Wformat
    // checker as a suspicious embedded NUL and fails a -Werror native
    // build (clang 18/Ubuntu, Linux aarch64 release CI) even though the
    // bytes were always correct.
    if (len > 0 && str[len - 1] == '\0')
        len--;
    fprintf(f, "\"");
    for (int i = 0; i < len; i++) {
        unsigned char c = (unsigned char)str[i];
        switch (c) {
            case '\n':
                fprintf(f, "\\n");
                break;
            case '\r':
                fprintf(f, "\\r");
                break;
            case '\t':
                fprintf(f, "\\t");
                break;
            case '\\':
                fprintf(f, "\\\\");
                break;
            case '"':
                fprintf(f, "\\\"");
                break;
            default:
                if (c >= 32 && c < 127)
                    fputc(c, f);
                else
                    fprintf(f, "\\%03o", c);
                break;
        }
    }
    fprintf(f, "\"");
}

// #965: index of `var` in `block_fn`'s capture list, or -1. Mirrors
// find_capture_index (codegen.c) exactly -- kept as an independent copy
// here rather than shared, since codegen's version is `static` in a
// different translation unit and this file's dependency surface is
// otherwise limited to internal.h's declarations.
int block_capture_index(Obj *block_fn, Obj *var) {
    if (!block_fn || !var)
        return -1;
    for (int i = 0; i < block_fn->num_captures; i++)
        if (block_fn->captures[i] == var)
            return i;
    return -1;
}

// #965: the env struct name serialize_block_preamble() paired with
// `block_fn`, or NULL if `block_fn` isn't a block function (or somehow has
// no entry -- defensive only, every Obj.is_block function gets one).
const char *find_block_env(SerializeContext *ctx, Obj *block_fn) {
    for (int i = 0; i < ctx->block_envs_len; i++)
        if (ctx->block_envs[i].block_fn == block_fn)
            return ctx->block_envs[i].env_struct_name;
    return NULL;
}

// #965: when `var` is captured by the block literal currently being
// serialized (ctx->current_fn), print the descriptor-field access that
// reaches it through __static_link and return true; otherwise print
// nothing and return false so the caller falls back to the variable's
// plain name. Mirrors gen_addr's cap_idx >= 0 branch (codegen.c) exactly:
// a plain capture's descriptor field holds the snapshotted value directly,
// while an is_block_var capture's field holds the shared heap box's
// pointer, so reading the *value* needs one extra dereference -- the `->`
// in `(*((T*)p)->__capK)` already binds tighter than the outer `*`, so this
// is unambiguous without further parenthesization.
static bool serialize_block_capture_ref(FILE *f, SerializeContext *ctx,
                                        Obj *var) {
    if (!ctx->current_fn || !ctx->current_fn->is_block)
        return false;
    int idx = block_capture_index(ctx->current_fn, var);
    if (idx < 0)
        return false;
    const char *env = find_block_env(ctx, ctx->current_fn);
    if (!env)
        env = "struct __cccc_block_env_?"; // defensive only, see find_block_env
    if (var->is_block_var)
        fprintf(f, "(*((%s *)__static_link)->__cap%d)", env, idx);
    else
        fprintf(f, "((%s *)__static_link)->__cap%d", env, idx);
    return true;
}

// #1074: the env struct name serialize_nested_preamble() paired with
// `owner_fn`, or a defensive placeholder if somehow missing (every function
// that directly parents a nested function gets an entry -- see
// NestedEnvEntry's comment).
static const char *find_nested_env_name(SerializeContext *ctx, Obj *owner_fn) {
    for (int i = 0; i < ctx->nested_envs_len; i++)
        if (ctx->nested_envs[i].owner_fn == owner_fn)
            return ctx->nested_envs[i].env_struct_name;
    return "struct __cccc_nenv_?";
}

// #1074: builds a C expression, of type `<target>'s own env struct> *`,
// giving the address of `target`'s env struct instance as seen from inside
// `from_fn`'s own serialized body -- `from_fn` must be nested and `target`
// must be `from_fn`'s immediate parent or a stricter ancestor of it (never
// `from_fn` itself; callers already special-case that, since it just means
// "my own env", `&__cccc_nenv`, with nothing to chase).
//
// Mirrors codegen_expr.c's calling_nested static-link chase
// (:2938-2960) exactly: `from_fn`'s own `__static_link` parameter already
// *is* a pointer to its immediate parent's env (untyped `void *`, so it
// only needs a cast); reaching a stricter ancestor means walking that
// env's own `__up` field once per intervening level, casting at each hop.
static char *nested_env_ptr_expr(VirtualMachine *vm, SerializeContext *ctx,
                                 Obj *from_fn, Obj *target) {
    Obj  *p = from_fn->parent_fn;
    char *expr =
        arena_format(vm, "((%s *)__static_link)", find_nested_env_name(ctx, p));
    while (p != target) {
        p    = p->parent_fn;
        expr = arena_format(vm, "((%s *)%s->__up)",
                            find_nested_env_name(ctx, p), expr);
    }
    return expr;
}

// #1081: builds a C expression, of type `<block_anc's own descriptor
// struct> *`, giving block_anc's real descriptor pointer as seen from
// inside from_fn's own serialized body. from_fn must be nested and
// block_anc one of its ancestors that is a genuine Apple block
// (Obj.is_block) not directly owning the variable being reached (see
// serialize_nested_upvar_ref()'s own climb). Reuses nested_env_ptr_expr's
// existing chase up to block_anc's own env struct -- populated exactly
// like any other nested function's env, purely to pass block_anc's own
// directly-owned locals/params to ITS OWN nested children (e.g. a nested
// function reading the block's own param, the already-correct single-hop
// case) -- and adds one more `.__up` hop: serialize_function's populator
// sets a nested function's own env's `.__up` field to its own
// `__static_link` verbatim (`fn->is_nested ? "__static_link" : "(void
// *)0"`), and block_anc's REAL `__static_link` IS its descriptor pointer
// (ND_BLOCK_CALL always passes the descriptor as its callee's A0/
// __static_link) -- so this single extra hop reaches it exactly, whatever
// its own env's `.__up` field looks like is irrelevant here.
static char *block_ancestor_desc_ptr_expr(VirtualMachine   *vm,
                                          SerializeContext *ctx, Obj *from_fn,
                                          Obj *block_anc) {
    const char *env = find_block_env(ctx, block_anc);
    if (!env)
        env = "struct __cccc_block_env_?"; // defensive only, see find_block_env
    return arena_format(vm, "((%s *)%s->__up)", env,
                        nested_env_ptr_expr(vm, ctx, from_fn, block_anc));
}

// #1074: when `var` is an upvar of the nested function currently being
// serialized (ctx->current_fn) -- i.e. a local/param owned by one of its
// ancestors, recorded by serialize_nested_preamble()'s analysis pass --
// print the env-chase expression that reaches it and return true;
// otherwise print nothing and return false so the caller falls back to the
// variable's plain name (its own local/param, or a block capture, handled
// separately above). Every legal upvar reference was already validated
// (VLA/deferred-VLA-pointer/__block-storage locals rejected, see
// serialize_nested_preamble()) by the time this ever runs.
static bool serialize_nested_upvar_ref(FILE *f, VirtualMachine *vm,
                                       SerializeContext *ctx, Obj *var) {
    // is_block reuses is_nested for VM codegen purposes (parse_blocks.c) --
    // a block's own outer-local reference is already fully handled by
    // serialize_block_capture_ref() above, a distinct, already-correct
    // mechanism; excluded here so it's never double-handled.
    if (!ctx->current_fn || !ctx->current_fn->is_nested ||
        ctx->current_fn->is_block)
        return false;
    for (Obj *v = ctx->current_fn->locals; v; v = v->next)
        if (v == var)
            return false; // owned by the nested function itself, not an upvar
    Obj *owner       = NULL;
    Obj *block_owner = NULL;
    for (Obj *anc = ctx->current_fn->parent_fn; anc; anc = anc->parent_fn) {
        bool is_own = false;
        for (Obj *v = anc->locals; v; v = v->next)
            if (v == var) {
                is_own = true;
                break;
            }
        if (is_own) {
            owner = anc;
            break;
        }
        // #1081: `anc` doesn't own `var` directly -- if `anc` is a block,
        // it's a hard boundary (see record_nested_upvar()'s identical
        // climb): `var` is owned by one of THAT BLOCK's own ancestors, but
        // was already captured transitively into the block's own
        // descriptor at parse time, and must be read from there rather
        // than by continuing this climb toward the real, further-out
        // owner.
        if (anc->is_block) {
            block_owner = anc;
            break;
        }
    }
    if (block_owner) {
        int idx = block_capture_index(block_owner, var);
        if (idx < 0)
            return false; // defensive only -- record_nested_upvar() already
                          // validated this at parse-adjacent analysis time
        char *desc_ptr =
            block_ancestor_desc_ptr_expr(vm, ctx, ctx->current_fn, block_owner);
        // Mirrors serialize_block_capture_ref()'s identical is_block_var
        // arm: the descriptor's own capture slot holds the shared heap
        // box's pointer for a __block local, one extra dereference to
        // reach the value.
        if (var->is_block_var)
            fprintf(f, "(*%s->__cap%d)", desc_ptr, idx);
        else
            fprintf(f, "%s->__cap%d", desc_ptr, idx);
        return true;
    }
    if (!owner)
        return false; // defensive only -- see the identical scan below
    NestedEnvEntry *e = NULL;
    for (int i = 0; i < ctx->nested_envs_len; i++)
        if (ctx->nested_envs[i].owner_fn == owner) {
            e = &ctx->nested_envs[i];
            break;
        }
    if (!e)
        return false; // defensive only -- owner must already have an entry
    int idx = -1;
    for (int i = 0; i < e->upvars_len; i++)
        if (e->upvars[i] == var) {
            idx = i;
            break;
        }
    if (idx < 0)
        return false; // defensive only

    // #1080: a __block-storage upvar's field holds a T ** (address of the
    // shared heap-box pointer) -- one more dereference than an ordinary
    // upvar's T * to reach the actual value, mirroring
    // serialize_block_capture_ref()'s identical is_block_var arm.
    char *env_ptr = nested_env_ptr_expr(vm, ctx, ctx->current_fn, owner);
    if (var->is_block_var)
        fprintf(f, "(**%s->__uv%d)", env_ptr, idx);
    else
        fprintf(f, "(*%s->__uv%d)", env_ptr, idx);
    return true;
}

// True when an ND_ALOAD/ND_ASTORE address expression has a pointee the
// __atomic_* builtins accept. Mirrors codegen's ALDR/ASTR guard (1/2/4/8-byte
// non-float pointee); anything else takes codegen's plain load/store fallback,
// so serializing it as a plain dereference matches the VM -- and
// __atomic_load_n would not compile on a float or aggregate pointee anyway.
bool atomic_serializable_pointee(Node *addr) {
    if (!addr || !addr->ty || !addr->ty->base)
        return false;
    Type *base = addr->ty->base;
    if (is_flonum(base))
        return false;
    return base->size == 1 || base->size == 2 || base->size == 4 ||
           base->size == 8;
}

// #1188: cccc's memory_order enum (include/stdatomic.h) is declared in the
// same relaxed/consume/acquire/release/acq_rel/seq_cst order as glibc's, so
// its values already coincide with GCC/Clang's __ATOMIC_* numbering -- but
// that coincidence is only used here to pick a *name* for readability, never
// relied on to skip emitting the value: an out-of-range constant falls
// through to the raw-integer arm below rather than being clamped or asserted.
static const char *atomic_order_name(long long order) {
    switch (order) {
        case 0:
            return "__ATOMIC_RELAXED";
        case 1:
            return "__ATOMIC_CONSUME";
        case 2:
            return "__ATOMIC_ACQUIRE";
        case 3:
            return "__ATOMIC_RELEASE";
        case 4:
            return "__ATOMIC_ACQ_REL";
        case 5:
            return "__ATOMIC_SEQ_CST";
        default:
            return NULL;
    }
}

// #1101: an address operand handed to a __atomic_* builtin must not carry
// the _Atomic qualifier in its static type -- clang rejects
// `__atomic_store_n(&x, ...)` outright when x is spelled through a host
// atomic typedef ("address argument to atomic operation must be a pointer
// to integer or pointer ('atomic_int *' (aka '_Atomic(int) *') invalid)"),
// because declarations print through those typedef names (atomic_int -> the
// real <stdatomic.h> _Atomic int) and so &x has type _Atomic(int)*. The
// builtins' own contract wants a pointer to the *unqualified* pointee --
// atomicity lives in the object, not the argument's spelling, exactly as
// the hand-written __cccc_ensure_mtx/__cccc_ensure_cnd shims below already
// assume when they cast once_flag/mtx->__handle for their own __atomic_*
// calls. So when the operand is a pointer to an _Atomic-qualified pointee,
// emit a cast to that pointee with the qualifier stripped: a shallow copy
// of the pointee Type with origin severed, so find_typedef_name_exact
// cannot re-spell the atomic typedef by identity and the canonical spelling
// ("int", "long", "_Bool", "void *"...) falls out instead. Anything else
// serializes byte-identical to before.
void serialize_atomic_addr(FILE *f, VirtualMachine *vm, SerializeContext *ctx,
                           Node *addr) {
    if (!addr || !addr->ty || addr->ty->kind != TY_PTR || !addr->ty->base ||
        !addr->ty->base->is_atomic) {
        serialize_expr(f, vm, ctx, addr, 2);
        return;
    }
    Type pointee      = *addr->ty->base;
    pointee.is_atomic = false;
    pointee.origin    = NULL;
    fprintf(f, "(");
    serialize_type(f, ctx, &pointee);
    fprintf(f, " *)");
    // Same comma-guarded argument position the bare operand occupied
    // before (#1042(b)).
    serialize_expr(f, vm, ctx, addr, 2);
}

// #1018 follow-up: C's default argument promotions (C17 6.5.2.2p6/7,
// applied to a variadic call's own trailing arguments) mean a real
// `va_arg(ap, T)` may never spell a promotable T -- clang/gcc both
// diagnose it explicitly ("second argument to 'va_arg' is of promotable
// type 'float'/'char'/...; this va_arg has undefined behavior because
// arguments will be promoted to 'double'/'int'"). CCCC's own VM-ABI
// va_arg macro reads exactly `type`'s own width from the slot (its
// __builtin_choose_expr's fp arm already special-cases float, but only to
// pick the 8-byte double-read path -- the *printed* type argument still
// came from the user's literal `type` text, unpromoted) and happens to
// work regardless, since the VM's own calling convention always writes a
// full 8-byte slot -- so this had no VM-visible symptom, only a native
// diagnostic (confirmed against real clang: unpromoted `char`/`float`
// both actually round-trip correctly here since clang's promotion and
// CCCC's slot width agree, but printing the promoted type is what a real
// host compiler's own <stdarg.h> expects and is the only way to silence
// the UB warning). Mirrors integer_promotion (type.c, static) for the
// integer half; float is the one additional case that helper doesn't
// cover, since it's scoped to is_integer().
static Type *va_arg_promoted_type(Type *ty) {
    if (!ty)
        return ty;
    if (ty->kind == TY_FLOAT)
        return ty_double;
    if (is_integer(ty) && ty->kind != TY_BITINT && ty->size < 4)
        return ty_int;
    return ty;
}

// #1102: the rightmost operand of a comma chain, drilling through nested
// ND_COMMA nodes (an initializer's balanced-comma tree nests both ways, but
// the compound literal's hidden temp always sits on the right spine).
static Node *comma_chain_tail(Node *node) {
    while (node && node->kind == ND_COMMA && node->rhs)
        node = node->rhs;
    return node;
}

// #1102: is this chain's tail something whose address C allows taking? A
// plain variable, a member access, or a dereference -- exactly the lvalue
// shapes a lowered expression can end in. Anything else (a number, a binary
// result, another rvalue) falls back to the generic spelling.
static bool addr_comma_tail_is_lvalue(Node *chain) {
    Node *tail = comma_chain_tail(chain);
    return tail && (tail->kind == ND_VAR || tail->kind == ND_MEMBER ||
                    tail->kind == ND_DEREF);
}

// #1102 followup: drill through a postfix lvalue shell -- member accesses,
// plus the explicit ND_DEREF that `->` lowers to -- down to whatever sits
// at its base (`&((struct P){30, 12}).x` is ADDR(MEMBER(COMMA))). Returns
// that base only when it is a comma chain; NULL otherwise, leaving every
// other shape to the generic spelling.
static Node *addr_comma_base(Node *shell) {
    Node *n = shell;
    while (n && (n->kind == ND_MEMBER || n->kind == ND_DEREF))
        n = n->lhs;
    return n && n->kind == ND_COMMA ? n : NULL;
}

// #1102 followup: spell a MEMBER/DEREF shell exactly as serialize_expr()
// would, except that the comma chain at its bottom -- already emitted
// earlier in the surrounding `(chain..., ...)` sequence -- is replaced by
// the bare tail operand. This is what lets the & bind *inside* the chain
// while still covering every `.member`/`*` step above it:
// `&(memset(...) , t, t).x` re-spells as `(memset(...) , &t.x)`. Precedence
// bookkeeping mirrors serialize_expr's own (MEMBER 15 / DEREF 14), so a
// deref under a member access keeps its parentheses -- `->` lowers to
// MEMBER(DEREF(base)), which must come out `(*t).m`, not `*t.m`.
static void serialize_addr_shell(FILE *f, VirtualMachine *vm,
                                 SerializeContext *ctx, Node *n, Node *base,
                                 Node *tail, int parent_prec) {
    if (!n || n == base) {
        serialize_expr(f, vm, ctx, n == base ? tail : n, parent_prec);
        return;
    }
    int  node_prec   = get_precedence(n->kind);
    bool need_parens = node_prec < parent_prec;
    if (need_parens)
        fprintf(f, "(");
    if (n->kind == ND_MEMBER) {
        serialize_addr_shell(f, vm, ctx, n->lhs, base, tail, node_prec);
        if (n->member && n->member->name)
            fprintf(f, ".%.*s", n->member->name->len, n->member->name->loc);
        else if (!n->member)
            // Same unresolved-member placeholder ND_MEMBER itself emits.
            fprintf(f, "./* unknown */");
        // else: an anonymous struct/union member -- transparent in C, no
        // spelling of its own (see ND_MEMBER's case for both).
    } else { // ND_DEREF -- the only other kind addr_comma_base() accepts
        fprintf(f, "*");
        serialize_addr_shell(f, vm, ctx, n->lhs, base, tail, node_prec);
    }
    if (need_parens)
        fprintf(f, ")");
}

// #1102: serialize `&(A, B, x)` as `(A, B, &x)` -- see the ND_ADDR case in
// serialize_expr(). Flattens nested comma levels into one parenthesized
// sequence (the comma operator is associative, so grouping is free --
// balanced_comma() in parse_init.c relies on the same fact), binds the &
// to the rightmost operand, and leaves every other operand to the ordinary
// serializer. `end` is the pre-computed rightmost operand -- compared by
// pointer identity so the &, and only the &, lands there: an initializer's
// balanced-comma tree terminates on *both* sides, and spelling `&` at the
// first non-comma child would bind it to a mid-chain assignment instead.
// #1102 followup: `wrap` is the addressed expression *above* the chain's
// tail -- a MEMBER/DEREF shell such as `.x` from `&((struct P){30, 12}).x`
// -- spelled by serialize_addr_shell() with the chain itself elided. The
// caller emits the enclosing parentheses unconditionally: a bare comma
// chain must never leak into a context that saw `&(...)`'s precedence
// (e.g. `p = &(cl)` would otherwise re-parse as `(p = a), b`).
// #1289 follow-up (not this ticket's scope, not reachable from either of
// its repros): the `" , "` at the bottom of this function is printed
// unconditionally, the same shape host_owned_zero_init_classify() used to
// get wrong before #1289 -- if comma->lhs or comma->rhs ever resolves to a
// noop (ND_NULL_EXPR), this would print an empty operand too. The
// is_noop_expr guards at the recursive call above and at this function's
// own call site only cover comma->lhs's *immediate* children, not
// comma->rhs after it flattens through another ND_COMMA. Left as-is here;
// track any real repro as its own ticket rather than widening #1289.
static void serialize_addr_comma(FILE *f, VirtualMachine *vm,
                                 SerializeContext *ctx, Node *comma, Node *end,
                                 Node *wrap) {
    // Left side: flatten nested comma chains into the same sequence. The
    // tail can only sit on the right spine, so lhs never gets the &.
    if (comma->lhs && comma->lhs->kind == ND_COMMA &&
        !is_noop_expr(comma->lhs->lhs) && !is_noop_expr(comma->lhs->rhs))
        serialize_addr_comma(f, vm, ctx, comma->lhs, NULL, NULL);
    else
        // Argument-separator precedence (2), same guard ND_FUNCALL's
        // argument loop uses (#1042(b)) -- keeps a top-level `=` or `,`
        // inside the chain from leaking.
        serialize_expr(f, vm, ctx, comma->lhs, 2);
    fprintf(f, " , ");
    if (comma->rhs == end) {
        fprintf(f, "&");
        serialize_addr_shell(f, vm, ctx, wrap, comma, end,
                             get_precedence(ND_ADDR) + 1);
    } else if (comma->rhs && comma->rhs->kind == ND_COMMA) {
        serialize_addr_comma(f, vm, ctx, comma->rhs, end, wrap);
    } else {
        // A flattened left branch's own last element: plain spelling.
        serialize_expr(f, vm, ctx, comma->rhs, 2);
    }
}

// #1102: spell the synthesized byte-offset cast-back -- the leading `(T *)`
// of `((T *)((char *)ptr + off))` from #918's pointer-arithmetic spelling --
// with any element/pointee qualifier stripped. A const-qualified element
// (`const int arr[3]`) makes usual_arith_conv's decay cast and this
// cast-back print `(const int *)`, and clang rejects ANY store through a
// statically-qualified lvalue ("read-only variable is not assignable") even
// though every explicit cast in the chain legally strips the qualifier and
// the underlying object is genuinely mutable -- an aggregate initializer's
// assignment sequence, whose declaration hoist_mutable_type() has already
// de-qualified (#1029/#1102). Reads are unaffected by the missing
// qualifier, so strip whenever present; anything unqualified keeps its
// exact previous spelling. Shallow copy with origin severed so
// find_typedef_name_exact() cannot re-spell a qualified typedef alias
// (same trick serialize_atomic_addr()/#1101 uses).
static void serialize_offset_cast_type(FILE *f, VirtualMachine *vm,
                                       SerializeContext *ctx, Type *ty) {
    bool qualified =
        ty && ty->base &&
        (ty->base->is_const || ty->base->is_volatile || ty->base->is_atomic);
    fprintf(f, "(");
    if (!qualified) {
        if (ty && (ty->kind == TY_ARRAY || ty->kind == TY_VLA)) {
            serialize_type(f, ctx, ty->base);
            fprintf(f, " *");
        } else {
            serialize_type(f, ctx, ty);
        }
    } else {
        // Strip the qualifiers onto a fresh unqualified pointee, then
        // re-spell a plain pointer TO it through serialize_type() rather
        // than printing `pointee *` by hand -- the pointee may itself be
        // a pointer (an array of const-qualified function pointers, e.g.
        // `static int (*const table[])(void)`), where naive concatenation
        // produces the unparseable `(int (*)(void) *)`.
        Type pointee        = *ty->base;
        pointee.is_const    = false;
        pointee.is_volatile = false;
        pointee.is_atomic   = false;
        pointee.origin      = NULL;
        Type ptr            = {0};
        ptr.kind            = TY_PTR;
        ptr.size            = 8;
        ptr.align           = 8;
        ptr.is_unsigned     = true;
        ptr.base            = &pointee;
        serialize_type(f, ctx, &ptr);
    }
    fprintf(f, ")");
}

// Print indentation
void print_indent_level(FILE *f, int indent) {
    for (int i = 0; i < indent; i++)
        fprintf(f, "    ");
}

// ---------- #1123: multi-word _BitInt(N>128) expression lowering ----------
//
// A _BitInt wider than 128 bits has no host scalar container (serialize_type
// spells 65..128 as __int128, but nothing exists past that) -- it lowers to
// an emitted `__cccc_bi<K>` struct (K = size/8 64-bit words,
// serialize_wide_bitint_preamble, src/serialize_shims.c) and every operation
// on it routes through src/stdlib/wide_bitint.c's runtime, extracted
// verbatim into this TU by the same preamble. This section is the
// expression-level half: it whitelists exactly the node kinds
// src/codegen_expr.c's own wide-_BitInt arms implement (that switch is the
// VM's authoritative enumeration -- it hard-errors on anything it doesn't
// handle, so mirroring it here is provably complete) and prints nothing
// (returning false) for every other node, so the ordinary switch below is
// unaffected for the overwhelming majority of expressions.

// True iff `ty` needs the __cccc_biK lowering -- i.e. has no host scalar/
// __int128 container. size<=16 (bit_width<=128) already round-trips through
// an ordinary integer or __int128 and every C operator works on it directly;
// only size>16 needs anything in this section.
static bool ty_is_super_wide_bitint(Type *ty) {
    return ty && ty->kind == TY_BITINT && ty->size > 16;
}

// #1123: wraps a wide-_BitInt-typed `node` with __cccc_bitint_nonzero()
// wherever C expects a scalar truth value (if/while/for conditions, `!`,
// `&&`/`||` operands, a `?:` condition) -- a raw __cccc_biK struct value
// cannot appear directly in any of those. Prints nothing and returns false
// when `node` isn't wide (the overwhelming common case); callers fall back
// to their own ordinary serialize_expr() call unchanged.
bool serialize_wide_bitint_truth(FILE *f, VirtualMachine *vm,
                                 SerializeContext *ctx, Node *node) {
    if (!node || !ty_is_super_wide_bitint(node->ty))
        return false;
    int words = node->ty->size / 8;
    int seq   = ctx->wide_bitint_seq++;
    fprintf(f, "({ __cccc_bi%d __cccc_bi_t%d = (", words, seq);
    serialize_expr(f, vm, ctx, node, 0);
    fprintf(f, "); __cccc_bitint_nonzero(__cccc_bi_t%d.w, %d); })", seq, words);
    return true;
}

// The int/narrow-BitInt/__int128 -> wide cast arm and the wide -> same range
// cast arm both need a source value bridged into a 2-word buffer at a known
// 128-bit width, since __cccc_bitint_extend (like every helper here) only
// ever reads a words/width-described buffer, never a scalar. `sign_spell` is
// "" for an unsigned source (zero-extend to 128 bits) or "unsigned " for a
// signed one -- wait, backwards: C's own (T) conversion sign/zero-extends
// per the *source*'s signedness when narrowing is not in play, so casting to
// `(is_signed ? "" : "unsigned ") __int128` performs the correct 128-bit
// widening before this function ever runs; see its call sites.
static void serialize_wide_bitint_int_bridge(FILE *f, VirtualMachine *vm,
                                             SerializeContext *ctx,
                                             Node *src_node, bool src_signed,
                                             int seq) {
    fprintf(f, "unsigned __int128 __cccc_bi_s%d = (unsigned __int128)(%s)(",
            seq, src_signed ? "__int128" : "unsigned __int128");
    serialize_expr(f, vm, ctx, src_node, 0);
    fprintf(f,
            "); uint64_t __cccc_bi_sw%d[2] = { "
            "(unsigned long long)__cccc_bi_s%d, "
            "(unsigned long long)(__cccc_bi_s%d >> 64) };",
            seq, seq, seq);
}

// #1123: the ND_CAST arm of the whitelist below. Handles all four
// direction combinations a cast can take across the size>16 boundary; every
// other cast (both sides <=128 bits, or neither side TY_BITINT at all) never
// reaches here -- see serialize_wide_bitint_expr's own gate.
static bool serialize_wide_bitint_cast(FILE *f, VirtualMachine *vm,
                                       SerializeContext *ctx, Node *node,
                                       int seq) {
    Type *dst      = node->ty;
    Type *src      = node->lhs->ty;
    bool  dst_wide = ty_is_super_wide_bitint(dst);
    bool  src_wide = ty_is_super_wide_bitint(src);

    if (src_wide && dst_wide) {
        // wide -> wide: a direct multi-word extend/truncate, no bridging.
        int words_src = src->size / 8, words_dst = dst->size / 8;
        fprintf(f, "({ __cccc_bi%d __cccc_bi_a%d = (", words_src, seq);
        serialize_expr(f, vm, ctx, node->lhs, 0);
        fprintf(f,
                "); __cccc_bi%d __cccc_bi_r%d; __cccc_bitint_extend("
                "__cccc_bi_r%d.w, __cccc_bi_a%d.w, %d, %d, %d, %d, %d); "
                "__cccc_bi_r%d; })",
                words_dst, seq, seq, seq, words_src, src->bit_width, words_dst,
                dst->bit_width, !src->is_unsigned ? 1 : 0, seq);
        return true;
    }

    if (dst_wide) {
        int words = dst->size / 8;
        if (is_flonum(src)) {
            // float/double/long double -> wide: bounce through a raw bit
            // pattern (matches __cccc_bitint_from_double/to_double's own
            // long-long-not-double interface, chosen so this file's
            // interface doesn't need to change to add wide-_BitInt support,
            // #457 -- see src/stdlib/wide_bitint.c's own comment).
            fprintf(f,
                    "({ __cccc_bi%d __cccc_bi_r%d; __cccc_bitint_from_double("
                    "__cccc_bi_r%d.w, __cccc_double_to_bits((double)(",
                    words, seq, seq);
            serialize_expr(f, vm, ctx, node->lhs, 0);
            fprintf(f, ")), %d, %d, %d); __cccc_bi_r%d; })", words,
                    dst->bit_width, !dst->is_unsigned ? 1 : 0, seq);
            return true;
        }
        // int/narrow-BitInt/__int128 -> wide: bridge to 2 words at 128 bits
        // (sign/zero-extended per src's own signedness by the ordinary C
        // cast in the bridge), then extend to the real destination width.
        fprintf(f, "({ ");
        serialize_wide_bitint_int_bridge(f, vm, ctx, node->lhs,
                                         !src->is_unsigned, seq);
        fprintf(f,
                " __cccc_bi%d __cccc_bi_r%d; __cccc_bitint_extend("
                "__cccc_bi_r%d.w, __cccc_bi_sw%d, 2, 128, %d, %d, %d); "
                "__cccc_bi_r%d; })",
                words, seq, seq, seq, words, dst->bit_width,
                !src->is_unsigned ? 1 : 0, seq);
        return true;
    }

    // src_wide, !dst_wide: wide -> int/float/_Bool/__int128-or-narrower.
    int words = src->size / 8;
    if (dst->kind == TY_BOOL) {
        fprintf(f, "({ __cccc_bi%d __cccc_bi_a%d = (", words, seq);
        serialize_expr(f, vm, ctx, node->lhs, 0);
        fprintf(f, "); __cccc_bitint_nonzero(__cccc_bi_a%d.w, %d); })", seq,
                words);
        return true;
    }
    if (is_flonum(dst)) {
        fprintf(f, "((");
        serialize_type(f, ctx, dst);
        fprintf(f, ")({ __cccc_bi%d __cccc_bi_a%d = (", words, seq);
        serialize_expr(f, vm, ctx, node->lhs, 0);
        fprintf(f,
                "); __cccc_bits_to_double(__cccc_bitint_to_double("
                "__cccc_bi_a%d.w, %d, %d, %d)); }))",
                seq, words, src->bit_width, !src->is_unsigned ? 1 : 0);
        return true;
    }
    // Any other destination (int/narrow-BitInt/__int128/pointer): extend
    // the source down to its own low 2 words (128 bits) -- correct for any
    // dst width, since a narrowing conversion only ever keeps the low bits
    // and a plain C (T) cast on the reassembled 128-bit value truncates the
    // rest exactly like an ordinary narrowing conversion would. The dst-side
    // #1124 mask wrapper (serialize_expr(), bitint_needs_mask/
    // bitint_op_needs_mask -- ND_CAST is in that set) still runs around this
    // whole call for a dst bit_width that isn't a multiple of 8, since that
    // logic lives in the public serialize_expr() entry point, not here.
    fprintf(f, "((");
    serialize_type(f, ctx, dst);
    fprintf(f, ")({ __cccc_bi%d __cccc_bi_a%d = (", words, seq);
    serialize_expr(f, vm, ctx, node->lhs, 0);
    fprintf(f,
            "); uint64_t __cccc_bi_e%d[2]; __cccc_bitint_extend("
            "__cccc_bi_e%d, __cccc_bi_a%d.w, %d, %d, 2, 128, %d); "
            "((unsigned __int128)__cccc_bi_e%d[1] << 64) | __cccc_bi_e%d[0]; "
            "}))",
            seq, seq, seq, words, src->bit_width, !src->is_unsigned ? 1 : 0,
            seq, seq);
    return true;
}

// #1123: the value-producing whitelist itself. Mirrors src/codegen_expr.c's
// own wide-_BitInt arms one-for-one; see that file's ND_* switch (codegen_
// expr.c:1162 hard-errors on anything not implemented there, making this
// whitelist provably exhaustive). Prints nothing and returns false when
// `node` doesn't touch a size>16 _BitInt at all -- serialize_expr_raw falls
// through to its ordinary switch unchanged.
//
// Every arm stages its operand(s) into a __cccc_biK temporary via a GNU
// statement expression rather than taking their address in place --
// mandatory, not stylistic: an array member (`.w`) of a struct *rvalue*
// (the result of a nested lowering, a function call, ...) does not decay to
// a usable pointer the way an lvalue's does. A nested wide-_BitInt
// sub-expression needs no special handling: the plain serialize_expr() calls
// below re-enter serialize_expr_raw for that sub-node, which re-enters this
// same dispatcher.
static bool serialize_wide_bitint_expr(FILE *f, VirtualMachine *vm,
                                       SerializeContext *ctx, Node *node) {
    Type *ty           = node->ty;
    bool  result_wide  = ty_is_super_wide_bitint(ty);
    Type *operand_ty   = node->lhs ? node->lhs->ty : NULL;
    bool  operand_wide = ty_is_super_wide_bitint(operand_ty);

    switch (node->kind) {
        case ND_NUM:
            if (!result_wide || !node->wide_digits)
                return false;
            break;
        case ND_NEG:
        case ND_BITNOT:
        case ND_ADD:
        case ND_SUB:
        case ND_MUL:
        case ND_BITAND:
        case ND_BITOR:
        case ND_BITXOR:
        case ND_DIV:
        case ND_MOD:
        case ND_EQ:
        case ND_NE:
        case ND_LT:
        case ND_LE:
            if (!operand_wide)
                return false;
            break;
        case ND_SHL:
        case ND_SHR:
            if (!result_wide)
                return false;
            break;
        case ND_CAST:
            if (!operand_wide && !result_wide)
                return false;
            break;
        default:
            return false;
    }

    int seq = ctx->wide_bitint_seq++;

    switch (node->kind) {
        case ND_NUM: {
            int words = ty->size / 8;
            fprintf(f,
                    "({ __cccc_bi%d __cccc_bi_r%d; __cccc_bitint_from_str("
                    "__cccc_bi_r%d.w, \"%s\", %d, %d, %d); __cccc_bi_r%d; })",
                    words, seq, seq, node->wide_digits, node->wide_base, words,
                    ty->bit_width, seq);
            return true;
        }
        case ND_NEG:
        case ND_BITNOT: {
            int         words  = operand_ty->size / 8;
            const char *helper = node->kind == ND_NEG ? "__cccc_bitint_neg"
                                                      : "__cccc_bitint_not";
            fprintf(f, "({ __cccc_bi%d __cccc_bi_a%d = (", words, seq);
            serialize_expr(f, vm, ctx, node->lhs, 0);
            fprintf(f,
                    "), __cccc_bi_r%d; %s(__cccc_bi_r%d.w, __cccc_bi_a%d.w, "
                    "%d, %d); __cccc_bi_r%d; })",
                    seq, helper, seq, seq, words, operand_ty->bit_width, seq);
            return true;
        }
        case ND_ADD:
        case ND_SUB:
        case ND_MUL:
        case ND_BITAND:
        case ND_BITOR:
        case ND_BITXOR:
        case ND_DIV:
        case ND_MOD: {
            int         words   = operand_ty->size / 8;
            bool        signedv = !operand_ty->is_unsigned;
            const char *helper  = NULL;
            switch (node->kind) {
                case ND_ADD:
                    helper = "__cccc_bitint_add";
                    break;
                case ND_SUB:
                    helper = "__cccc_bitint_sub";
                    break;
                case ND_MUL:
                    helper = "__cccc_bitint_mul";
                    break;
                case ND_BITAND:
                    helper = "__cccc_bitint_and";
                    break;
                case ND_BITOR:
                    helper = "__cccc_bitint_or";
                    break;
                case ND_BITXOR:
                    helper = "__cccc_bitint_xor";
                    break;
                case ND_DIV:
                    helper =
                        signedv ? "__cccc_bitint_sdiv" : "__cccc_bitint_udiv";
                    break;
                case ND_MOD:
                    helper =
                        signedv ? "__cccc_bitint_smod" : "__cccc_bitint_umod";
                    break;
                default:
                    break;
            }
            fprintf(f, "({ __cccc_bi%d __cccc_bi_a%d = (", words, seq);
            serialize_expr(f, vm, ctx, node->lhs, 0);
            fprintf(f, "), __cccc_bi_b%d = (", seq);
            serialize_expr(f, vm, ctx, node->rhs, 0);
            fprintf(f,
                    "), __cccc_bi_r%d; %s(__cccc_bi_r%d.w, __cccc_bi_a%d.w, "
                    "__cccc_bi_b%d.w, %d, %d); __cccc_bi_r%d; })",
                    seq, helper, seq, seq, seq, words, operand_ty->bit_width,
                    seq);
            return true;
        }
        case ND_SHL:
        case ND_SHR: {
            int         words   = ty->size / 8;
            bool        signedv = !ty->is_unsigned;
            const char *helper =
                node->kind == ND_SHL
                    ? "__cccc_bitint_shl"
                    : (signedv ? "__cccc_bitint_sshr" : "__cccc_bitint_ushr");
            fprintf(f, "({ __cccc_bi%d __cccc_bi_a%d = (", words, seq);
            serialize_expr(f, vm, ctx, node->lhs, 0);
            fprintf(f,
                    "), __cccc_bi_r%d; %s(__cccc_bi_r%d.w, __cccc_bi_a%d.w, "
                    "(long long)(",
                    seq, helper, seq, seq);
            serialize_expr(f, vm, ctx, node->rhs, 0);
            fprintf(f, "), %d, %d); __cccc_bi_r%d; })", words, ty->bit_width,
                    seq);
            return true;
        }
        case ND_EQ:
        case ND_NE:
        case ND_LT:
        case ND_LE: {
            int         words   = operand_ty->size / 8;
            bool        signedv = !operand_ty->is_unsigned;
            const char *cmpop   = node->kind == ND_EQ   ? "=="
                                  : node->kind == ND_NE ? "!="
                                  : node->kind == ND_LT ? "<"
                                                        : "<=";
            fprintf(f, "({ __cccc_bi%d __cccc_bi_a%d = (", words, seq);
            serialize_expr(f, vm, ctx, node->lhs, 0);
            fprintf(f, "), __cccc_bi_b%d = (", seq);
            serialize_expr(f, vm, ctx, node->rhs, 0);
            fprintf(f,
                    "); __cccc_bitint_cmp(__cccc_bi_a%d.w, __cccc_bi_b%d.w, "
                    "%d, %d, %d) %s 0; })",
                    seq, seq, words, operand_ty->bit_width, signedv ? 1 : 0,
                    cmpop);
            return true;
        }
        case ND_CAST:
            return serialize_wide_bitint_cast(f, vm, ctx, node, seq);
        default:
            return false;
    }
}

// Serialize an expression
static void serialize_expr_raw(FILE *f, VirtualMachine *vm,
                               SerializeContext *ctx, Node *node,
                               int parent_prec) {
    (void)vm; // May be used later

    if (!node) {
        fprintf(f, "/* NULL */");
        return;
    }

    // #1018: a va_start/va_arg/va_copy/va_end node (Node.va_form, src/
    // cccc.h) always prints as the real host <stdarg.h> call instead of
    // walking its own VM-ABI impl subtree -- checked before node_prec is
    // computed from node->kind, since that kind (whatever the impl
    // expression's own root operator happens to be -- ND_STMT_EXPR,
    // ND_COND, ND_ASSIGN, ...) has nothing to do with this call's own
    // precedence (highest, like any other function call). No on-demand
    // #include is needed the way #1050/#1057 needed one: unlike a
    // reflection-API builder resolving memcpy/size_t with no source-level
    // #include at all, va_start/va_arg/va_copy/va_end only exist as macros
    // -- reaching this node at all requires the user's own
    // `#include <stdarg.h>`/`"stdarg.h"` to already be in the TU (for the
    // macro expansion to have happened), and that line is auto-captured
    // and replayed like any ordinary header; include/stdarg.h's own
    // `#include_next` hand-off (the #1040 follow-on) already resolves it
    // to the real host header whenever a genuine host compiler (not
    // CCCC's own preprocessor) is the one reading it.
    if (node->va_form != VA_NONE) {
        switch (node->va_form) {
            case VA_START:
                fprintf(f, "va_start(");
                serialize_expr(f, vm, ctx, node->va_ap, 2);
                fprintf(f, ", ");
                serialize_expr(f, vm, ctx, node->va_last, 2);
                fprintf(f, ")");
                return;
            case VA_ARG:
                fprintf(f, "va_arg(");
                serialize_expr(f, vm, ctx, node->va_ap, 2);
                fprintf(f, ", ");
                serialize_type(f, ctx, va_arg_promoted_type(node->va_type));
                fprintf(f, ")");
                return;
            case VA_COPY:
                fprintf(f, "va_copy(");
                serialize_expr(f, vm, ctx, node->va_ap, 2);
                fprintf(f, ", ");
                serialize_expr(f, vm, ctx, node->va_src, 2);
                fprintf(f, ")");
                return;
            case VA_END:
                fprintf(f, "va_end(");
                serialize_expr(f, vm, ctx, node->va_ap, 2);
                fprintf(f, ")");
                return;
            default:
                break;
        }
    }

    // #1123: multi-word _BitInt(N>128) lowering. `!x`/`x && y`/`x || y` need
    // their own hook here (not serialize_wide_bitint_expr's whitelist below):
    // the result is always a plain int, so there's no single "does this node
    // touch a wide type" test that works for a binary op whose two operands
    // can be wide independently of one another. A statement expression is
    // always a primary expression (highest precedence), so none of these
    // three need node_prec/need_parens below.
    if (node->kind == ND_NOT && node->lhs &&
        ty_is_super_wide_bitint(node->lhs->ty)) {
        fprintf(f, "(!");
        serialize_wide_bitint_truth(f, vm, ctx, node->lhs);
        fprintf(f, ")");
        return;
    }
    if ((node->kind == ND_LOGAND || node->kind == ND_LOGOR) &&
        ((node->lhs && ty_is_super_wide_bitint(node->lhs->ty)) ||
         (node->rhs && ty_is_super_wide_bitint(node->rhs->ty)))) {
        fprintf(f, "(");
        if (!serialize_wide_bitint_truth(f, vm, ctx, node->lhs))
            serialize_expr(f, vm, ctx, node->lhs, 4);
        fprintf(f, " %s ", node->kind == ND_LOGAND ? "&&" : "||");
        if (!serialize_wide_bitint_truth(f, vm, ctx, node->rhs))
            serialize_expr(f, vm, ctx, node->rhs, 4);
        fprintf(f, ")");
        return;
    }
    // The value-producing whitelist (arithmetic/bitwise/compare/cast/wb
    // literal) -- see serialize_wide_bitint_expr's own comment.
    if (serialize_wide_bitint_expr(f, vm, ctx, node))
        return;

    int  node_prec   = get_precedence(node->kind);
    bool need_parens = (node_prec < parent_prec);

    if (need_parens)
        fprintf(f, "(");

    switch (node->kind) {
        case ND_NUM:
            if (node->ty && is_decimal(node->ty)) {
                // #402: node->fval/val are never populated for a decimal
                // literal (see tokenize.c) -- dec_digits plus the
                // width-appropriate suffix is the only way to round-trip it
                // back to valid C source.
                const char *suffix = dec_width_code(node->ty) == 0   ? "df"
                                     : dec_width_code(node->ty) == 1 ? "dd"
                                                                     : "dl";
                fprintf(f, "%s%s", node->dec_digits ? node->dec_digits : "0",
                        suffix);
            } else if (node->ty && is_flonum(node->ty)) {
                // #1038: this used to funnel every flonum type -- TY_FLOAT,
                // TY_DOUBLE and TY_LDOUBLE alike -- through a single unsuffixed
                // `%Lg` (default 6 significant digits), losing both precision
                // (e.g. DBL_MAX round-tripped as the text "1.79769e+308") and,
                // for TY_FLOAT/TY_LDOUBLE, the type entirely (a `1.0f` or
                // `1.0L` literal re-emitted as a plain double constant).
                // node->fval is always long double; TY_DOUBLE's %g conversion
                // needs an explicit (double) cast -- passing a long double to a
                // non-L conversion is undefined varargs behavior, harmless on
                // platforms where long double == double (aarch64) but wrong on
                // x86_64. Mirrors serialize_init_bytes's TY_FLOAT/TY_DOUBLE
                // arms (this is the ND_NUM/expression-literal counterpart of
                // that global-initializer path).
                if (node->ty->kind == TY_FLOAT) {
                    if (!serialize_flonum_special(f, node->fval, "f")) {
                        char buf[64];
                        format_float_literal(buf, sizeof buf,
                                             (double)node->fval);
                        fprintf(f, "%sf", buf);
                    }
                } else if (node->ty->kind == TY_LDOUBLE) {
                    // Builtin family name suffix ("l") and the literal suffix
                    // ("L") are cased differently --
                    // __builtin_infl/__builtin_nanl vs. the `L` token suffix --
                    // don't conflate them.
                    if (!serialize_flonum_special(f, node->fval, "l")) {
                        char buf[64];
                        format_ldouble_literal(buf, sizeof buf, node->fval);
                        fprintf(f, "%sL", buf);
                    }
                } else { // TY_DOUBLE
                    // #1058: same "force a decimal point" fixup
                    // format_float_literal/format_ldouble_literal already apply
                    // for TY_FLOAT/TY_LDOUBLE -- this arm used to print a bare
                    // %.17g with no fixup at all, so an integral value like
                    // 55.0 serialized as "55", read back by a real host
                    // compiler as an *integer* literal. Harmless under an
                    // enclosing (double) cast, a wrong answer wherever the
                    // literal's own text is what supplies its type (e.g. a
                    // double vararg passed positionally, #1018).
                    if (!serialize_flonum_special(f, node->fval, "")) {
                        char buf[64];
                        int  n = snprintf(buf, sizeof buf, "%.17g",
                                          (double)node->fval);
                        if (n > 0 && (size_t)n < sizeof buf &&
                            !strpbrk(buf, ".eEnN"))
                            snprintf(buf + n, sizeof(buf) - (size_t)n, ".0");
                        fprintf(f, "%s", buf);
                    }
                }
            } else if (node->layout_ty &&
                       serialize_layout_const(f, ctx, node->layout_ty,
                                              node->layout_is_align)) {
                // #1031: this ND_NUM was folded from sizeof/_Alignof of a
                // type whose own definition is suppressed elsewhere in
                // this TU (a from_include struct/union, or something that
                // transitively contains one -- see
                // type_layout_is_host_owned()) in favor of the replayed
                // #include's real host layout. The folded literal here
                // (CCCC's own, possibly smaller, projection) would go
                // stale the moment that suppression takes effect --
                // test_sys_mount_statfs.c is the confirmed case: a
                // `malloc(sizeof(struct statfs) + tail)` sized against the
                // guest's ~56-byte projection hands the real, much larger
                // host `statfs()` an undersized buffer. serialize_layout_
                // const() re-emits the operator textually instead, so the
                // *host's* sizeof/_Alignof is what the emitted C actually
                // evaluates -- primary-precedence, so no parenthesization
                // is needed at any parent_prec. Its own internal
                // type_has_printable_name() check is the fallback guard: an
                // anonymous from_include aggregate with no tag/typedef
                // would otherwise force serialize_type() to print a
                // re-derived body right here, reinstating CCCC's own
                // projection -- strictly worse than the literal this is
                // trying to fix, so that case falls through to the
                // ordinary folded-literal path below instead (nothing was
                // printed; serialize_layout_const() returned false).
                //
                // #1095 closed three more of this same residual: array
                // dimensions, case labels, and enum values now share this
                // exact helper (see their own call sites). Bitfield widths,
                // _Static_assert, and a global initializer's byte image
                // remain -- see man/NATIVE.md's own entry for why those
                // three are not merely deferred.
            } else if (node->ty && node->ty->kind == TY_BITINT &&
                       node->ty->bit_width > 64 && node->wide_digits) {
                // #1121: a wb/uwb literal beyond 64 bits carries its full
                // precision out-of-band in wide_digits/wide_base (cccc.h) --
                // node->val alone is truncated to 64 bits by the tokenizer
                // (tokenize.c). Only the VM path previously consumed
                // wide_digits (codegen_expr.c); this arm is the -m/-c=native
                // counterpart. bit_width > 128 already can't reach here: the
                // enclosing type would have hard-errored out of
                // serialize_type() (case TY_BITINT above) first.
                unsigned __int128 v =
                    decode_wide_digits(node->wide_digits, node->wide_base);
                fprintf(f,
                        "((%s%s)(((unsigned __int128)0x%llxULL << 64) | "
                        "0x%llxULL))",
                        node->ty->is_unsigned ? "unsigned " : "", "__int128",
                        (unsigned long long)(uint64_t)(v >> 64),
                        (unsigned long long)(uint64_t)v);
            } else if (node->ty && is_integer(node->ty)) {
                // #1031: the old unconditional `%lld` of the raw bit pattern
                // had two failures for a folded integer literal. (1) An
                // unsigned 64-bit value >= 2^63 (e.g. 18446744073709551615ULL,
                // ULLONG_MAX) prints its reinterpreted-as-signed text with no
                // `U`/`ULL` suffix, so a real host compiler reads it back as a
                // negative `int` -- a later implicit conversion (e.g. to
                // double) then produces the wrong value. Confirmed via
                // test_unsigned_int_to_float_conversion.c. (2) INT64_MIN
                // itself (-9223372036854775808) isn't a valid signed literal
                // token at all -- the host warns "integer literal is too
                // large to be represented in a signed integer type,
                // interpreting as unsigned" and silently reinterprets it.
                // Emitting a width/sign-accurate suffix always (not only when
                // the value would otherwise misparse) also matters for a
                // partially folded expression: `sizeof(x) - 1` folding to
                // `8ULL` vs. plain `8` changes a later subtraction from
                // unsigned-huge to signed -2.
                //
                // A negative node->val can only reach here via constant
                // folding -- a literal `-1` as written in source parses as
                // ND_NEG(ND_NUM(1)), not a single ND_NUM with val == -1 (see
                // the #1031-adjacent note near line 1677 below).
                if (node->ty->is_unsigned) {
                    uint64_t uv = (uint64_t)node->val;
                    // Mask to the type's true width first -- a narrow
                    // unsigned value may have arrived here sign-extended
                    // (serialize_init_bytes does the same narrow-type
                    // sign-extension at its own ND_NUM-adjacent site), and
                    // 1ULL << 64 is UB so the size==8 case must skip the mask
                    // entirely.
                    if (node->ty->size < 8)
                        uv &= (1ULL << (node->ty->size * 8)) - 1;
                    fprintf(f, "%llu%s", (unsigned long long)uv,
                            node->ty->size == 8 ? "ULL" : "U");
                } else if (node->val == INT64_MIN) {
                    // The only spelling a host compiler accepts for the most
                    // negative signed 64-bit value -- a bare
                    // "-9223372036854775808" token is parsed as unary minus
                    // applied to a positive literal one past LLONG_MAX.
                    fprintf(f, "(-9223372036854775807LL - 1)");
                } else {
                    fprintf(f, "%lld%s", (long long)node->val,
                            node->ty->size == 8 ? "LL" : "");
                }
            } else
                // A folded null-pointer constant (TY_PTR/TY_NULLPTR_T) or a
                // node with no type reaches here -- plain decimal text (e.g.
                // "0") is valid C in either context, no suffix needed.
                fprintf(f, "%lld", (long long)node->val);
            break;

        case ND_VAR:
            if (node->var) {
                // A dotted `.L..N` name means "anonymous global"
                // (new_anon_gvar, parse.c) -- shared by string literals, static
                // locals, and compound literals (#925). Only a genuine string
                // literal inlines as string text here; the other two are
                // renamed to a valid identifier and given a real definition by
                // rename_anon_globals() before this ever runs, so they hit the
                // plain `fprintf(f, "%s", ...)` fallback below like any other
                // named var.
                if (node->var->is_string_literal) {
                    // #918: use the global's actual array length, not NUL
                    // termination -- an anonymous string-literal global can
                    // legitimately contain embedded NULs (wide/multi-part
                    // literals, __func__ splicing, etc).
                    int len = (node->var->ty && node->var->ty->kind == TY_ARRAY)
                                  ? node->var->ty->array_len
                                  : (int)strlen(node->var->init_data);
                    serialize_string_n(f, node->var->init_data, len);
                } else if (serialize_block_capture_ref(f, ctx, node->var)) {
                    // #965: handled -- var is captured by the block literal
                    // currently being serialized.
                } else if (serialize_nested_upvar_ref(f, vm, ctx, node->var)) {
                    // #1074: handled -- var is an upvar of the nested function
                    // currently being serialized.
                } else if (node->var->is_block_var) {
                    // #965: a __block local's stack slot now holds a heap box
                    // pointer (see serialize_function's hoist loop) -- read
                    // through it so the expression's spelled type matches
                    // node->ty (the variable's declared type, not a pointer to
                    // it). Only reached here for a __block var referenced from
                    // *outside* any block capturing it (or from the same
                    // function that declared it); the captured case above
                    // already applies its own dereference.
                    fprintf(f, "(*%s)", node->var->name);
                } else if (node->var == vm->compiler.builtin_alloca) {
                    // #1024: declare_builtin_functions (parse_decl.c) names
                    // this Obj literally "alloca" so the VM's own symbol table
                    // resolves it -- but that Obj has no source token and no
                    // definition, so the #901 from_include prototype pass never
                    // declares a plain `alloca` for the host compiler to see
                    // ("call to undeclared library function 'alloca'"). Every
                    // native cc that matters (gcc/clang) supplies
                    // __builtin_alloca with no header at all; spell the call
                    // that way here instead of trying to synthesize a
                    // <alloca.h>/<stdlib.h> declaration.
                    fprintf(f, "__builtin_alloca");
                } else if (node->var == vm->compiler.builtin_strlen) {
                    // #1154: same rationale as builtin_alloca just above --
                    // declare_builtin_functions (parse_decl.c) forwards
                    // __builtin_strlen/__builtin_strcmp/__builtin_mem{set,
                    // cpy,move,cmp} to a private stub Obj literally named
                    // "strlen"/"memcpy"/etc (new_private_func_obj), kept out
                    // of global scope on purpose so it never conflicts with
                    // a user's own <string.h> declaration. That also means
                    // it is never on the `prog` list, so no declaration-
                    // emission pass (cc_serialize_program's prototype walk,
                    // serialize_synth_libc_includes, ...) can ever see it or
                    // declare it -- printing the plain libc name here would
                    // need the caller's own #include <string.h> in scope,
                    // unlike real GCC/clang, which recognise every one of
                    // these six spellings as a builtin needing no header at
                    // all. Spell the call that way instead.
                    fprintf(f, "__builtin_strlen");
                } else if (node->var == vm->compiler.builtin_strcmp) {
                    fprintf(f, "__builtin_strcmp");
                } else if (node->var == vm->compiler.builtin_memset) {
                    fprintf(f, "__builtin_memset");
                } else if (node->var == vm->compiler.builtin_memcpy) {
                    fprintf(f, "__builtin_memcpy");
                } else if (node->var == vm->compiler.builtin_memmove) {
                    fprintf(f, "__builtin_memmove");
                } else if (node->var == vm->compiler.builtin_memcmp) {
                    fprintf(f, "__builtin_memcmp");
                } else {
                    fprintf(f, "%s", node->var->name);
                }
            } else {
                fprintf(f, "/* unknown_var */");
            }
            break;

        case ND_ADD:
        case ND_SUB: {
            // #918: pointer arithmetic needs (char *)-based casts, not the
            // naively-printed operand types -- see the strip_casts()/
            // node_is_pointerish()/node_is_integerish() comment above.
            Node *lhs_inner = strip_casts(node->lhs);
            Node *rhs_inner = strip_casts(node->rhs);
            bool  lhs_ptr   = node_is_pointerish(lhs_inner);
            bool  rhs_ptr   = node_is_pointerish(rhs_inner);
            bool  lhs_int   = node_is_integerish(lhs_inner);
            bool  rhs_int   = node_is_integerish(rhs_inner);

            if (node->kind == ND_SUB && lhs_ptr && rhs_ptr) {
                // ptr - ptr: new_sub() already wraps this whole node in an
                // outer `(hi - lo) / elemsize` ND_DIV (untouched here) -- only
                // the pointer subtraction itself needs (char *) casts, so the
                // host compiler doesn't scale by its own idea of the element
                // size on top of that division.
                fprintf(f, "((char *)");
                serialize_expr(f, vm, ctx, node->lhs, 14);
                fprintf(f, " - (char *)");
                serialize_expr(f, vm, ctx, node->rhs, 14);
                fprintf(f, ")");
                break;
            }

            if (lhs_ptr && rhs_int) {
                // ptr +/- num: rhs is already the byte-scaled offset new_add()/
                // new_sub() computed (rhs *= sizeof(*ptr)) -- casting the
                // pointer to (char *) before applying it, then casting the
                // whole result back to node->ty, keeps the host from scaling
                // the offset a second time. Print rhs_inner (not node->rhs):
                // the outer cast usual_arith_conv() wrapped it in is a bogus
                // (pointer-typed) cast on an integer offset, and printing it
                // would produce the exact `ptr + (int *)offset` error this
                // fix exists to avoid.
                //
                // #928: node->ty can itself be an array type (e.g. a reflection
                // MakeSubscript() on an array-typed anon global) --
                // serialize_type would print `(int [3])`, and a cast to array
                // type is not valid C. Cast to pointer-to-element instead; the
                // ND_DEREF this node is wrapped in still reads the right value
                // through it. #964: node->ty can also be TY_VLA (`int[n]`),
                // same fix applies.
                // #1102: the cast-back drops pointee qualifiers -- see
                // serialize_offset_cast_type().
                serialize_offset_cast_type(f, vm, ctx, node->ty);
                fprintf(f, "((char *)");
                serialize_expr(f, vm, ctx, node->lhs, 14);
                fprintf(f, " %s ", get_binary_op_str(node->kind));
                serialize_expr(f, vm, ctx, rhs_inner, node_prec + 1);
                fprintf(f, ")");
                break;
            }

            if (node->kind == ND_ADD && rhs_ptr && lhs_int) {
                // num + ptr: new_add() canonicalizes this to ptr + num, but
                // set_checked_deref_bounds() builds ND_ADD via new_binary()
                // directly and does not canonicalize -- handle it defensively.
                // Print lhs_inner for the same reason as above.
                // #928: same array-cast fix as the ptr+num arm above; #964
                // extends it to TY_VLA (a cast to `int[n]` is equally invalid
                // C). #1102: qualifier-stripping cast-back, as above.
                serialize_offset_cast_type(f, vm, ctx, node->ty);
                fprintf(f, "((char *)");
                serialize_expr(f, vm, ctx, node->rhs, 14);
                fprintf(f, " + ");
                serialize_expr(f, vm, ctx, lhs_inner, node_prec + 1);
                fprintf(f, ")");
                break;
            }

            // Plain arithmetic (int+int, float+float, ...).
            serialize_expr(f, vm, ctx, node->lhs, node_prec);
            fprintf(f, " %s ", get_binary_op_str(node->kind));
            serialize_expr(f, vm, ctx, node->rhs, node_prec + 1);
            break;
        }

        case ND_MUL:
        case ND_DIV:
        case ND_MOD:
        case ND_BITAND:
        case ND_BITOR:
        case ND_BITXOR:
        case ND_SHL:
        case ND_SHR:
        case ND_EQ:
        case ND_NE:
        case ND_LT:
        case ND_LE:
        case ND_LOGAND:
        case ND_LOGOR:
        case ND_ASSIGN:
            serialize_expr(f, vm, ctx, node->lhs, node_prec);
            fprintf(f, " %s ", get_binary_op_str(node->kind));
            serialize_expr(f, vm, ctx, node->rhs, node_prec + 1);
            break;

        case ND_COMMA:
            // #1103: `T v = {0}` (or any all-zero initializer) on a
            // host-owned-layout local lowers to
            // ND_COMMA(ND_MEMZERO(v), <per-member zero stores...>) --
            // print the memzero and drop every store in the chain that's
            // provably redundant with it; see
            // serialize_host_owned_zero_init_chain()'s own comment above.
            if (node->lhs->kind == ND_MEMZERO && node->lhs->var &&
                type_layout_is_host_owned(ctx, node->lhs->var->ty, 0)) {
                serialize_expr(f, vm, ctx, node->lhs, node_prec);
                bool printed = true;
                serialize_host_owned_zero_init_chain(
                    f, vm, ctx, node->rhs, node->lhs->var, node_prec, &printed);
                break;
            }
            // Skip null sides — ND_NULL_EXPR , X is not valid C.
            if (is_noop_expr(node->lhs) && is_noop_expr(node->rhs))
                break;
            if (is_noop_expr(node->lhs)) {
                serialize_expr(f, vm, ctx, node->rhs, node_prec + 1);
            } else if (is_noop_expr(node->rhs)) {
                serialize_expr(f, vm, ctx, node->lhs, node_prec);
            } else {
                serialize_expr(f, vm, ctx, node->lhs, node_prec);
                fprintf(f, " , ");
                serialize_expr(f, vm, ctx, node->rhs, node_prec + 1);
            }
            break;

        case ND_NEG:
        case ND_NOT:
        case ND_BITNOT:
        case ND_ADDR:
        case ND_DEREF: {
            // #1102: & never distributes over a comma chain -- C's comma
            // operator yields an rvalue even when its last operand is an
            // lvalue, so the naive spelling of `&(struct P){30, 12}`
            // (a block-scope compound literal lowers to
            // ND_ADDR(ND_COMMA(memzero+assigns..., hidden temp var)))
            // came out as `&(memset(...), t.x = 30, t)` and clang
            // rejected it outright ("cannot take the address of an
            // rvalue"). Re-spell with the & binding to the chain's
            // addressable tail instead: `(memset(...), t.x = 30, &t)`.
            // Same evaluation order, same resulting pointer; the VM
            // never re-parses the text, but every host compiler does.
            // #1102 followup: the addressed expression can also carry a
            // postfix shell above the chain -- `&((struct P){30, 12}).x`
            // (parenthesized literals take the ordinary postfix path, so
            // `.x`/`->x` layer MEMBER (and DEREF) nodes over the comma) --
            // which used to emit the equally-invalid `&(..., t).x`. The &
            // binds inside the chain there too, covering the whole shell:
            // `(..., &t.x)`.
            Node *addr_base = node->kind == ND_ADDR && node->lhs
                                  ? addr_comma_base(node->lhs)
                                  : NULL;
            if (node->kind == ND_ADDR && addr_base &&
                !is_noop_expr(addr_base->lhs) &&
                !is_noop_expr(addr_base->rhs) &&
                addr_comma_tail_is_lvalue(addr_base)) {
                fprintf(f, "(");
                serialize_addr_comma(f, vm, ctx, addr_base,
                                     comma_chain_tail(addr_base), node->lhs);
                fprintf(f, ")");
                break;
            }
            fprintf(f, "%s", get_unary_op_str(node->kind));
            // #1102: two adjacent '-' tokens re-lex as pre-decrement, so
            // `-(-5)` -- ND_NEG over ND_NEG, typically from a macro like
            // `#define abs(x) ((x) < 0 ? -(x) : (x))` expanded on -5 --
            // must keep its inner operand parenthesized; the old flat
            // spelling emitted `--5` and every host compiler read it as
            // "--5" ("expression is not assignable"). The check drills
            // through ND_CAST chains: a widening cast serializes as
            // nothing (the suppression arm below), so the nested '-'
            // can hide behind one (_Generic-selected arms are exactly
            // this shape). Only this pairing glues: `!!x`, `~~x` and
            // `**p` are ordinary token sequences, and every other prefix
            // operator's own spelling ends in a non-'-' character, so
            // all other operands keep the exact spelling they had.
            if (node->kind == ND_NEG && node->lhs) {
                Node *op = node->lhs;
                while (op->kind == ND_CAST && op->lhs)
                    op = op->lhs;
                if (op->kind == ND_NEG) {
                    fprintf(f, "(");
                    serialize_expr(f, vm, ctx, node->lhs, 2);
                    fprintf(f, ")");
                    break;
                }
            }
            serialize_expr(f, vm, ctx, node->lhs, node_prec);
            break;
        }

        case ND_CAST: {
            // #1035: include/fenv.h's FE_DFL_ENV sentinel is spelled
            // `((const fenv_t *)-1)` -- a value src/stdlib/fenv.c's wrap_fe*()
            // functions recognize and substitute the real host FE_DFL_ENV for
            // under the VM, but under -c=native the literal -1 pointer reaches
            // the real host libm directly and gets dereferenced (SIGSEGV). The
            // generated C's own re-included <fenv.h> falls through to the
            // host's real header via #include_next (see that file's own
            // comment), so the bare identifier FE_DFL_ENV resolves in the
            // output -- emit it instead of the sentinel's numeric encoding.
            // The AST for a guest `FE_DFL_ENV` use is a *doubled* cast
            // (`(const fenv_t *)(const fenv_t *)-1`, once from the macro's own
            // cast and once from however the guest expression casts it again,
            // e.g. fesetenv's implicit-const-add), so peel through as many
            // nested casts-to-fenv_t-pointer as present down to the underlying
            // -1 constant, then swallow the whole outer node.
            if (node->ty && node->ty->kind == TY_PTR) {
                Node *inner = node;
                while (inner->kind == ND_CAST && inner->ty &&
                       inner->ty->kind == TY_PTR) {
                    TypeName *pointee = find_typedef_name(ctx, inner->ty->base);
                    if (!pointee || pointee->name_len != 6 ||
                        strncmp(pointee->name, "fenv_t", 6) != 0)
                        break;
                    inner = inner->lhs;
                }
                // A literal -1 parses as ND_NEG(ND_NUM(1)), not a single
                // ND_NUM with val == -1 -- unwrap one more level.
                bool matched = false;
                if (inner != node) {
                    Node *lit = inner;
                    if (lit->kind == ND_NEG && lit->lhs) {
                        // The literal `1` being negated arrives wrapped in its
                        // own (implicit int-promotion) ND_CAST(s) -- peel
                        // those too before checking for ND_NUM(1).
                        Node *n = lit->lhs;
                        while (n->kind == ND_CAST && n->lhs)
                            n = n->lhs;
                        matched = n->kind == ND_NUM && n->val == 1;
                    } else {
                        matched = lit->kind == ND_NUM && lit->val == -1;
                    }
                }
                if (matched) {
                    fprintf(f, "FE_DFL_ENV");
                    break;
                }
            }
            // Suppress widening integer casts — these are always implicit in C.
            // Only emit a cast if it crosses a type category or narrows/changes
            // signedness.
            Type *dst = node->ty;
            Type *src = node->lhs ? node->lhs->ty : NULL;
            // #1111: casting TO nullptr_t is not valid C23 syntax even where
            // assignment/conversion would be -- every implicit conversion the
            // type checker inserts when nullptr meets an assignment or
            // comparison (assignment conversion, null-pointer-constant
            // equalization) serializes as an explicit cast, and
            // serialize_type() above would spell its destination through its
            // own #999 scalar typedef-name lookup as the bundled <stddef.h>
            // name "nullptr_t". Real clang rejects every such cast outright
            // ("cannot cast an object of type 'int' to 'nullptr_t'"). The host
            // nullptr_t is typeof(nullptr) == void *, same size and
            // representation, so spelling the destination "(void *)" keeps
            // every assignment and comparison meaning-preserving while
            // compiling under any host. Scoped to cast destinations only:
            // declarations of nullptr_t objects keep their typedef name (valid
            // C23, resolves via the output's <stddef.h>).
            if (dst && dst->kind == TY_NULLPTR_T) {
                fprintf(f, "(void *)");
                serialize_expr(f, vm, ctx, node->lhs, node_prec);
                break;
            }
            // #1068: a bare "(dst_type)float_expr" cast is UB in the *host*
            // compiler for NaN/out-of-range values, same reason cccc_f64_to_i64
            // et al exist in src/internal.h for the VM's own F2I3/F2U3 opcodes
            // (#775/#780) -- the VM defines this conversion as saturating with
            // FE_INVALID raised, but a real host compiler is free to do
            // anything, and x86_64 clang/gcc both demonstrably do worse than
            // "anything": the common branchless double/float->uint64 lowering
            // spuriously raises FE_INVALID even for an in-range value (measured
            // directly, x86_64 only -- aarch64's FCVTZS/FCVTZU already saturate
            // correctly and never raise it, so this is a no-op there). Route
            // every real-floating -> non-floating cast through one of four
            // on-demand helpers (serialize_synth_f2i_helpers below) that are a
            // near-verbatim port of internal.h's own VM helpers, so native
            // output agrees with the VM by construction. Mirrors
            // codegen_expr.c's own opcode selection exactly: F2U3 (unsigned
            // helper) only for an unsigned 64-bit integer destination (matching
            // is_u64_int(), codegen_emit.c -- narrower unsigned destinations
            // are already correct via F2I3's signed saturation plus an ordinary
            // truncating narrow cast, which the outer "(dst_type)" below still
            // supplies), F2I3 (signed helper) for everything else including
            // TY_BOOL. A destination wider than 64 bits (an over-64-bit
            // _BitInt) takes a completely different VM codegen path (raw
            // bit-copy, not this numeric conversion) and is excluded here to
            // match. A TY_VECTOR destination is also excluded: that's #1019's
            // own scalar-broadcast ND_CAST marker just below
            // (usual_arith_conv's "vector op scalar" internal annotation, not a
            // genuine value conversion) -- without this exclusion a float
            // scalar broadcast into a vector op wrongly got routed through the
            // integer-saturating helper instead of staying a plain float,
            // corrupting the vector arithmetic itself.
            // #1208: `is_flonum()` is false for TY_COMPLEX, so without the
            // explicit exclusion a `float -> _Complex` cast (the real
            // operand of a complex construction, e.g. `1.5f + 2.5f*I`)
            // wrongly routed through the integer-saturating helper and
            // truncated 1.5f to 1. A real->complex cast is not a
            // real->integer conversion; the per-part real cast inside the
            // ND_COMPLEX construction arm handles the element type.
            bool f2i_native = src && dst && is_flonum(src) && !is_flonum(dst) &&
                              dst->kind != TY_VECTOR &&
                              dst->kind != TY_COMPLEX &&
                              !(dst->kind == TY_BITINT && dst->bit_width > 64);
            if (f2i_native) {
                bool u64_dst =
                    is_integer(dst) && dst->is_unsigned && dst->size == 8;
                bool        f32_src = src->kind == TY_FLOAT;
                const char *fn =
                    u64_dst ? (f32_src ? "__cccc_f2u64_f32" : "__cccc_f2u64")
                            : (f32_src ? "__cccc_f2i64_f32" : "__cccc_f2i64");
                fprintf(f, "(");
                serialize_type(f, ctx, node->ty);
                fprintf(f, ")%s(", fn);
                serialize_expr(f, vm, ctx, node->lhs, 2);
                fprintf(f, ")");
                break;
            }
            bool dst_int =
                dst && (dst->kind == TY_BOOL || dst->kind == TY_CHAR ||
                        dst->kind == TY_SHORT || dst->kind == TY_INT ||
                        dst->kind == TY_LONG);
            bool src_int =
                src && (src->kind == TY_BOOL || src->kind == TY_CHAR ||
                        src->kind == TY_SHORT || src->kind == TY_INT ||
                        src->kind == TY_LONG);
            static const int int_rank[] = {[TY_BOOL]  = 0,
                                           [TY_CHAR]  = 1,
                                           [TY_SHORT] = 2,
                                           [TY_INT]   = 3,
                                           [TY_LONG]  = 4};
            bool widening = dst_int && src_int &&
                            dst->is_unsigned == src->is_unsigned &&
                            int_rank[dst->kind] >= int_rank[src->kind];
            // #1019: a scalar operand of a `vector op scalar` binary op gets an
            // implicit ND_CAST(vector_ty, scalar) inserted by usual_arith_conv
            // (type.c) as its internal marker for "broadcast this scalar across
            // the vector's lanes" -- it is not source-level C. GCC/clang
            // perform that broadcast themselves inside the operator and reject
            // the same thing spelled as an explicit cast ("invalid conversion
            // between vector type and integer type of different size"). Emit
            // the bare scalar operand instead and let the host compiler's own
            // vector extension do the broadcast, exactly as real vector_size
            // source would. Only the scalar-source case is suppressed here -- a
            // vector-to-vector cast (same-type no-op, or a genuine bitcast
            // between differently-shaped vectors) still needs to print.
            bool scalar_splat =
                dst && dst->kind == TY_VECTOR && src && src->kind != TY_VECTOR;
            // #1300 (item 3): a cast whose destination is a pointer to an
            // *anonymous* struct/union (no tag, no typedef name) cannot be
            // spelled in C -- serialize_type() below re-synthesizes a fresh
            // `struct {...} *` body, and C's nominal typing makes that a
            // distinct type from the field's own identically-shaped anonymous
            // struct at every assignment site (`vm->compiler.call_patches`,
            // cccc.h, is exactly this: an anon-struct pointer assigned
            // realloc()'s result). Newer GCC promotes the resulting
            // -Wincompatible-pointer-types to a hard error. Every such cast is
            // an implicit conversion the type checker inserted, and the source
            // is always a pointer (a `void *` realloc/malloc return, in
            // practice) that converts implicitly with no cast at all -- emit
            // the operand bare. Guarded on src being a pointer so a genuine
            // integer->pointer reinterpret still prints its cast (however
            // unspellable the target); dropping it there is no worse than the
            // broken cast, but the pointer case is the only one that actually
            // occurs.
            bool anon_agg_ptr_cast = dst && dst->kind == TY_PTR && src &&
                                     src->kind == TY_PTR &&
                                     type_needs_anon_aggregate(ctx, dst);
            if (widening || scalar_splat || anon_agg_ptr_cast) {
                serialize_expr(f, vm, ctx, node->lhs, parent_prec);
            } else {
                fprintf(f, "(");
                // #1107: a pointer-typed scalar typedef reached only through
                // a DERIVED pointer-to-it (e.g. `pthread_t *`, the type
                // pthread_create()'s first arg coerces to) used to always
                // fully decompose here -- serialize_type()/serialize_type_
                // decl() recurse straight into ty->base with no alias
                // lookup at this level, so only a cast to the *scalar*
                // typedef itself (`(pthread_t)x`) ever spelled its alias;
                // a cast to `pthread_t *` printed the fully-decomposed
                // structural spelling ("void **") instead. Harmless when
                // the aliased and canonical spellings denote the same real
                // host type (e.g. macOS's own pthread_t is a real pointer),
                // but a hard "incompatible pointer type" error when they
                // don't -- glibc's pthread_t is `unsigned long int`, so the
                // decomposed "void **" cast rejected the real `pthread_t *`
                // parameter type from the host's own #include_next'd
                // <pthread.h> (#1022) outright. Scoped narrowly to this one
                // cast-expression site (not a general serialize_type_decl()
                // fix): a from_include pointer typedef's own real
                // declaration is always already visible here (the host's
                // #include_next runs before any guest declaration), so
                // there is no forward-reference hazard the way a plain
                // struct/user typedef's own declarator position would have
                // (verified against tests/test_minilua.c's own
                // `lua_CFunction`, whose typedef line is only emitted late
                // in ctx->typedefs' own pass -- an early cast to it here
                // would need a typedef this file hasn't printed yet).
                TypeName *ptr_alias =
                    dst && dst->kind == TY_PTR && dst->base
                        ? find_typedef_name_exact(ctx, dst->base)
                        : NULL;
                if (ptr_alias && ptr_alias->from_include)
                    fprintf(f, "%.*s *", ptr_alias->name_len, ptr_alias->name);
                else
                    serialize_type(f, ctx, node->ty);
                fprintf(f, ")");
                serialize_expr(f, vm, ctx, node->lhs, node_prec);
            }
            break;
        }

        case ND_COND:
            // #1019: GNU per-lane vector select -- the condition is itself a
            // vector (typically a comparison mask), and each lane independently
            // picks its then/els element (type.c's ND_COND type-checking, which
            // requires the arms to be identically-typed vectors here, and
            // codegen_expr.c's gen_vector_expr both dispatch on this same
            // is_vector(node->cond->ty) check). GCC accepts `cond ? a : b` with
            // a vector cond directly; clang rejects it ("used type '...' where
            // arithmetic or pointer type is required"). Lower to portable mask
            // arithmetic instead of relying on the GCC-only extension: the
            // condition's own vector type is guaranteed (type.c) to have the
            // same lane count/width as the arms, so it doubles as the mask type
            // and casting an arm to/from it is a same-size bitcast. `!= 0`
            // implements GCC's "nonzero", not "all-bits-set", per-lane
            // truthiness rule. Each of cond/then/els is evaluated exactly once.
            //
            // An ordinary C ternary with a *scalar* condition and vector arms
            // (standard C, not this GNU extension) falls through to the plain
            // `?:` below unchanged -- real clang already accepts that form.
            if (is_vector(node->cond->ty)) {
                fprintf(f, "__extension__ ({ ");
                serialize_type(f, ctx, node->cond->ty);
                fprintf(f, " __cccc_vsel_m = (");
                serialize_expr(f, vm, ctx, node->cond, 0);
                fprintf(f, ") != 0; ");
                serialize_type(f, ctx, node->ty);
                fprintf(f, " __cccc_vsel_t = (");
                serialize_expr(f, vm, ctx, node->then, 0);
                fprintf(f, "); ");
                serialize_type(f, ctx, node->ty);
                fprintf(f, " __cccc_vsel_f = (");
                serialize_expr(f, vm, ctx, node->els, 0);
                fprintf(f, "); (");
                serialize_type(f, ctx, node->ty);
                fprintf(f, ")((__cccc_vsel_m & (");
                serialize_type(f, ctx, node->cond->ty);
                fprintf(f, ")__cccc_vsel_t) | (~__cccc_vsel_m & (");
                serialize_type(f, ctx, node->cond->ty);
                fprintf(f, ")__cccc_vsel_f)); })");
                break;
            }
            // #1123: a wide-_BitInt cond can't appear bare in `?:` -- see
            // serialize_wide_bitint_truth's own comment.
            //
            // Precedence of the three operands is NOT 0. In C's grammar the
            // condition is a logical-OR-expression and the else-branch a
            // conditional-expression, so a comma or assignment expression in
            // either position must be parenthesized -- `a ? b : (c, d)` and
            // `(a = b) ? c : d`. Passing 0 (as the middle operand, a full
            // `expression`, legitimately may) dropped the parens the bundled
            // <assert.h> macro relies on: `((expr) ? (void)0 : (puts(...),
            // abort()))` serialized as `expr ? (void)0 : puts(...) , abort()`,
            // which re-parses as `(expr ? (void)0 : puts(...)) , abort()` --
            // abort() unconditionally. (Found via #1132's self-hosting spike:
            // every assert() in the compiled compiler fired on the first
            // hashmap rehash.) get_precedence(ND_COND) forces parens on
            // anything looser than `?:` itself (comma, assignment) while
            // leaving a right-associated nested ternary in the else-branch
            // bare; +1 additionally parenthesizes a nested ternary sitting in
            // the condition.
            if (!serialize_wide_bitint_truth(f, vm, ctx, node->cond))
                serialize_expr(f, vm, ctx, node->cond,
                               get_precedence(ND_COND) + 1);
            fprintf(f, " ? ");
            serialize_expr(f, vm, ctx, node->then, 0);
            fprintf(f, " : ");
            serialize_expr(f, vm, ctx, node->els, get_precedence(ND_COND));
            break;

        case ND_FUNCALL:
            // #969: __builtin_pc_function_name / __builtin_pc_source_location
            // lower to a call into a VM-only FFI shim (__cccc_pc_to_name /
            // __cccc_pc_to_source, cc_load_symbolize_runtime, debugger.c) whose
            // argument is a VM bytecode offset. Neither the shim nor the symbol
            // table it reads exists natively, so there is nothing to lower to
            // -- reject here rather than emit a call the host compiler rejects
            // by its internal name. Deliberately not rejected at parse time
            // (primary(), parse.c): under -c=generated only *generated* code is
            // serialized, and a __builtin_pc_* call in VM-only code is legal
            // there.
            if (node->lhs && node->lhs->kind == ND_VAR && node->lhs->var) {
                const char *pc_builtin = NULL;
                if (vm->compiler.builtin_pc_to_name &&
                    node->lhs->var == vm->compiler.builtin_pc_to_name)
                    pc_builtin = "__builtin_pc_function_name";
                else if (vm->compiler.builtin_pc_to_source &&
                         node->lhs->var == vm->compiler.builtin_pc_to_source)
                    pc_builtin = "__builtin_pc_source_location";
                if (pc_builtin) {
                    if (node->tok)
                        error_tok(vm, node->tok,
                                  "%s cannot be serialized to C: it resolves a "
                                  "VM bytecode offset via the VM's symbol "
                                  "table, which does not exist natively",
                                  pc_builtin);
                    else
                        error("cccc: %s cannot be serialized to C: it resolves "
                              "a VM bytecode offset via the VM's symbol table, "
                              "which does not exist natively",
                              pc_builtin);
                }
            }
            // #1054/#1030: setjmp/longjmp/_setjmp/_longjmp all print as calls
            // to exactly `_setjmp`/`_longjmp` -- real, plain `extern`-declared
            // functions on every supported host (unlike plain `setjmp`, a
            // macro on glibc) -- with the env argument cast to `(void *)`
            // rather than the implicit `long *` these builtins' VM-side
            // parameter type would otherwise print (parse_decl.c). See
            // serialize_synth_setjmp_decls()'s own comment (this file) for why:
            // it declares exactly these two names, and never replays the
            // captured `#include <setjmp.h>` line, so nothing else may spell
            // the callee any other way here.
            // sigsetjmp/siglongjmp lower to the *real* host sigsetjmp()/
            // siglongjmp() (via the __cccc_sigsetjmp macro
            // serialize_synth_setjmp_decls emits -- on glibc the real name is
            // __sigsetjmp) so the signal-mask save/restore is genuine; the
            // savemask/val argument rides through the trailing arg loop below.
            if (node->lhs && node->lhs->kind == ND_VAR && node->lhs->var &&
                (obj_is_reserved_builtin(node->lhs->var, "setjmp") ||
                 obj_is_reserved_builtin(node->lhs->var, "longjmp") ||
                 obj_is_reserved_builtin(node->lhs->var, "_setjmp") ||
                 obj_is_reserved_builtin(node->lhs->var, "_longjmp") ||
                 obj_is_reserved_builtin(node->lhs->var, "sigsetjmp") ||
                 obj_is_reserved_builtin(node->lhs->var, "siglongjmp"))) {
                bool is_sigsetjmp =
                    obj_is_reserved_builtin(node->lhs->var, "sigsetjmp");
                bool is_siglongjmp =
                    obj_is_reserved_builtin(node->lhs->var, "siglongjmp");
                bool is_longjmp =
                    obj_is_reserved_builtin(node->lhs->var, "longjmp") ||
                    obj_is_reserved_builtin(node->lhs->var, "_longjmp");
                fprintf(f, is_siglongjmp  ? "siglongjmp("
                           : is_sigsetjmp ? "__cccc_sigsetjmp("
                           : is_longjmp   ? "_longjmp("
                                          : "_setjmp(");
                Node *arg = node->args;
                if (arg) {
                    Node *env = arg;
                    while (env->kind == ND_CAST && env->lhs)
                        env = env->lhs;
                    fprintf(f, "(void *)");
                    serialize_expr(f, vm, ctx, env, node_prec);
                    arg = arg->next;
                }
                for (; arg; arg = arg->next) {
                    fprintf(f, ", ");
                    serialize_expr(f, vm, ctx, arg, 2);
                }
                fprintf(f, ")");
                break;
            }
            // #1085: any argument whose type is CCCC's own struct va_list must
            // arrive at the callee as an independent copy, never the caller's
            // own storage. A struct/union-by-value argument is passed here by
            // the caller's own address (#714/#1078), so *any* callee that
            // itself calls va_arg on the parameter -- a CCCC-emitted callee
            // (which also gets its own copy via #1062's shim, so a second copy
            // here is redundant but harmless) or, the case this fixes, a host
            // libc v*-family function (vprintf/vsnprintf/vsscanf/vsyslog/...,
            // which has no CCCC-emitted prologue to shim into and, on glibc,
            // has its own array-typed va_list that decays to a pointer in
            // parameter position, C17 6.7.6.3p7) -- would otherwise silently
            // advance the *caller's* va_list. Deliberately not narrowed to
            // "callee has no body" (host libc only): a call through a function
            // pointer would slip past that predicate and still alias on glibc,
            // and widening it costs nothing here. Wraps the whole call
            // (including the #1074 static-link injection just below, so a
            // nested-function callee stays correct too) in a statement
            // expression that declares one va_copy'd local per such argument
            // and substitutes it in the argument list below; no va_end on the
            // copy, mirroring #1062's own reasoning (verified there that
            // va_end expands to nothing observable for either va_list
            // representation this project targets).
            bool has_va_list_arg = false;
            for (Node *va_arg_scan = node->args; va_arg_scan;
                 va_arg_scan       = va_arg_scan->next) {
                if (type_is_cccc_va_list(va_arg_scan->ty)) {
                    has_va_list_arg = true;
                    break;
                }
            }
            char va_fwd_names[8][32];
            if (has_va_list_arg) {
                fprintf(f, "__extension__ ({ ");
                int va_fwd_idx = 0;
                for (Node *arg = node->args; arg;
                     arg       = arg->next, va_fwd_idx++) {
                    if (va_fwd_idx >= 8 || !type_is_cccc_va_list(arg->ty))
                        continue;
                    snprintf(va_fwd_names[va_fwd_idx],
                             sizeof va_fwd_names[va_fwd_idx],
                             "__cccc_va_fwd_%d", ctx->va_fwd_seq++);
                    fprintf(f, "va_list %s; va_copy(%s, ",
                            va_fwd_names[va_fwd_idx], va_fwd_names[va_fwd_idx]);
                    serialize_expr(f, vm, ctx, arg, 2);
                    fprintf(f, "); ");
                }
            }
            // #1155: reallocarray is declared by CCCC's own bundled
            // include/stdlib.h (routed through the VM heap's overflow-
            // checked REALCA opcode, see codegen_expr.c) but has no
            // definition on a host libc that lacks it (e.g. this SDK's
            // macOS) -- and since the guest's own `#include <stdlib.h>`
            // was captured and replayed, #901's bodiless-prototype pass
            // correctly declines to re-derive a declaration for it (that
            // header genuinely is in scope), leaving the emitted call
            // entirely undeclared. Route through the __cccc_reallocarray
            // accessor shim (serialize_native_accessor_shims,
            // serialize_shims.c) instead of the real name, same shape as
            // setjmp/longjmp's remap just above.
            if (is_extern_func_name(node->lhs, "reallocarray")) {
                fprintf(f, "__cccc_reallocarray(");
            } else {
                serialize_expr(f, vm, ctx, node->lhs, node_prec);
                fprintf(f, "(");
            }
            // #1074: a direct call to a nested function needs its hidden
            // __static_link argument supplied explicitly -- the parser already
            // gave the callee's own signature a leading `void *__static_link`
            // parameter (parse_decl.c), but nothing else ever passed it.
            // Mirrors codegen_expr.c's calling_nested value selection exactly:
            // calling one's own direct child passes that child's own env
            // (declared as
            // `__cccc_nenv` at the top of the function currently being
            // serialized, ctx->current_fn -- see serialize_function); calling a
            // sibling or an ancestor's nested function (only reachable from
            // inside that ancestor's own nest, so ctx->current_fn must itself
            // be nested) chases ->__up via nested_env_ptr_expr().
            // serialize_nested_ preamble() has already rejected, at compile
            // time, every reference to a nested function that ISN'T a direct
            // callee, so `node->lhs` here is guaranteed to be exactly this
            // shape whenever the check below matches.
            if (node->lhs && node->lhs->kind == ND_VAR && node->lhs->var &&
                node->lhs->var->is_function && node->lhs->var->is_nested &&
                !node->lhs->var->is_block) {
                Obj *callee_parent = node->lhs->var->parent_fn;
                Obj *current_fn    = ctx->current_fn;
                fprintf(f, "(void *)");
                if (callee_parent == current_fn)
                    fprintf(f, "&__cccc_nenv");
                else if (current_fn && current_fn->is_nested &&
                         !current_fn->is_block)
                    fprintf(f, "%s",
                            nested_env_ptr_expr(vm, ctx, current_fn,
                                                callee_parent));
                else
                    // Unreachable in valid C (a nested function's name has
                    // block scope, only visible inside its own parent's nest)
                    // -- mirror codegen_expr.c's identical fallback rather than
                    // emit nothing.
                    fprintf(f, "&__cccc_nenv");
                if (node->args)
                    fprintf(f, ", ");
            }
            {
                int va_fwd_idx = 0;
                for (Node *arg = node->args; arg;
                     arg       = arg->next, va_fwd_idx++) {
                    // #1042(b): a comma-expression argument (e.g. from a macro
                    // like ivalue(r) that expands to one) must stay
                    // parenthesized here -- the comma is the argument separator
                    // in this context, so an unparenthesized ND_COMMA silently
                    // splits into extra arguments
                    // ("too many arguments to function call"). parent_prec 2 is
                    // above get_precedence(ND_COMMA)'s 1 but below every other
                    // node kind's precedence, so only a bare top-level comma
                    // gets wrapped.
                    //
                    // #1085: a va_list-typed argument prints the va_copy'd
                    // local declared just above instead of re-serializing the
                    // original expression -- both for correctness (the callee
                    // must see the copy, not the caller's own storage) and to
                    // avoid evaluating a (rare, but legal) side-effecting
                    // va_list expression twice.
                    if (va_fwd_idx < 8 && has_va_list_arg &&
                        type_is_cccc_va_list(arg->ty))
                        fprintf(f, "%s", va_fwd_names[va_fwd_idx]);
                    else
                        serialize_expr(f, vm, ctx, arg, 2);
                    if (arg->next)
                        fprintf(f, ", ");
                }
            }
            fprintf(f, ")");
            if (has_va_list_arg)
                fprintf(f, "; })");
            break;

        case ND_MEMBER:
            serialize_expr(f, vm, ctx, node->lhs, node_prec);
            if (node->member && node->member->name)
                fprintf(f, ".%.*s", node->member->name->len,
                        node->member->name->loc);
            else if (!node->member)
                // Genuinely unresolved -- distinct from the anonymous-member
                // case below, which is a normal, valid access.
                fprintf(f, "./* unknown */");
            // else: node->member is an anonymous struct/union member (its
            // C11 6.7.2.1p13 members are promoted into the enclosing
            // aggregate's namespace) -- an intermediate link in a struct_ref()
            // chain like `x.a` through `struct { struct { int a; }; } x`.
            // It has no spelling of its own and is transparent in C, so it
            // must serialize to nothing rather than the placeholder comment
            // that used to sit here -- that produced `s./* unknown */.i`,
            // which the host compiler rejects outright.
            break;

        case ND_STMT_EXPR:
            fprintf(f, "({\n");
            for (Node *s = node->body; s; s = s->next) {
                serialize_stmt(f, vm, ctx, s, 1);
            }
            fprintf(f, "})");
            break;

        case ND_MEMZERO:
            if (node->var && node->var->is_block_var)
                // #965: a __block local's slot holds the heap box *pointer*
                // (see serialize_function's hoist loop), not the storage
                // itself -- &name/sizeof(name) would zero the 8-byte pointer
                // slot instead of the real storage. Mirrors codegen's own
                // is_block_var arm on ND_MEMZERO (codegen.c), the same arm
                // #982's TY_VLA case was modelled on.
                fprintf(f, "__builtin_memset(%s, 0, sizeof(*%s))",
                        node->var->name, node->var->name);
            else if (node->var)
                fprintf(f, "__builtin_memset(&%s, 0, sizeof(%s))",
                        node->var->name, node->var->name);
            else
                fprintf(f, "/* memzero */");
            break;

        case ND_NULL_EXPR:
            // Empty expression
            break;

        case ND_FRAME_ADDR:
            // The parser rejects any level but 0, so there is nothing to carry.
            fprintf(f, "__builtin_frame_address(0)");
            break;

        case ND_RETURN_ADDR:
            // The *value* diverges by design: under the VM this is a bytecode
            // pc cast to void*, natively it is a real host return address. Both
            // are "the return address n frames up" in their own runtime, which
            // is the most faithful mapping available -- see NATIVE.md.
            fprintf(f, "__builtin_return_address(%lld)", (long long)node->val);
            break;

        case ND_UNREACHABLE:
            // __builtin_unreachable, __builtin_trap and __builtin_debugtrap all
            // lower to the same BTRAP opcode, so the VM traps for all three and
            // the original spelling is not recoverable here. __builtin_trap()
            // is the emission that matches that behaviour;
            // __builtin_unreachable() would be UB natively and the optimizer
            // would delete the path.
            fprintf(f, "__builtin_trap()");
            break;

        case ND_BITOP: {
            // val = (op << 8) | width. popcount/parity encode width 0 (see
            // parse.c), so the `ll` variant has to come from the argument's own
            // type -- emitting __builtin_popcount for a 64-bit argument would
            // compile cleanly and silently truncate.
            int  op    = (int)(node->val >> 8);
            int  width = (int)(node->val & 0xff);
            bool wide  = node->lhs && node->lhs->ty && node->lhs->ty->size == 8;
            const char *name;
            switch (op) {
                case 0:
                    name = (width == 64) ? "__builtin_clzll" : "__builtin_clz";
                    break;
                case 1:
                    name = (width == 64) ? "__builtin_ctzll" : "__builtin_ctz";
                    break;
                case 2:
                    name = wide ? "__builtin_popcountll" : "__builtin_popcount";
                    break;
                case 3:
                    name = wide ? "__builtin_parityll" : "__builtin_parity";
                    break;
                case 4:
                    name = (width == 64) ? "__builtin_ffsll" : "__builtin_ffs";
                    break;
                default:
                    // bswap: `width` is the byte count, not a bit width.
                    name = (width == 2)   ? "__builtin_bswap16"
                           : (width == 4) ? "__builtin_bswap32"
                                          : "__builtin_bswap64";
                    break;
            }
            fprintf(f, "%s(", name);
            serialize_expr(f, vm, ctx, node->lhs, 0);
            fprintf(f, ")");
            break;
        }

        case ND_VLA_PTR:
            // #964: `v` decayed to `v` -- serialize_stmt_list_item()/the
            // ND_EXPR_STMT case above already replace this node's only
            // constructor site (the `v = alloca(...)` assignment, parse.c) with
            // a real declaration, so this is a defensive fallback for any other
            // use of the variable (e.g. `v[0]` decays through ND_ADD, which
            // reaches here via node->lhs). `v` is now a genuine C array/VLA
            // local, so referencing its name is correct in both lvalue and
            // rvalue position.
            fprintf(f, "%s", node->var ? node->var->name : "/* unknown_vla */");
            break;

        case ND_OVERFLOW_ARITH: {
            // #964: val: 0=add 1=sub 2=mul (parse.c); lhs/rhs are the operands,
            // cas_addr the result pointer -- this maps directly onto the same
            // three GCC/clang builtins the parser accepted, both of which
            // support this signature natively.
            static const char *names[] = {
                "__builtin_add_overflow",
                "__builtin_sub_overflow",
                "__builtin_mul_overflow",
            };
            const char *name = (node->val >= 0 && node->val <= 2)
                                   ? names[node->val]
                                   : "__builtin_add_overflow";
            // #1042(b): every comma below is a real argument separator to a
            // fixed-arity builtin, same comma-in-arg-position hazard as
            // ND_FUNCALL.
            fprintf(f, "%s(", name);
            serialize_expr(f, vm, ctx, node->lhs, 2);
            fprintf(f, ", ");
            serialize_expr(f, vm, ctx, node->rhs, 2);
            fprintf(f, ", ");
            serialize_expr(f, vm, ctx, node->cas_addr, 2);
            fprintf(f, ")");
            break;
        }

        case ND_DYNOBJ_SIZE:
            fprintf(f, "__builtin_dynamic_object_size(");
            // #1042(b): see ND_OVERFLOW_ARITH above.
            serialize_expr(f, vm, ctx, node->lhs, 2);
            fprintf(f, ", %lld)", (long long)node->val);
            break;

        case ND_FENCE: {
            // atomic_thread_fence/atomic_signal_fence (#1188). Unlike every
            // other atomic operation in this file, `order` is NOT discarded
            // in favour of a fixed __ATOMIC_SEQ_CST: a fence with no order
            // does nothing at all, where seq_cst elsewhere is merely
            // stronger than whatever was requested. A constant order prints
            // as its symbolic __ATOMIC_* name for readable output; anything
            // else (order is a function parameter, computed value, etc. --
            // C11 specifies these as ordinary functions, so a non-constant
            // order is conforming) is serialized verbatim, since GCC/Clang's
            // __atomic_*_fence builtins accept a runtime int just as readily.
            fprintf(f, node->val ? "__atomic_signal_fence("
                                 : "__atomic_thread_fence(");
            const char *name = node->lhs->kind == ND_NUM
                                   ? atomic_order_name(node->lhs->val)
                                   : NULL;
            if (name)
                fprintf(f, "%s", name);
            else
                serialize_expr(f, vm, ctx, node->lhs, 2);
            fprintf(f, ")");
            break;
        }

        case ND_ALOAD:
            // codegen only takes the atomic path for 1/2/4/8-byte non-float
            // pointees and falls back to a plain load otherwise; mirror that,
            // since __atomic_load_n does not accept a float or aggregate
            // pointee and the VM is not being atomic there either.
            if (atomic_serializable_pointee(node->lhs)) {
                fprintf(f, "__atomic_load_n(");
                // #1101: strip _Atomic from the operand's static type.
                serialize_atomic_addr(f, vm, ctx, node->lhs);
                fprintf(f, ", __ATOMIC_SEQ_CST)");
            } else {
                fprintf(f, "(*(");
                serialize_expr(f, vm, ctx, node->lhs, 0);
                fprintf(f, "))");
            }
            break;

        case ND_ASTORE:
            // An atomic store in *expression* position has to yield the stored
            // value (codegen gives it C assignment semantics) but
            // __atomic_store_n returns void, so the value is threaded through a
            // statement expression rather than evaluating the operand twice.
            // The common statement-position case is handled in serialize_stmt
            // and emits the plain call.
            if (atomic_serializable_pointee(node->lhs)) {
                fprintf(f, "__extension__ ({ __typeof__(*(");
                serialize_expr(f, vm, ctx, node->lhs, 0);
                fprintf(f, ")) __cccc_astore_v = (");
                serialize_expr(f, vm, ctx, node->rhs, 0);
                fprintf(f, "); __atomic_store_n(");
                // #1101: strip _Atomic from the operand's static type.
                serialize_atomic_addr(f, vm, ctx, node->lhs);
                fprintf(f, ", __cccc_astore_v, __ATOMIC_SEQ_CST); "
                           "__cccc_astore_v; })");
            } else {
                fprintf(f, "(*(");
                serialize_expr(f, vm, ctx, node->lhs, 0);
                fprintf(f, ") = ");
                // #1042(b): rhs sits after `=` inside one shared enclosing
                // paren, not its own -- an unparenthesized top-level ND_COMMA
                // here would bind looser than `=` and change which value the
                // whole parenthesized expression yields.
                serialize_expr(f, vm, ctx, node->rhs, 2);
                fprintf(f, ")");
            }
            break;

        case ND_EXCH:
            // codegen rejects float/odd-size pointees outright for exchange and
            // compare-and-swap, so these two map 1:1 with no fallback arm.
            fprintf(f, "__atomic_exchange_n(");
            // #1101: strip _Atomic from the operand's static type.
            serialize_atomic_addr(f, vm, ctx, node->lhs);
            fprintf(f, ", ");
            serialize_expr(f, vm, ctx, node->rhs, 2);
            fprintf(f, ", __ATOMIC_SEQ_CST)");
            break;

        case ND_CAS:
            // (obj, *expected, desired) -> bool, matching codegen's ACAS
            // contract: cas_old is a *pointer* to the expected value, as
            // __atomic_compare_ exchange_n also takes. weak = 0.
            fprintf(f, "__atomic_compare_exchange_n(");
            // #1101: strip _Atomic from both pointer operands' static types
            // (the address, and the pointer to the expected value -- the
            // builtin wants both to point at unqualified objects).
            serialize_atomic_addr(f, vm, ctx, node->cas_addr);
            fprintf(f, ", ");
            serialize_atomic_addr(f, vm, ctx, node->cas_old);
            fprintf(f, ", ");
            serialize_expr(f, vm, ctx, node->cas_new, 2);
            fprintf(f, ", 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)");
            break;

        case ND_LABEL_VAL:
            // [GNU] labels-as-values. node->label is the source identifier, the
            // same one ND_LABEL/ND_GOTO already serialize.
            fprintf(f, "&&%s",
                    node->label ? node->label : "/* unknown label */");
            break;

        case ND_COMPLEX: {
            // val: 0 = construct from (real, imag), 1 = creal, 2 = cimag,
            // 3 = conj. The f/l suffix follows the element float type.
            Type *elem = node->ty;
            if (elem && elem->kind == TY_COMPLEX && elem->base)
                elem = elem->base;
            const char *suffix = !elem                        ? ""
                                 : (elem->kind == TY_FLOAT)   ? "f"
                                 : (elem->kind == TY_LDOUBLE) ? "l"
                                                              : "";
            if (node->val == 0) {
                // __builtin_complex requires both operands to have the same
                // real floating type, so each is cast to the element type
                // explicitly.
                fprintf(f, "__builtin_complex((");
                serialize_type(f, ctx, elem);
                fprintf(f, ")(");
                serialize_expr(f, vm, ctx, node->lhs, 0);
                fprintf(f, "), (");
                serialize_type(f, ctx, elem);
                fprintf(f, ")(");
                serialize_expr(f, vm, ctx, node->rhs, 0);
                fprintf(f, "))");
            } else {
                const char *name = (node->val == 1)   ? "creal"
                                   : (node->val == 2) ? "cimag"
                                                      : "conj";
                fprintf(f, "__builtin_%s%s(", name, suffix);
                // #1042(b): single-argument builtin -- a bare top-level comma
                // in the operand would still misparse as an extra argument.
                serialize_expr(f, vm, ctx, node->lhs, 2);
                fprintf(f, ")");
            }
            break;
        }

        case ND_CONVERTVECTOR:
            fprintf(f, "__builtin_convertvector(");
            // #1042(b): the builtin's own ", " separates the operand from the
            // target type, same comma-in-arg-position hazard as ND_FUNCALL.
            serialize_expr(f, vm, ctx, node->lhs, 2);
            fprintf(f, ", ");
            serialize_type(f, ctx, node->ty);
            fprintf(f, ")");
            break;

        case ND_DECIMAL_TO_CHARS:
            // #402, CCCC_HAS_DECIMAL builds only. Unlike every other builtin
            // here there is no host equivalent to lower to -- clang and gcc
            // have no _Decimal support at all -- so this fails loudly rather
            // than fabricating a call that would not link.
            error("cccc: _Decimal is not supported in native/serialized output "
                  "(__builtin_decimal_to_chars has no host equivalent)");
            break;

        case ND_BLOCK_LITERAL: {
            // #965: default lowering -- lift + explicit env struct
            // (serialize_block_preamble, block_capture_index). The env struct
            // instance lives in node->block_desc_var, an existing local on the
            // *enclosing* function's frame (block_literal(), parse.c) --
            // exactly matching the VM's own per-invocation stack descriptor
            // (ND_BLOCK_LITERAL, codegen.c), so lifetime semantics carry over
            // unchanged. Building it as a comma expression writes into that
            // named local (rather than a temporary), which is what keeps the
            // block value's address stable for the rest of the enclosing
            // scope.
            Obj        *block_fn = node->block_fn;
            const char *env = block_fn ? find_block_env(ctx, block_fn) : NULL;
            if (!node->block_desc_var || !block_fn || !env) {
                // #963c: this used to fall back to the same
                // "/* unsupported expr kind N */" comment as the generic
                // default: arm below -- a silent drop inside a *handled* case,
                // in the newest code in this file. block_desc_var/block_fn/env
                // are all set unconditionally by block_literal() (parse.c) and
                // serialize_block_preamble()'s registration pass before this
                // function ever runs, so reaching here means one of those
                // invariants was violated upstream; fail loudly instead of
                // emitting a null expression.
                if (node->tok)
                    error_tok(vm, node->tok,
                              "internal error: block literal is missing its "
                              "descriptor local, function, or env struct at "
                              "serialization time");
                else
                    error("cccc: internal error: block literal is missing its "
                          "descriptor local, function, or env struct at "
                          "serialization time");
                break;
            }
            if (!ctx->current_fn) {
                // A block literal's descriptor is a *local* -- there is no
                // enclosing frame to hold one at file scope.
                if (node->tok)
                    error_tok(vm, node->tok,
                              "a block literal cannot be serialized at file "
                              "scope (its descriptor needs an enclosing "
                              "function's frame)");
                else
                    error("cccc: a block literal cannot be serialized at file "
                          "scope");
            }

            const char *desc = node->block_desc_var->name;
            fprintf(f,
                    "(%s.__invoke = (void *)%s, %s.__size = (long)sizeof(%s)",
                    desc, block_fn->name, desc, desc);
            for (int i = 0; i < node->num_block_captures; i++) {
                Obj *cap = node->block_captures[i];
                // #994: a by-value capture whose type is an array (accepted by
                // the parser like clang rejects but this compiler doesn't --
                // collect_captures_in_node has no guard) can't use plain `=` --
                // C forbids array assignment. The env struct field is declared
                // with the real array type (serialize_block_preamble), so copy
                // through __builtin_memcpy instead; every other capture kind
                // (scalar, struct/union, block-var pointer) keeps plain `=`,
                // valid C for all of them.
                bool is_array_cap =
                    !cap->is_block_var && cap->ty->kind == TY_ARRAY;
                if (is_array_cap)
                    fprintf(f, ", __builtin_memcpy(%s.__cap%d, ", desc, i);
                else
                    fprintf(f, ", %s.__cap%d = ", desc, i);

                // Mirrors codegen's ND_BLOCK_LITERAL capture-copy loop
                // (codegen.c) exactly, three sources in the same order:
                int enc_idx = (ctx->current_fn->is_block)
                                  ? block_capture_index(ctx->current_fn, cap)
                                  : -1;
                const char *enc_env =
                    enc_idx >= 0 ? find_block_env(ctx, ctx->current_fn) : NULL;
                // #1080: capture owned by an ancestor of the enclosing
                // *nested function* itself (not by the enclosing block) --
                // registered as an upvar of the real owner by
                // collect_nested_refs()'s ND_BLOCK_LITERAL arm. Read it back
                // through the same env chase an ordinary nested-function
                // upvar reference uses (nested_env_ptr_expr), one
                // dereference either way -- for an is_block_var capture that
                // yields the shared box pointer (copied verbatim, same as
                // the enc_idx branch above); for a plain capture it yields
                // the value itself.
                Obj *anc_owner = NULL;
                if (enc_idx < 0 && cap->is_local &&
                    ctx->current_fn->is_nested && !ctx->current_fn->is_block &&
                    !nested_var_is_own(ctx->current_fn, cap)) {
                    for (Obj *anc = ctx->current_fn->parent_fn; anc;
                         anc      = anc->parent_fn)
                        if (nested_var_is_own(anc, cap)) {
                            anc_owner = anc;
                            break;
                        }
                }
                int anc_idx = -1;
                if (anc_owner) {
                    for (int i = 0; i < ctx->nested_envs_len; i++)
                        if (ctx->nested_envs[i].owner_fn == anc_owner) {
                            for (int j = 0; j < ctx->nested_envs[i].upvars_len;
                                 j++)
                                if (ctx->nested_envs[i].upvars[j] == cap) {
                                    anc_idx = j;
                                    break;
                                }
                            break;
                        }
                }
                if (enc_idx >= 0 && enc_env) {
                    // Transitive capture: read from the enclosing block's own
                    // descriptor via __static_link. Exactly one dereference
                    // either way -- for an is_block_var capture the parent's
                    // field already holds the box pointer (copied verbatim
                    // below); for a plain capture the parent's field holds the
                    // value itself.
                    fprintf(f, "((%s *)__static_link)->__cap%d", enc_env,
                            enc_idx);
                } else if (anc_owner && anc_idx >= 0) {
                    fprintf(f, "(*%s->__uv%d)",
                            nested_env_ptr_expr(vm, ctx, ctx->current_fn,
                                                anc_owner),
                            anc_idx);
                } else if (cap->is_block_var) {
                    // Direct __block local in the enclosing stack: copy its box
                    // pointer verbatim -- the new field is T*, matching it.
                    fprintf(f, "%s", cap->name);
                } else {
                    // Ordinary local or global: copy its value.
                    fprintf(f, "%s", cap->name);
                }
                if (is_array_cap)
                    fprintf(f, ", sizeof(%s.__cap%d))", desc, i);
            }
            fprintf(f, ", (struct __cccc_block *)&%s)", desc);
            break;
        }

        case ND_BLOCK_CALL: {
            // #965: GNU statement expression -- the descriptor pointer is
            // needed twice (loaded from ->__invoke, then passed again as the
            // static link), and evaluating node->lhs a second time would be
            // wrong for a non-idempotent expression (e.g. a block-returning
            // function call as the callee). gcc and clang both accept
            // statement expressions, and this file's output is already
            // GNU-flavoured (__builtin_memset/__builtin_memcpy elsewhere,
            // ND_STMT_EXPR itself above).
            fprintf(f, "({ struct __cccc_block *__cccc_blk = (struct "
                       "__cccc_block *)(");
            serialize_expr(f, vm, ctx, node->lhs, 0);
            fprintf(f, "); ((");
            serialize_type(f, ctx, node->ty);
            fprintf(f, " (*)(void *");
            Type *block_ty =
                (node->lhs && node->lhs->ty && node->lhs->ty->kind == TY_BLOCK)
                    ? node->lhs->ty
                    : NULL;
            for (Type *p = block_ty ? block_ty->params : NULL; p; p = p->next) {
                fprintf(f, ", ");
                serialize_type(f, ctx, p);
            }
            fprintf(f, "))__cccc_blk->__invoke)(__cccc_blk");
            for (Node *arg = node->args; arg; arg = arg->next) {
                fprintf(f, ", ");
                // #1042(b): same comma-in-arg-position hazard as ND_FUNCALL's
                // argument loop above.
                serialize_expr(f, vm, ctx, arg, 2);
            }
            fprintf(f, "); })");
            break;
        }

        case ND_MACRO_CALL:
        case ND_INIT_SPLICE:
        case ND_QUOTE_LAZY:
            // #963c/#1242: all three are comptime-internal and are consumed
            // before this
            // function ever runs -- ND_MACRO_CALL is compiled away by
            // compile_all_macros/cc_eager_expand_macro_call during
            // cc_expand_macros (main.c), which always runs ahead of the
            // -m/-c=native/-c=generated serialization pass, and the one path
            // that could defer one into a global initializer
            // (has_pending_macro_init, parse.c) is resolved to concrete .data
            // bytes by cc_finalize_macro_gvar_inits, also inside
            // cc_expand_macros, before serialization starts. ND_INIT_SPLICE is
            // likewise expanded away by quote_substitute at comptime. Reaching
            // either case here means a macro/splice escaped expansion, which is
            // an internal invariant violation, not user-writable input -- fail
            // loudly and name the kind rather than emitting a silently-dropped
            // comment.
            if (node->tok)
                error_tok(vm, node->tok,
                          "internal error: %s reached the serializer "
                          "unexpanded (should have been resolved during "
                          "macro/comptime expansion)",
                          cc_node_kind_name(node->kind));
            else
                error("cccc: internal error: %s reached the serializer "
                      "unexpanded (should have been resolved during "
                      "macro/comptime expansion)",
                      cc_node_kind_name(node->kind));
            break;

        default:
            // #963c: every reachable NodeKind is expected to have an explicit
            // case above (see NATIVE.md's "Serialized-output divergences"
            // section for the constructs that are intentionally dropped with a
            // diagnostic rather than serialized). This used to emit
            // "/* unsupported expr kind N */" and keep going -- in expression
            // position that fails the host build loudly, but in statement
            // position (serialize_stmt's own default: routes here and appends
            // ";") it produced a syntactically valid null statement: the
            // construct silently vanished and the native binary returned a
            // different answer than the VM (#963's whole motivation). Fail
            // immediately and name the kind instead, so the next NodeKind added
            // without a case here is caught at implementation/test time rather
            // than silently miscompiling.
            if (node->tok)
                error_tok(vm, node->tok,
                          "internal error: no serializer case for %s (kind %d)",
                          cc_node_kind_name(node->kind), node->kind);
            else
                error(
                    "cccc: internal error: no serializer case for %s (kind %d)",
                    cc_node_kind_name(node->kind), node->kind);
            break;
    }

    if (need_parens)
        fprintf(f, ")");
}

// #1124: does `ty` need an explicit width mask after a value-producing
// operation under -c=native/-m? serialize_type's TY_BITINT case (above)
// picks a container purely from ty->size -- signed char/short/int/long/
// __int128 -- and nowhere re-masks a computed value back down to
// ty->bit_width the way the VM's own emit_bitint_trunc (src/codegen_emit.c)
// does. The host container only truncates for free when its own width
// exactly matches the declared _BitInt(N) width (N in {8,16,32,64,128});
// every other N needs the mask this wrapper emits. ty->size > 16 already
// hard-errors at serialize_type (#1121/#1123), so this never fires for
// those.
static bool bitint_needs_mask(Type *ty) {
    return ty && ty->kind == TY_BITINT && ty->size <= 16 &&
           ty->bit_width != ty->size * 8;
}

// Kinds whose *result* can carry bits above the declared bit_width that the
// host container wouldn't otherwise truncate: arithmetic that can overflow
// the value's own N bits, a cast that (re)establishes the type, and the two
// unary ops that can set bits above N (~x always can; -x can too, e.g. the
// INT_MIN-style all-ones-into-sign-bit case). Deliberately excluded:
// comparisons/logical ops (int-valued, never produce a _BitInt result) and
// ND_MOD/ND_SHR/ND_BITAND/ND_BITOR/ND_BITXOR (structurally cannot widen an
// already-in-range operand's own value beyond N bits).
static bool bitint_op_needs_mask(NodeKind kind) {
    switch (kind) {
        case ND_ADD:
        case ND_SUB:
        case ND_MUL:
        case ND_DIV:
        case ND_SHL:
        case ND_NEG:
        case ND_BITNOT:
        case ND_CAST:
            return true;
        default:
            return false;
    }
}

// #1124: public serialize_expr entry point. Wraps serialize_expr_raw's own
// switch-on-node->kind body with a width mask when `node` is a _BitInt(N)
// result that needs one -- every recursive serialize_expr call inside the
// raw body (sub-expressions) re-enters here, so nested operands are masked
// at their own width independently.
//
// The shift-pair mirrors emit_bitint_trunc exactly, but must run in a fixed
// *computation* width (WT: `long`/`unsigned long`, 64 bits, or __int128 for
// a size==16 container) rather than the container type T itself -- casting
// straight to a narrow T (e.g. `signed char` for _BitInt(5)) and shifting
// there would be silently wrong, since C's integer promotions re-widen a
// char/short operand to `int` for the shift regardless of the preceding
// cast, making the shift amount (computed against T's own width) apply in
// the wrong width. Casting to WT first pins the shift's actual width; the
// outer "(T)" then narrows the already-correctly-truncated result back down
// to the container type, which is well-defined (two's-complement wrap on
// every host this project targets, same as any ordinary narrowing
// conversion).
//
// The left shift always runs on the *unsigned* wide type (UWT), even when
// `ty` is signed: `x << S` where x is a negative signed value is only
// well-defined starting C23 (two's-complement wraparound; UB before that),
// and clang warns on it (-Wshift-negative-value) regardless of the TU's own
// -std, since the generated file's #include chain can pull in headers
// parsed under a different effective mode. Shifting the unsigned bit
// pattern instead sidesteps the question entirely -- left shift of an
// unsigned type is always well-defined overflow-as-wraparound. The
// right shift then runs on the signed WT so it arithmetic-shifts (sign-
// extends) exactly like the VM's own SHR3, which is implementation-defined
// but universal on every real gcc/clang target.
void serialize_expr(FILE *f, VirtualMachine *vm, SerializeContext *ctx,
                    Node *node, int parent_prec) {
    if (node && bitint_needs_mask(node->ty) &&
        bitint_op_needs_mask(node->kind)) {
        Type       *ty      = node->ty;
        bool        wide_wt = ty->size == 16;
        int         wt_bits = wide_wt ? 128 : 64;
        int         shift   = wt_bits - ty->bit_width;
        const char *wt =
            wide_wt ? (ty->is_unsigned ? "unsigned __int128" : "__int128")
                    : (ty->is_unsigned ? "unsigned long" : "long");
        const char *uwt = wide_wt ? "unsigned __int128" : "unsigned long";
        fprintf(f, "((");
        serialize_type(f, ctx, ty);
        fprintf(f, ")((%s)((%s)(", wt, uwt);
        serialize_expr_raw(f, vm, ctx, node, 0);
        fprintf(f, ") << %d) >> %d))", shift, shift);
        return;
    }
    serialize_expr_raw(f, vm, ctx, node, parent_prec);
}

// #1235: new_inc_dec() lowers a postfix `A++` to
// `(typeof A)((A += 1) + -1)`, and to_assign() further lowers the `+= 1`
// into `(tmp = &A, *tmp = *tmp + 1)`. The trailing `+ -1` reconstructs the
// pre-increment value `A++` must yield -- dead in any value-discarding
// position, and its own `-Wunused-value` trigger. In such a position the
// caller routes the node here instead of serialize_expr(): peel the
// is_inc_dec_result cast and the compensating ND_ADD and emit only the
// store. Structural, so `A--` (compensating term `+ 1`), a pointer operand
// (term scaled by the element size) and a floating operand (`+ -1.0`) are
// all handled without a per-case check. Anything else is forwarded intact.
void serialize_discard_expr(FILE *f, VirtualMachine *vm, SerializeContext *ctx,
                            Node *node, int parent_prec) {
    if (node && node->kind == ND_CAST && node->is_inc_dec_result) {
        // Shape: ND_CAST(is_inc_dec_result) -> ND_ADD(<rmw>, <compensating>),
        // where <rmw> is to_assign()'s `(tmp = &A, *tmp = *tmp + n)` comma,
        // usually wrapped in a promotion ND_CAST that usual_arith_conv()
        // inserted (a no-op int->int for a plain int operand, a real
        // (double)/(T *) for a float/pointer operand). Descend to the comma
        // and emit only that -- the surrounding cast and the compensating
        // term both exist solely to reconstruct `A++`'s value, dead here.
        Node *rmw = node->lhs;
        if (rmw && rmw->kind == ND_ADD && rmw->lhs) {
            rmw = rmw->lhs;
            while (rmw && rmw->kind == ND_CAST && rmw->lhs)
                rmw = rmw->lhs;
            serialize_expr(f, vm, ctx, rmw, parent_prec);
            return;
        }
    }
    serialize_expr(f, vm, ctx, node, parent_prec);
}
