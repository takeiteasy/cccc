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

// Serialization: statements (#1150).
#include "./serialize_internal.h"

// Forward declaration
// #1124: serialize_expr's actual switch-on-node->kind body, renamed so the
// public serialize_expr can wrap it with the _BitInt width-mask check below
// without recursing into itself.
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

// #1209: `var` just came into existence at its own in-place declaration
// (the VLA/pointer-to-VLA sites below, `ctx->current_fn` is `var`'s owning
// function throughout its body). If some nested descendant reads `var`
// across a static link, its env field couldn't be filled at the top of the
// function the way an ordinary upvar's is (nested_upvar_is_deferred()) --
// fill it here instead, now that `&var` is finally valid.
//
// Two things that look like bugs and aren't: a sibling nested function
// sharing this same env may be called before this assignment runs, and the
// field goes stale once a block containing `var` exits. Both are safe for
// the same reason -- a nested function that cannot see `var` in its own
// lexical scope can never reach a call that would read this field, either
// before it's first assigned or after `var`'s scope ends.
static void emit_deferred_nested_upvar_store(FILE *f, VirtualMachine *vm,
                                             SerializeContext *ctx, int indent,
                                             Obj *var) {
    for (int i = 0; i < ctx->nested_envs_len; i++) {
        NestedEnvEntry *e = &ctx->nested_envs[i];
        if (e->owner_fn != ctx->current_fn)
            continue;
        for (int j = 0; j < e->upvars_len; j++) {
            if (e->upvars[j] != var)
                continue;
            print_indent_level(f, indent);
            fprintf(f, "__cccc_nenv.__uv%d = (", j);
            serialize_type_decl(f, ctx, nested_upvar_field_type(vm, var), "");
            fprintf(f, ")&%s;\n", var->name);
            return;
        }
    }
}

// Serialize a statement
void serialize_stmt(FILE *f, VirtualMachine *vm, SerializeContext *ctx,
                    Node *node, int indent) {
    if (!node)
        return;

    switch (node->kind) {
        case ND_RETURN:
            print_indent_level(f, indent);
            fprintf(f, "return");
            if (node->lhs) {
                fprintf(f, " ");
                serialize_expr(f, vm, ctx, node->lhs, 0);
            }
            fprintf(f, ";\n");
            break;

        case ND_EXPR_STMT:
            if (is_noop_expr(node->lhs))
                break;
            // #964: `v = alloca(tmp)` is declaration()'s lowering of a VLA
            // local
            // -- re-emitting it literally would diverge from VM semantics
            // (alloca in a loop body is not freed per iteration the way a real
            // VLA is, so a loop declaring a VLA would grow the host stack
            // unboundedly) and the assignment target isn't a valid C lvalue
            // once `v` is a genuine array. Emit a real declaration in its place
            // instead; serialize_stmt_list_item() keeps the enclosing block
            // unbraced so it stays visible to later statements.
            if (node_is_vla_ptr_assign(node->lhs)) {
                Obj *var = node->lhs->lhs->var;
                print_indent_level(f, indent);
                serialize_type_decl(f, ctx, var->ty, var->name);
                fprintf(f, ";\n");
                emit_deferred_nested_upvar_store(f, vm, ctx, indent, var);
                break;
            }
            // #973 follow-up: the initializer of a pointer-to-VLA local (see
            // Obj.deferred_vla_ptr_init, cccc.h) was skipped by the hoist loop
            // above -- this is its recorded in-place declaration site. Emit a
            // real declaration with the initializer attached instead of a bare
            // assignment to an as-yet-undeclared name. Identity (not shape)
            // comparison: node->lhs is a plain ND_ASSIGN like any reassignment
            // of the same variable would produce, so only the exact node
            // recorded at parse time is treated as the declaration.
            if (node_is_deferred_vla_ptr_init(node->lhs)) {
                Obj *var = node->lhs->lhs->var;
                print_indent_level(f, indent);
                serialize_type_decl(f, ctx, var->ty, var->name);
                fprintf(f, " = ");
                // #1042(b): this `=` is printed manually, outside
                // serialize_expr's own ND_ASSIGN case (which already protects
                // its rhs via node_prec + 1) -- an unparenthesized top-level
                // ND_COMMA here would bind looser than `=` and initialize the
                // declaration with the comma's first operand instead of its
                // value.
                serialize_expr(f, vm, ctx, node->lhs->rhs, 2);
                fprintf(f, ";\n");
                emit_deferred_nested_upvar_store(f, vm, ctx, indent, var);
                break;
            }
            // An atomic store written as its own statement (the usual case)
            // discards its value, so hand it to the ND_ASTORE statement case
            // and emit the plain void-returning call rather than the
            // value-producing statement expression.
            if (node->lhs && node->lhs->kind == ND_ASTORE) {
                serialize_stmt(f, vm, ctx, node->lhs, indent);
                break;
            }
            print_indent_level(f, indent);
            // #1235: a bare `A++;`/`A--;` statement discards its value -- emit
            // the underlying store without the dead value-reconstruction term
            // that otherwise trips -Wunused-value.
            serialize_discard_expr(f, vm, ctx, node->lhs, 0);
            fprintf(f, ";\n");
            break;

        case ND_BLOCK:
            // #1098: a block-scope _Static_assert/static_assert parses to
            // an otherwise-empty ND_BLOCK with its condition/message
            // stashed on it (static_assert_decl(), parse_stmt.c) -- emit
            // the real assert here instead of an empty `{}` when it
            // qualifies (see serialize_static_assert's own gate); an
            // unqualifying one is left as the pre-existing empty block,
            // same as before this ticket.
            if (node->static_assert_cond) {
                serialize_static_assert(f, vm, ctx, node->static_assert_cond,
                                        node->static_assert_msg,
                                        node->static_assert_msg_len, node->tok,
                                        indent);
                break;
            }
            print_indent_level(f, indent);
            fprintf(f, "{\n");
            for (Node *s = node->body; s; s = s->next) {
                serialize_stmt_list_item(f, vm, ctx, s, indent + 1);
            }
            print_indent_level(f, indent);
            fprintf(f, "}\n");
            break;

        case ND_IF:
            print_indent_level(f, indent);
            fprintf(f, "if (");
            // #1123: a wide-_BitInt cond can't appear bare here.
            if (!serialize_wide_bitint_truth(f, vm, ctx, node->cond))
                serialize_expr(f, vm, ctx, node->cond, 0);
            fprintf(f, ")\n");
            serialize_stmt(f, vm, ctx, node->then, indent + 1);
            if (node->els) {
                print_indent_level(f, indent);
                fprintf(f, "else\n");
                serialize_stmt(f, vm, ctx, node->els, indent + 1);
            }
            break;

        case ND_FOR:
            print_indent_level(f, indent);
            fprintf(f, "for (");
            // #927: a declaration-form init (`for (int i = 0; ...)`) parses as
            // an ND_BLOCK whose body is one ND_EXPR_STMT per declarator
            // (declaration(), parse.c) -- not an expression, so handing it to
            // serialize_expr fell through to its default case and silently
            // dropped the initialization (loop variable left uninitialized;
            // #963c has since turned that default case into a hard error, so
            // this exact failure mode can no longer reach the host compiler
            // silently -- this ND_FOR handling avoids it in the first place by
            // never calling serialize_expr on the ND_BLOCK at all). The
            // declarations themselves
            // are already hoisted to the top of the function by
            // serialize_function(); only the initializing assignment(s) belong
            // in the init clause, comma-joined for a multi-declarator init
            // (`for (int i = 0, j = 1; ...)`). A no-initializer declaration
            // (`for (int i; ...)`) has an empty body -- emit nothing, matching
            // a bare `for (;;)`-style empty init clause. A non-declaration init
            // (`for (i = 0; ...)`) is a bare ND_EXPR_STMT (expr_stmt(),
            // parse.c) and serializes the same way.
            if (node->init) {
                // #964: a VLA declared in a for-loop initializer (`for (int i =
                // 0, v[n]; ...)`) parses and runs in the VM, but this init
                // clause is serialized as comma-joined *assignments* below --
                // C forbids mixing a declaration with expressions there, and
                // hoisting the declaration out ahead of the loop would change
                // its scope/lifetime (and can read a variable the init clause
                // itself assigns). Rejected with a diagnostic rather than
                // emitted as broken C; doing this properly is tracked as a
                // follow-up.
                if (node->init->kind == ND_BLOCK &&
                    block_defines_vla(node->init))
                    error_tok(vm, node->tok,
                              "a variable-length array declared in a for-loop "
                              "initializer cannot be serialized to C");
                if (node->init->kind == ND_BLOCK) {
                    bool first_init = true;
                    for (Node *s = node->init->body; s; s = s->next) {
                        if (s->kind != ND_EXPR_STMT || is_noop_expr(s->lhs))
                            continue;
                        if (!first_init)
                            fprintf(f, ", ");
                        first_init = false;
                        // #1042(b): this loop already comma-joins multiple
                        // declarator inits itself (the ", " above) -- an
                        // unparenthesized ND_COMMA inside one declarator's own
                        // initializer would be indistinguishable from another
                        // separator comma.
                        serialize_expr(f, vm, ctx, s->lhs, 2);
                    }
                } else if (node->init->kind == ND_EXPR_STMT) {
                    if (!is_noop_expr(node->init->lhs))
                        serialize_expr(f, vm, ctx, node->init->lhs, 0);
                } else {
                    serialize_expr(f, vm, ctx, node->init, 0);
                }
            }
            fprintf(f, "; ");
            // #1123: a wide-_BitInt cond can't appear bare here.
            if (node->cond &&
                !serialize_wide_bitint_truth(f, vm, ctx, node->cond))
                serialize_expr(f, vm, ctx, node->cond, 0);
            fprintf(f, "; ");
            if (node->inc)
                // #1235: the update clause discards its value -- drop the dead
                // `+ -1` term a postfix `i++` would otherwise leave here.
                serialize_discard_expr(f, vm, ctx, node->inc, 0);
            fprintf(f, ")\n");
            {
                // #1005: push a jump frame so a break/continue in the body
                // resolves back to this loop -- see the ND_GOTO arm below.
                SerJumpFrame frame = {ctx->jumps, node->brk_label,
                                      node->cont_label};
                ctx->jumps         = &frame;
                serialize_stmt(f, vm, ctx, node->then, indent + 1);
                ctx->jumps = frame.parent;
            }
            break;

        case ND_DO:
            print_indent_level(f, indent);
            fprintf(f, "do\n");
            {
                SerJumpFrame frame = {ctx->jumps, node->brk_label,
                                      node->cont_label};
                ctx->jumps         = &frame;
                serialize_stmt(f, vm, ctx, node->then, indent + 1);
                ctx->jumps = frame.parent;
            }
            print_indent_level(f, indent);
            fprintf(f, "while (");
            // #1123: a wide-_BitInt cond can't appear bare here.
            if (!serialize_wide_bitint_truth(f, vm, ctx, node->cond))
                serialize_expr(f, vm, ctx, node->cond, 0);
            fprintf(f, ");\n");
            break;

        case ND_SWITCH:
            // #1005: previously reconstructed the switch from the case_next
            // chain instead of serializing node->then -- that walked only the
            // first statement following each `case`/`default` (everything after
            // it, including the case's own `break`, was silently dropped),
            // emitted cases in reverse source order (case_next is prepended at
            // parse time) with `default:` always forced last (destroying
            // fallthrough), and dropped GNU case ranges entirely. Serializing
            // the real body -- mirroring ND_IF/ND_FOR's shape -- fixes all four
            // at once; ND_CASE below now emits its own label in place and lets
            // the enclosing ND_BLOCK's ->next walk handle subsequent
            // statements.
            print_indent_level(f, indent);
            fprintf(f, "switch (");
            serialize_expr(f, vm, ctx, node->cond, 0);
            fprintf(f, ")\n");
            {
                // A switch saves/restores only brk_label at parse time
                // (parse.c) -- cont_label is deliberately NULL here so a
                // `continue` inside this switch skips over this frame and
                // resolves to the nearest enclosing loop, not this switch.
                SerJumpFrame frame        = {ctx->jumps, node->brk_label, NULL};
                Node        *saved_switch = ctx->cur_switch;
                ctx->jumps                = &frame;
                ctx->cur_switch           = node;
                serialize_stmt(f, vm, ctx, node->then, indent + 1);
                ctx->cur_switch = saved_switch;
                ctx->jumps      = frame.parent;
            }
            break;

        case ND_GOTO:
            print_indent_level(f, indent);
            if (node->label) {
                // A real source-level `goto label;` (parse.c sets ->label to
                // the source identifier; resolve_goto_labels also fills in
                // ->unique_label, but ->label is authoritative here).
                fprintf(f, "goto %s;\n", node->label);
            } else {
                // #1005: break/continue lower to an ND_GOTO with only
                // ->unique_label set (parse.c) -- a ".L..N" string that is not
                // a valid C identifier and, unlike a source goto's target, has
                // no ND_LABEL anywhere to jump to. Resolve which construct it
                // targets by walking the jump-frame stack built above (pointer
                // identity, matching parse.c's own nn_find_target) and emit the
                // real C keyword instead of a (nonexistent) label reference.
                const char *kw = NULL;
                for (SerJumpFrame *fr = ctx->jumps; fr && !kw;
                     fr               = fr->parent) {
                    if (fr->brk_label && fr->brk_label == node->unique_label)
                        kw = "break";
                    else if (fr->cont_label &&
                             fr->cont_label == node->unique_label)
                        kw = "continue";
                }
                if (!kw)
                    error_tok(vm, node->tok,
                              "internal error: break/continue target not found "
                              "while serializing");
                fprintf(f, "%s;\n", kw);
            }
            break;

        case ND_LABEL:
            fprintf(f, "%s:\n", node->label);
            serialize_stmt(f, vm, ctx, node->lhs, indent);
            break;

        case ND_CASE:
            print_indent_level(f, indent);
            if (ctx->cur_switch && ctx->cur_switch->default_case == node) {
                fprintf(f, "default:\n");
            } else if (node->begin == node->end) {
                // #1095: re-materialize a host-owned sizeof/_Alignof label
                // the same way #1031's ND_NUM arm does -- a case label is
                // only ever compared against a runtime value, never fed
                // into a layout CCCC itself emits, so there's no
                // consistency hazard to guard here the way array
                // dimensions/initializers have. serialize_layout_const()
                // itself still declines (prints nothing, returns false)
                // when re-materializing isn't sound/possible, so the plain
                // folded value is the fallback exactly as before.
                fprintf(f, "case ");
                if (!serialize_layout_const(f, ctx, node->case_begin_layout_ty,
                                            node->case_begin_layout_is_align))
                    fprintf(f, "%ld", node->begin);
                fprintf(f, ":\n");
            } else {
                // [GNU] Case ranges, e.g. "case 1 ... 5:"
                fprintf(f, "case ");
                if (!serialize_layout_const(f, ctx, node->case_begin_layout_ty,
                                            node->case_begin_layout_is_align))
                    fprintf(f, "%ld", node->begin);
                fprintf(f, " ... ");
                if (!serialize_layout_const(f, ctx, node->case_end_layout_ty,
                                            node->case_end_layout_is_align))
                    fprintf(f, "%ld", node->end);
                fprintf(f, ":\n");
            }
            serialize_stmt(f, vm, ctx, node->lhs, indent);
            break;

        case ND_GOTO_EXPR:
            // [GNU] `goto *ptr`. Parsed by stmt() and consuming its own `;`, so
            // it is a statement here even though the audit files it with the
            // expression kinds.
            print_indent_level(f, indent);
            fprintf(f, "goto *(");
            serialize_expr(f, vm, ctx, node->lhs, 0);
            fprintf(f, ");\n");
            break;

        case ND_ASTORE:
            // Statement position discards the result, so the plain
            // void-returning call is enough -- no statement expression needed.
            // See the ND_ASTORE case in serialize_expr for the value-producing
            // form.
            print_indent_level(f, indent);
            if (atomic_serializable_pointee(node->lhs)) {
                fprintf(f, "__atomic_store_n(");
                // #1101: strip _Atomic from the operand's static type.
                serialize_atomic_addr(f, vm, ctx, node->lhs);
                fprintf(f, ", ");
                serialize_expr(f, vm, ctx, node->rhs, 2);
                fprintf(f, ", __ATOMIC_SEQ_CST);\n");
            } else {
                fprintf(f, "*(");
                serialize_expr(f, vm, ctx, node->lhs, 0);
                fprintf(f, ") = ");
                // #1042(b): `= rhs;` here is not wrapped in any enclosing
                // parens (unlike the expression-position ND_ASTORE case) -- an
                // unparenthesized top-level ND_COMMA in rhs would bind looser
                // than `=`, assigning the comma's *first* operand instead of
                // its value (the last operand), a silent wrong-answer bug, not
                // merely a compile error.
                serialize_expr(f, vm, ctx, node->rhs, 2);
                fprintf(f, ";\n");
            }
            break;

        case ND_ASM:
            // asm is the one construct deliberately emitted verbatim even
            // though the VM does not execute it by default (--asm-passthru opts
            // into VM execution): there is no way to evaluate host assembly in
            // the VM, so native output hands it to the host compiler. See
            // NATIVE.md.
            //
            // #1130: emit the __-wrapped __asm__ spelling, not bare asm --
            // asm is a GNU alternate keyword that GCC disables under a
            // strict ISO -std=cNN, turning this into a syntax error on a
            // real host compiler. __asm__ is accepted in every dialect,
            // matching the __typeof__/__extension__ spellings used
            // elsewhere in this file.
            print_indent_level(f, indent);
            fprintf(f, "__asm__(");
            if (node->asm_str)
                serialize_string_n(f, node->asm_str,
                                   (int)strlen(node->asm_str));
            else
                fprintf(f, "\"\"");
            fprintf(f, ");\n");
            break;

        default:
            // Treat as expression statement. #963c deliberately leaves this
            // default: arm alone: it is the legitimate route for every
            // expression-kind NodeKind reaching statement position (there is no
            // per-kind list to maintain here), not a fallback for an unhandled
            // kind. It now inherits serialize_expr's own hard error for any
            // kind that function doesn't recognize, so an unhandled kind still
            // fails loudly here -- it just fails one call deeper than it used
            // to, instead of this arm silently emitting a "comment + ;" null
            // statement (#963's original silent-miscompile symptom).
            print_indent_level(f, indent);
            serialize_expr(f, vm, ctx, node, 0);
            fprintf(f, ";\n");
            break;
    }
}

// #964: serialize one statement in a *list* context (a function body or an
// ND_BLOCK's own body) -- the one place a VLA-defining ND_BLOCK is safe to
// unbrace. declaration()'s ND_BLOCK wrapping (used to bundle a single `type
// v1, v2;` statement's per-declarator initializers) is not a real C block
// scope; the plain ND_BLOCK case in serialize_stmt() braces it like any
// other compound statement, which is harmless for an ordinary declaration
// (its variable is already hoisted, only initializer assignments remain
// inside) but would end a VLA's C-level scope right where it's declared.
// Only called from statement-list positions -- never the direct body of an
// if/else/loop/switch, where a declaration can't legally sit anyway (cccc
// itself already rejects e.g. `if (n) int v[n];`).
void serialize_stmt_list_item(FILE *f, VirtualMachine *vm,
                              SerializeContext *ctx, Node *node, int indent) {
    if (node && node->kind == ND_BLOCK && block_defines_vla(node)) {
        for (Node *s = node->body; s; s = s->next)
            serialize_stmt(f, vm, ctx, s, indent);
        return;
    }
    serialize_stmt(f, vm, ctx, node, indent);
}
