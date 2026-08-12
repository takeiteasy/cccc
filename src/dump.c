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

// AST dump for -a/--ast flag

#include "./internal.h"

static const char *node_kind_name(NodeKind kind) {
    switch (kind) {
    case ND_NULL_EXPR:     return "NULL_EXPR";
    case ND_ADD:           return "ADD";
    case ND_SUB:           return "SUB";
    case ND_MUL:           return "MUL";
    case ND_DIV:           return "DIV";
    case ND_NEG:           return "NEG";
    case ND_MOD:           return "MOD";
    case ND_BITAND:        return "BITAND";
    case ND_BITOR:         return "BITOR";
    case ND_BITXOR:        return "BITXOR";
    case ND_SHL:           return "SHL";
    case ND_SHR:           return "SHR";
    case ND_EQ:            return "EQ";
    case ND_NE:            return "NE";
    case ND_LT:            return "LT";
    case ND_LE:            return "LE";
    case ND_ASSIGN:        return "ASSIGN";
    case ND_COND:          return "COND";
    case ND_COMMA:         return "COMMA";
    case ND_MEMBER:        return "MEMBER";
    case ND_ADDR:          return "ADDR";
    case ND_DEREF:         return "DEREF";
    case ND_NOT:           return "NOT";
    case ND_BITNOT:        return "BITNOT";
    case ND_LOGAND:        return "LOGAND";
    case ND_LOGOR:         return "LOGOR";
    case ND_RETURN:        return "RETURN";
    case ND_IF:            return "IF";
    case ND_FOR:           return "FOR";
    case ND_DO:            return "DO";
    case ND_SWITCH:        return "SWITCH";
    case ND_CASE:          return "CASE";
    case ND_BLOCK:         return "BLOCK";
    case ND_GOTO:          return "GOTO";
    case ND_GOTO_EXPR:     return "GOTO_EXPR";
    case ND_LABEL:         return "LABEL";
    case ND_LABEL_VAL:     return "LABEL_VAL";
    case ND_FUNCALL:       return "FUNCALL";
    case ND_EXPR_STMT:     return "EXPR_STMT";
    case ND_STMT_EXPR:     return "STMT_EXPR";
    case ND_VAR:           return "VAR";
    case ND_VLA_PTR:       return "VLA_PTR";
    case ND_NUM:           return "NUM";
    case ND_CAST:          return "CAST";
    case ND_MEMZERO:       return "MEMZERO";
    case ND_ASM:           return "ASM";
    case ND_CAS:           return "CAS";
    case ND_EXCH:          return "EXCH";
    case ND_ALOAD:         return "ALOAD";
    case ND_ASTORE:        return "ASTORE";
    case ND_FRAME_ADDR:    return "FRAME_ADDR";
    case ND_RETURN_ADDR:   return "RETURN_ADDR";
    case ND_BLOCK_LITERAL: return "BLOCK_LITERAL";
    case ND_BLOCK_CALL:    return "BLOCK_CALL";
    case ND_MACRO_CALL:    return "MACRO_CALL";
    case ND_UNREACHABLE:   return "UNREACHABLE";
    case ND_BITOP:         return "BITOP";
    case ND_OVERFLOW_ARITH: return "OVERFLOW_ARITH";
    default:               return "UNKNOWN";
    }
}

static const char *type_kind_name(TypeKind kind) {
    switch (kind) {
    case TY_VOID:    return "void";
    case TY_BOOL:    return "bool";
    case TY_CHAR:    return "char";
    case TY_SHORT:   return "short";
    case TY_INT:     return "int";
    case TY_LONG:    return "long";
    case TY_FLOAT:   return "float";
    case TY_DOUBLE:  return "double";
    case TY_LDOUBLE: return "ldouble";
    case TY_ENUM:    return "enum";
    case TY_PTR:     return "pointer";
    case TY_FUNC:    return "function";
    case TY_ARRAY:   return "array";
    case TY_VLA:     return "vla";
    case TY_STRUCT:  return "struct";
    case TY_UNION:   return "union";
    case TY_BLOCK:   return "block";
    case TY_NULLPTR_T: return "nullptr_t";
    case TY_BITINT:  return "_BitInt";
    case TY_DECIMAL32:  return "_Decimal32";
    case TY_DECIMAL64:  return "_Decimal64";
    case TY_DECIMAL128: return "_Decimal128";
    case TY_ERROR:   return "error";
    default:         return "unknown";
    }
}



static void dump_type_simple(FILE *f, Type *ty) {
    if (!ty) {
        fprintf(f, "<null>");
        return;
    }

    if (ty->is_const)
        fprintf(f, "const ");
    if (ty->is_volatile)
        fprintf(f, "volatile ");

    switch (ty->kind) {
    case TY_PTR:
        dump_type_simple(f, ty->base);
        fprintf(f, "*");
        break;
    case TY_ARRAY:
        dump_type_simple(f, ty->base);
        fprintf(f, "[%d]", ty->array_len);
        break;
    case TY_FUNC:
        dump_type_simple(f, ty->return_ty);
        fprintf(f, "(");
        for (Type *p = ty->params; p; p = p->next) {
            dump_type_simple(f, p);
            if (p->next)
                fprintf(f, ", ");
        }
        if (ty->is_variadic)
            fprintf(f, ty->params ? ", ..." : "...");
        fprintf(f, ")");
        break;
    case TY_STRUCT:
    case TY_UNION: {
        fprintf(f, "%s", type_kind_name(ty->kind));
        // Prefer the tag name (struct_tag survives declarator name-overwrite,
        // #892) over ty->name, which is the *declarator* being typed against
        // this struct/union type -- e.g. `struct P p;` set ty->name to "p",
        // not "P", so printing ty->name here mis-spelled every tagged-type
        // REPL result (#666).
        Token *tag = ty->struct_tag ? ty->struct_tag : ty->name;
        if (tag) {
            fprintf(f, " ");
            fprintf(f, "%.*s", tag->len, tag->loc);
        }
        break;
    }
    case TY_ENUM: {
        fprintf(f, "%s", type_kind_name(ty->kind));
        Token *tag = ty->enum_tag ? ty->enum_tag : ty->name;
        if (tag) {
            fprintf(f, " ");
            fprintf(f, "%.*s", tag->len, tag->loc);
        }
        break;
    }
    default:
        if (ty->is_unsigned && (ty->kind == TY_CHAR || ty->kind == TY_SHORT ||
                                 ty->kind == TY_INT || ty->kind == TY_LONG))
            fprintf(f, "unsigned ");
        fprintf(f, "%s", type_kind_name(ty->kind));
        break;
    }
}

static void dump_type_verbose(FILE *f, Type *ty) {
    if (!ty) {
        fprintf(f, "<null>");
        return;
    }
    dump_type_simple(f, ty);
    fprintf(f, " [size=%d, align=%d", ty->size, ty->align);
    if (ty->is_atomic)
        fprintf(f, ", atomic");
    fprintf(f, "]");
}

static void dump_type(FILE *f, Type *ty, int verbose) {
    if (verbose)
        dump_type_verbose(f, ty);
    else
        dump_type_simple(f, ty);
}

static void dump_node(FILE *f, Node *node, int depth, int verbose);

static void dump_node_list(FILE *f, const char *label, Node *node, int depth, int verbose) {
    if (!node)
        return;
    print_indent(f, depth);
    fprintf(f, "%s:\n", label);
    for (Node *n = node; n; n = n->next)
        dump_node(f, n, depth + 1, verbose);
}

static void dump_obj_list(FILE *f, const char *label, Obj *obj, int depth, int verbose) {
    if (!obj)
        return;
    print_indent(f, depth);
    fprintf(f, "%s:\n", label);
    for (Obj *o = obj; o; o = o->next) {
        print_indent(f, depth + 1);
        fprintf(f, "%s :: ", o->name ? o->name : "<anon>");
        dump_type(f, o->ty, verbose);
        if (verbose) {
            if (o->is_param)
                fprintf(f, " [param]");
            if (o->offset)
                fprintf(f, " [offset=%d]", o->offset);
            if (o->align)
                fprintf(f, " [align=%d]", o->align);
        }
        fprintf(f, "\n");
    }
}

static void dump_node(FILE *f, Node *node, int depth, int verbose) {
    if (!node) {
        print_indent(f, depth);
        fprintf(f, "<null>\n");
        return;
    }

    print_indent(f, depth);
    fprintf(f, "%s", node_kind_name(node->kind));

    if (node->ty) {
        fprintf(f, " :: ");
        dump_type(f, node->ty, verbose);
    }

    switch (node->kind) {
    case ND_VAR:
        if (node->var && node->var->name)
            fprintf(f, " = %s", node->var->name);
        break;
    case ND_NUM:
        if (node->ty && is_flonum(node->ty))
            fprintf(f, " = %Lg", node->fval);
        else
            fprintf(f, " = %lld", (long long)node->val);
        break;
    case ND_MEMBER:
        if (node->member && node->member->name)
            fprintf(f, " = %.*s", node->member->name->len, node->member->name->loc);
        break;
    case ND_GOTO:
    case ND_LABEL:
        if (node->label)
            fprintf(f, " = %s", node->label);
        break;
    case ND_LABEL_VAL:
        if (node->label)
            fprintf(f, " = %s", node->label);
        break;
    case ND_ASM:
        if (node->asm_str)
            fprintf(f, " = \"%s\"", node->asm_str);
        break;
    case ND_MACRO_CALL:
        if (node->macro_name)
            fprintf(f, " = %s", node->macro_name);
        break;
    case ND_CASE:
        if (node->begin == node->end)
            fprintf(f, " = %ld", node->begin);
        else
            fprintf(f, " = %ld...%ld", node->begin, node->end);
        break;
    default:
        break;
    }

    if (verbose) {
        if (node->pass_by_stack)
            fprintf(f, " [pass_by_stack]");
    }

    fprintf(f, "\n");

    // Recurse into children
    switch (node->kind) {
    case ND_ADD: case ND_SUB: case ND_MUL: case ND_DIV: case ND_MOD:
    case ND_BITAND: case ND_BITOR: case ND_BITXOR:
    case ND_SHL: case ND_SHR:
    case ND_EQ: case ND_NE: case ND_LT: case ND_LE:
    case ND_ASSIGN: case ND_LOGAND: case ND_LOGOR: case ND_COMMA:
        dump_node(f, node->lhs, depth + 1, verbose);
        dump_node(f, node->rhs, depth + 1, verbose);
        break;

    case ND_NEG: case ND_NOT: case ND_BITNOT: case ND_ADDR: case ND_DEREF:
    case ND_CAST: case ND_RETURN: case ND_EXPR_STMT:
        dump_node(f, node->lhs, depth + 1, verbose);
        break;

    case ND_STMT_EXPR:
        dump_node_list(f, "body", node->body, depth + 1, verbose);
        break;

    case ND_COND:
        dump_node(f, node->cond, depth + 1, verbose);
        dump_node(f, node->then, depth + 1, verbose);
        dump_node(f, node->els, depth + 1, verbose);
        break;

    case ND_IF:
        dump_node(f, node->cond, depth + 1, verbose);
        dump_node(f, node->then, depth + 1, verbose);
        dump_node(f, node->els, depth + 1, verbose);
        break;

    case ND_FOR:
        dump_node(f, node->init, depth + 1, verbose);
        dump_node(f, node->cond, depth + 1, verbose);
        dump_node(f, node->inc, depth + 1, verbose);
        dump_node(f, node->then, depth + 1, verbose);
        break;

    case ND_DO:
        dump_node(f, node->then, depth + 1, verbose);
        dump_node(f, node->cond, depth + 1, verbose);
        break;

    case ND_BLOCK:
    case ND_BLOCK_LITERAL:
        dump_node_list(f, "body", node->body, depth + 1, verbose);
        break;

    case ND_SWITCH:
        dump_node(f, node->cond, depth + 1, verbose);
        dump_node(f, node->then, depth + 1, verbose);
        dump_node_list(f, "cases", node->case_next, depth + 1, verbose);
        if (node->default_case)
            dump_node(f, node->default_case, depth + 1, verbose);
        break;

    case ND_CASE:
        dump_node(f, node->lhs, depth + 1, verbose);
        break;

    case ND_FUNCALL: case ND_BLOCK_CALL:
        dump_node(f, node->lhs, depth + 1, verbose);
        dump_node_list(f, "args", node->args, depth + 1, verbose);
        break;

    case ND_MEMBER:
        dump_node(f, node->lhs, depth + 1, verbose);
        break;

    case ND_GOTO_EXPR:
        dump_node(f, node->lhs, depth + 1, verbose);
        break;

    case ND_LABEL:
        dump_node(f, node->lhs, depth + 1, verbose);
        break;

    case ND_CAS:
        dump_node(f, node->cas_addr, depth + 1, verbose);
        dump_node(f, node->cas_old, depth + 1, verbose);
        dump_node(f, node->cas_new, depth + 1, verbose);
        break;

    case ND_EXCH:
        if (node->atomic_addr)
            fprintf(f, " [addr=%s]", node->atomic_addr->name);
        dump_node(f, node->atomic_expr, depth + 1, verbose);
        break;

    case ND_ALOAD:
        dump_node(f, node->lhs, depth + 1, verbose);
        break;

    case ND_ASTORE:
        dump_node(f, node->lhs, depth + 1, verbose);
        dump_node(f, node->rhs, depth + 1, verbose);
        break;

    case ND_VLA_PTR:
    case ND_MEMZERO:
    case ND_FRAME_ADDR:
    case ND_RETURN_ADDR:
    case ND_NULL_EXPR:
    case ND_NUM:
    case ND_VAR:
    case ND_ASM:
    case ND_GOTO:
    case ND_LABEL_VAL:
    case ND_MACRO_CALL:
    case ND_UNREACHABLE:
        // Leaf-ish nodes: nothing more to dump
        break;

    default:
        // Fallback for any unhandled kinds
        if (node->lhs) dump_node(f, node->lhs, depth + 1, verbose);
        if (node->rhs) dump_node(f, node->rhs, depth + 1, verbose);
        break;
    }
}

static void dump_obj(FILE *f, Obj *obj, int verbose) {
    if (!obj)
        return;

    if (obj->is_function) {
        fprintf(f, "FUNCTION: %s :: ", obj->name ? obj->name : "<anon>");
        dump_type(f, obj->ty, verbose);
        fprintf(f, "\n");

        if (obj->is_inline)
            print_indent(f, 1), fprintf(f, "[inline]\n");
        if (obj->is_static)
            print_indent(f, 1), fprintf(f, "[static]\n");
        if (obj->is_nested)
            print_indent(f, 1), fprintf(f, "[nested depth=%d]\n", obj->nesting_depth);

        dump_obj_list(f, "params", obj->params, 1, verbose);
        dump_obj_list(f, "locals", obj->locals, 1, verbose);

        if (obj->body) {
            print_indent(f, 1);
            fprintf(f, "body:\n");
            for (Node *n = obj->body; n; n = n->next)
                dump_node(f, n, 2, verbose);
        }
    } else {
        fprintf(f, "GLOBAL: %s :: ", obj->name ? obj->name : "<anon>");
        dump_type(f, obj->ty, verbose);
        if (obj->is_static)
            fprintf(f, " [static]");
        if (obj->is_constexpr)
            fprintf(f, " [constexpr]");
        if (obj->is_tentative)
            fprintf(f, " [tentative]");
        fprintf(f, "\n");

        if (obj->init_expr) {
            print_indent(f, 1);
            fprintf(f, "init:\n");
            dump_node(f, obj->init_expr, 2, verbose);
        }
    }

    fprintf(f, "\n");
}

void cc_dump_ast(FILE *f, Obj *prog, int verbose) {
    if (!f || !prog)
        return;

    for (Obj *obj = prog; obj; obj = obj->next)
        dump_obj(f, obj, verbose);
}

// Public single-node entry point used by the reflection dump API (#58)
void cc_dump_node(FILE *f, Node *node, int verbose) {
    if (!f || !node)
        return;
    dump_node(f, node, 0, verbose);
}

// Public access to the kind-name table used by the AST generator (#58)
const char *cc_node_kind_name(NodeKind kind) {
    return node_kind_name(kind);
}

// Public entry point for printing a Type as C-ish declarator syntax (e.g.
// "int *", "struct Point", "unsigned long"). Used by the REPL's `:type`
// command and result formatter (ticket #661).
void cc_dump_type(FILE *f, Type *ty) {
    dump_type_simple(f, ty);
}

// ========================================================================
// Recursive value formatter (#666) -- shared between the REPL's expression
// result printer (src/repl.c) and, per the follow-up ticket filed alongside
// #666, the debugger's inspect/print commands (src/debugger.c). LLDB-style
// multi-line braces, e.g.:
//
//   (struct Outer) {
//     id = 7
//     pt = {
//       x = 1
//       y = 2
//     }
//     name = 0x1042 "bob"
//   }
//
// Neither entry point prints the "(type) " prefix or a trailing newline --
// the caller adds that, since the debugger wants "name = value" rather than
// "(type) value".
// ========================================================================

#define CC_DUMP_MAX_DEPTH 8         // nested aggregate depth before "{...}"
#define CC_DUMP_MAX_ARRAY_ELEMS 32  // array/vector elements before "..."
#define CC_DUMP_MAX_STRING_LEN 200  // bytes of a char*/char[] string before "..."

// Length-bounded C-style escaping, unlike json.c's print_escaped_string
// (JSON escaping, NUL-terminated input only) -- a char[] member may contain
// embedded NULs and is not JSON output.
static void dump_escaped_span(FILE *f, const char *data, size_t len) {
    fprintf(f, "\"");
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)data[i];
        switch (c) {
        case '"':  fprintf(f, "\\\""); break;
        case '\\': fprintf(f, "\\\\"); break;
        case '\n': fprintf(f, "\\n"); break;
        case '\r': fprintf(f, "\\r"); break;
        case '\t': fprintf(f, "\\t"); break;
        case '\0': fprintf(f, "\\0"); break;
        default:
            if (c < 0x20 || c == 0x7f)
                fprintf(f, "\\x%02x", c);
            else
                fprintf(f, "%c", c);
            break;
        }
    }
    fprintf(f, "\"");
}

// Reads a NUL-terminated string out of VM memory starting at `addr`,
// validating every byte's address before dereferencing it (cc_is_valid_vm_address)
// so a garbage/uninitialized char* prints as hex instead of crashing the
// REPL -- the case that motivated validating every depth uniformly, not just
// the top-level result (#666).
static void dump_c_string_at(FILE *f, VirtualMachine *vm, long long addr) {
    char buf[CC_DUMP_MAX_STRING_LEN];
    size_t n = 0;
    bool truncated = false;
    while (n < sizeof(buf)) {
        void *p = (void *)(intptr_t)(addr + (long long)n);
        if (!cc_is_valid_vm_address(vm, p)) {
            truncated = true;
            break;
        }
        char c = *(char *)p;
        if (c == '\0')
            break;
        buf[n++] = c;
    }
    if (n == sizeof(buf))
        truncated = true;
    dump_escaped_span(f, buf, n);
    if (truncated)
        fprintf(f, "...");
}

// Reads a `size`-byte integer at `addr`, zero/sign-extended to long long per
// `is_unsigned`. size is expected to be 1/2/4/8 (bitfield containers and
// scalar integer types); wider values (_BitInt > 64 bits) never reach here.
static long long read_int_from_mem(const void *addr, int size, bool is_unsigned) {
    long long raw = 0;
    size_t n = (size_t)(size < 8 ? size : 8);
    memcpy(&raw, addr, n);
    if (is_unsigned || size >= 8)
        return raw;
    int bits = size * 8;
    long long mask = 1LL << (bits - 1);
    return (raw ^ mask) - mask; // sign-extend
}

static double read_float_from_mem(const void *addr, int size) {
    if (size == 4) {
        float v;
        memcpy(&v, addr, sizeof(v));
        return (double)v;
    }
    // double and ldouble: ldouble is read the same as double here, matching
    // the existing register-based (fval) path's precision, which has always
    // treated TY_LDOUBLE as a plain double (see repl_print_result's
    // TY_FLOAT/TY_DOUBLE/TY_LDOUBLE case, unchanged by #666).
    double v;
    memcpy(&v, addr, sizeof(v));
    return v;
}

static long long read_bitfield(const void *struct_addr, Member *m) {
    const void *container = (const char *)struct_addr + m->offset;
    long long word = read_int_from_mem(container, m->ty->size, true);
    long long mask = (m->bit_width >= 64) ? ~0LL : ((1LL << m->bit_width) - 1);
    long long v = (word >> m->bit_offset) & mask;
    if (!m->ty->is_unsigned && m->bit_width < 64 && (v & (1LL << (m->bit_width - 1))))
        v -= (1LL << m->bit_width);
    return v;
}

static void dump_value_at(FILE *f, VirtualMachine *vm, Type *ty, const void *addr, int depth);

// Scalar/pointer leaf formatting, reading directly from VM memory at `addr`.
static void dump_scalar_at(FILE *f, VirtualMachine *vm, Type *ty, const void *addr) {
    switch (ty->kind) {
    case TY_BOOL: {
        long long v = read_int_from_mem(addr, ty->size, true);
        fprintf(f, "%s", v ? "true" : "false");
        return;
    }
    case TY_CHAR:
    case TY_SHORT:
    case TY_INT:
    case TY_LONG:
    case TY_ENUM: {
        long long v = read_int_from_mem(addr, ty->size, ty->is_unsigned);
        if (ty->is_unsigned)
            fprintf(f, "%llu", (unsigned long long)v);
        else
            fprintf(f, "%lld", v);
        return;
    }
    case TY_FLOAT:
    case TY_DOUBLE:
    case TY_LDOUBLE: {
        double v = read_float_from_mem(addr, ty->size == 4 ? 4 : 8);
        fprintf(f, "%g", v);
        return;
    }
    case TY_NULLPTR_T:
        fprintf(f, "NULL");
        return;
    case TY_PTR: {
        long long v = read_int_from_mem(addr, ty->size, true);
        if (v == 0) {
            fprintf(f, "NULL");
            return;
        }
        fprintf(f, "0x%llx", (unsigned long long)v);
        if (ty->base && ty->base->kind == TY_CHAR &&
            cc_is_valid_vm_address(vm, (void *)(intptr_t)v)) {
            fprintf(f, " ");
            dump_c_string_at(f, vm, v);
        }
        return;
    }
    case TY_BITINT:
        fprintf(f, "<_BitInt value: printing not yet supported>");
        return;
    case TY_DECIMAL32:
    case TY_DECIMAL64:
    case TY_DECIMAL128:
        fprintf(f, "<_Decimal value: printing not yet supported>");
        return;
    default:
        fprintf(f, "<value: printing not yet supported for this type>");
        return;
    }
}

// Recursive struct/union/array/vector aggregate formatting; falls through to
// dump_scalar_at for leaf types.
static void dump_value_at(FILE *f, VirtualMachine *vm, Type *ty, const void *addr, int depth) {
    if (!ty) {
        fprintf(f, "<null>");
        return;
    }

    switch (ty->kind) {
    case TY_STRUCT:
    case TY_UNION: {
        if (depth >= CC_DUMP_MAX_DEPTH) {
            fprintf(f, "{...}");
            return;
        }
        fprintf(f, "{\n");
        for (Member *m = ty->members; m; m = m->next) {
            // Flexible array member (`T x[];`, array_len == 0): no backing
            // storage in any one instance, nothing to read.
            if (m->ty->kind == TY_ARRAY && m->ty->array_len == 0)
                continue;
            print_indent(f, depth + 1);
            if (m->name) // anonymous struct/union members print unlabeled
                fprintf(f, "%.*s = ", m->name->len, m->name->loc);
            if (m->is_bitfield) {
                long long v = read_bitfield(addr, m);
                if (m->ty->is_unsigned)
                    fprintf(f, "%llu", (unsigned long long)v);
                else
                    fprintf(f, "%lld", v);
            } else {
                dump_value_at(f, vm, m->ty, (const char *)addr + m->offset, depth + 1);
            }
            fprintf(f, "\n");
        }
        print_indent(f, depth);
        fprintf(f, "}");
        return;
    }
    case TY_ARRAY: {
        int len = ty->array_len;
        if (ty->base && (ty->base->kind == TY_CHAR)) {
            // char[]: print as a string, bounded by the array length (not
            // necessarily NUL-terminated -- stop at the first NUL or the
            // array/print-cap length, whichever comes first).
            int cap = len < CC_DUMP_MAX_STRING_LEN ? len : CC_DUMP_MAX_STRING_LEN;
            int actual = 0;
            while (actual < cap && ((const char *)addr)[actual] != '\0')
                actual++;
            dump_escaped_span(f, (const char *)addr, (size_t)actual);
            if (cap < len)
                fprintf(f, "...");
            return;
        }
        if (depth >= CC_DUMP_MAX_DEPTH) {
            fprintf(f, "{...}");
            return;
        }
        fprintf(f, "{\n");
        int shown = len < CC_DUMP_MAX_ARRAY_ELEMS ? len : CC_DUMP_MAX_ARRAY_ELEMS;
        for (int i = 0; i < shown; i++) {
            print_indent(f, depth + 1);
            fprintf(f, "[%d] = ", i);
            dump_value_at(f, vm, ty->base, (const char *)addr + (size_t)i * (size_t)ty->base->size, depth + 1);
            fprintf(f, "\n");
        }
        if (shown < len) {
            print_indent(f, depth + 1);
            fprintf(f, "...\n");
        }
        print_indent(f, depth);
        fprintf(f, "}");
        return;
    }
    case TY_VECTOR: {
        if (depth >= CC_DUMP_MAX_DEPTH) {
            fprintf(f, "{...}");
            return;
        }
        fprintf(f, "{\n");
        int len = ty->vec_len;
        int shown = len < CC_DUMP_MAX_ARRAY_ELEMS ? len : CC_DUMP_MAX_ARRAY_ELEMS;
        for (int i = 0; i < shown; i++) {
            print_indent(f, depth + 1);
            fprintf(f, "[%d] = ", i);
            dump_value_at(f, vm, ty->base, (const char *)addr + (size_t)i * (size_t)ty->base->size, depth + 1);
            fprintf(f, "\n");
        }
        if (shown < len) {
            print_indent(f, depth + 1);
            fprintf(f, "...\n");
        }
        print_indent(f, depth);
        fprintf(f, "}");
        return;
    }
    case TY_VLA:
        fprintf(f, "<vla value: printing not yet supported>");
        return;
    default:
        dump_scalar_at(f, vm, ty, addr);
        return;
    }
}

// Memory-based entry point: addr points directly at a live value of type ty
// (a global/RETBUF-pool/heap address the caller already knows is valid).
// This is what the debugger follow-up will call with a local/global's frame
// address. Note: the REPL cannot use this for locals -- repl_exec_wrapper
// (src/repl.c) resets sp/bp before the result is printed, so any pointer
// into stack memory at print time is already stale; only RETBUF-pool,
// data-segment, and heap addresses are safe, which is all cc_dump_value_reg
// ever hands it.
void cc_dump_value(FILE *f, VirtualMachine *vm, Type *ty, const void *addr) {
    if (!ty)
        return;
    if (!addr) {
        fprintf(f, "<null>");
        return;
    }
    dump_value_at(f, vm, ty, addr, 0);
}

// REPL glue: ty's result came back in a register pair (ival/fval) rather
// than from a known address. For aggregate kinds, ival *is* the address
// (RETBUF pool for a returned struct/union/vector, data-segment address for
// an array lvalue) -- validated here before handing off to cc_dump_value.
void cc_dump_value_reg(FILE *f, VirtualMachine *vm, Type *ty, long long ival, double fval) {
    if (!ty)
        return;

    switch (ty->kind) {
    case TY_STRUCT:
    case TY_UNION:
    case TY_ARRAY:
    case TY_VECTOR:
        if (ival == 0 || !cc_is_valid_vm_address(vm, (void *)(intptr_t)ival)) {
            fprintf(f, "<invalid address 0x%llx>", (unsigned long long)ival);
            return;
        }
        cc_dump_value(f, vm, ty, (const void *)(intptr_t)ival);
        return;
    case TY_VLA:
        fprintf(f, "<vla value: printing not yet supported>");
        return;
    case TY_BITINT:
        fprintf(f, "<_BitInt value: printing not yet supported>");
        return;
    case TY_DECIMAL32:
    case TY_DECIMAL64:
    case TY_DECIMAL128:
        fprintf(f, "<_Decimal value: printing not yet supported>");
        return;
    case TY_BOOL:
        fprintf(f, "%s", ival ? "true" : "false");
        return;
    case TY_CHAR:
    case TY_SHORT:
    case TY_INT:
    case TY_LONG:
    case TY_ENUM:
        if (ty->is_unsigned)
            fprintf(f, "%llu", (unsigned long long)ival);
        else
            fprintf(f, "%lld", ival);
        return;
    case TY_FLOAT:
    case TY_DOUBLE:
    case TY_LDOUBLE:
        fprintf(f, "%g", fval);
        return;
    case TY_NULLPTR_T:
        fprintf(f, "NULL");
        return;
    case TY_PTR:
        if (ival == 0) {
            fprintf(f, "NULL");
            return;
        }
        fprintf(f, "0x%llx", (unsigned long long)ival);
        if (ty->base && ty->base->kind == TY_CHAR &&
            cc_is_valid_vm_address(vm, (void *)(intptr_t)ival)) {
            fprintf(f, " ");
            dump_c_string_at(f, vm, ival);
        }
        return;
    case TY_FUNC:
        fprintf(f, "0x%llx", (unsigned long long)ival);
        return;
    default:
        fprintf(f, "<value: printing not yet supported for this type>");
        return;
    }
}

// ========================================================================
// JSON AST dump
// ========================================================================

#define JSON_FIELD(f, d, first, name) do { \
    if (!(first)) fprintf((f), ",\n"); \
    (first) = false; \
    print_indent((f), (d)); \
    fprintf((f), "\"%s\": ", (name)); \
} while (0)

static void dump_ast_json_node(FILE *f, Node *node, int indent);

static void dump_ast_json_node_list(FILE *f, Node *node, int indent) {
    fprintf(f, "[\n");
    bool first = true;
    for (Node *n = node; n; n = n->next) {
        if (!first) fprintf(f, ",\n");
        first = false;
        print_indent(f, indent + 1);
        dump_ast_json_node(f, n, indent + 1);
    }
    fprintf(f, "\n");
    print_indent(f, indent);
    fprintf(f, "]");
}

static void print_token_name(FILE *f, Token *tok) {
    if (!tok || !tok->loc) {
        print_escaped_string(f, NULL);
        return;
    }
    char *name = malloc(tok->len + 1);
    if (!name) {
        print_escaped_string(f, NULL);
        return;
    }
    strncpy(name, tok->loc, tok->len);
    name[tok->len] = '\0';
    print_escaped_string(f, name);
    free(name);
}

static void dump_ast_json_var(FILE *f, Obj *obj, int indent) {
    int d = indent + 1;
    bool first = true;

    fprintf(f, "{\n");

    JSON_FIELD(f, d, first, "name");
    print_escaped_string(f, obj->name);

    JSON_FIELD(f, d, first, "type");
    serialize_type_json(f, obj->ty, d);

    fprintf(f, "\n");
    print_indent(f, indent);
    fprintf(f, "}");
}

static void dump_ast_json_var_list(FILE *f, Obj *obj, int indent) {
    fprintf(f, "[\n");
    bool first = true;
    for (Obj *o = obj; o; o = o->next) {
        if (!first) fprintf(f, ",\n");
        first = false;
        print_indent(f, indent + 1);
        dump_ast_json_var(f, o, indent + 1);
    }
    fprintf(f, "\n");
    print_indent(f, indent);
    fprintf(f, "]");
}

static void dump_ast_json_obj(FILE *f, Obj *obj, int indent) {
    int d = indent + 1;
    bool first = true;

    fprintf(f, "{\n");

    if (obj->is_function) {
        JSON_FIELD(f, d, first, "kind");
        fprintf(f, "\"function\"");

        JSON_FIELD(f, d, first, "name");
        print_escaped_string(f, obj->name);

        JSON_FIELD(f, d, first, "type");
        serialize_type_json(f, obj->ty, d);

        if (obj->is_inline) {
            JSON_FIELD(f, d, first, "is_inline");
            fprintf(f, "true");
        }
        if (obj->is_static) {
            JSON_FIELD(f, d, first, "is_static");
            fprintf(f, "true");
        }
        if (obj->is_nested) {
            JSON_FIELD(f, d, first, "is_nested");
            fprintf(f, "true");
        }

        if (obj->params) {
            JSON_FIELD(f, d, first, "params");
            dump_ast_json_var_list(f, obj->params, d);
        }
        if (obj->locals) {
            JSON_FIELD(f, d, first, "locals");
            dump_ast_json_var_list(f, obj->locals, d);
        }
        if (obj->body) {
            JSON_FIELD(f, d, first, "body");
            dump_ast_json_node_list(f, obj->body, d);
        }
    } else {
        JSON_FIELD(f, d, first, "kind");
        fprintf(f, "\"global\"");

        JSON_FIELD(f, d, first, "name");
        print_escaped_string(f, obj->name);

        JSON_FIELD(f, d, first, "type");
        serialize_type_json(f, obj->ty, d);

        if (obj->is_static) {
            JSON_FIELD(f, d, first, "is_static");
            fprintf(f, "true");
        }
        if (obj->is_constexpr) {
            JSON_FIELD(f, d, first, "is_constexpr");
            fprintf(f, "true");
        }
        if (obj->is_tentative) {
            JSON_FIELD(f, d, first, "is_tentative");
            fprintf(f, "true");
        }
        if (obj->init_expr) {
            JSON_FIELD(f, d, first, "init");
            dump_ast_json_node(f, obj->init_expr, d);
        }
    }

    fprintf(f, "\n");
    print_indent(f, indent);
    fprintf(f, "}");
}

static void dump_ast_json_node(FILE *f, Node *node, int indent) {
    if (!node) {
        fprintf(f, "null");
        return;
    }

    int d = indent + 1;
    bool first = true;

    fprintf(f, "{\n");

    JSON_FIELD(f, d, first, "kind");
    fprintf(f, "\"%s\"", node_kind_name(node->kind));

    JSON_FIELD(f, d, first, "type");
    serialize_type_json(f, node->ty, d);

    switch (node->kind) {
    case ND_VAR:
        JSON_FIELD(f, d, first, "var_name");
        print_escaped_string(f, node->var ? node->var->name : NULL);
        break;

    case ND_NUM:
        if (node->ty && is_flonum(node->ty)) {
            JSON_FIELD(f, d, first, "fval");
            fprintf(f, "%Lg", node->fval);
        } else {
            JSON_FIELD(f, d, first, "val");
            fprintf(f, "%lld", (long long)node->val);
        }
        break;

    case ND_MEMBER:
        JSON_FIELD(f, d, first, "member_name");
        print_token_name(f, node->member ? node->member->name : NULL);
        JSON_FIELD(f, d, first, "lhs");
        dump_ast_json_node(f, node->lhs, d);
        break;

    case ND_FUNCALL:
    case ND_BLOCK_CALL:
        JSON_FIELD(f, d, first, "func");
        dump_ast_json_node(f, node->lhs, d);
        if (node->args) {
            JSON_FIELD(f, d, first, "args");
            dump_ast_json_node_list(f, node->args, d);
        }
        break;

    case ND_BLOCK:
    case ND_BLOCK_LITERAL:
        if (node->body) {
            JSON_FIELD(f, d, first, "body");
            dump_ast_json_node_list(f, node->body, d);
        }
        break;

    case ND_STMT_EXPR:
        if (node->body) {
            JSON_FIELD(f, d, first, "body");
            dump_ast_json_node_list(f, node->body, d);
        }
        break;

    case ND_IF:
        JSON_FIELD(f, d, first, "cond");
        dump_ast_json_node(f, node->cond, d);
        JSON_FIELD(f, d, first, "then");
        dump_ast_json_node(f, node->then, d);
        if (node->els) {
            JSON_FIELD(f, d, first, "else");
            dump_ast_json_node(f, node->els, d);
        }
        break;

    case ND_FOR:
        if (node->init) {
            JSON_FIELD(f, d, first, "init");
            dump_ast_json_node(f, node->init, d);
        }
        if (node->cond) {
            JSON_FIELD(f, d, first, "cond");
            dump_ast_json_node(f, node->cond, d);
        }
        if (node->inc) {
            JSON_FIELD(f, d, first, "inc");
            dump_ast_json_node(f, node->inc, d);
        }
        JSON_FIELD(f, d, first, "then");
        dump_ast_json_node(f, node->then, d);
        break;

    case ND_DO:
        JSON_FIELD(f, d, first, "then");
        dump_ast_json_node(f, node->then, d);
        JSON_FIELD(f, d, first, "cond");
        dump_ast_json_node(f, node->cond, d);
        break;

    case ND_SWITCH:
        JSON_FIELD(f, d, first, "cond");
        dump_ast_json_node(f, node->cond, d);
        JSON_FIELD(f, d, first, "then");
        dump_ast_json_node(f, node->then, d);
        if (node->case_next) {
            JSON_FIELD(f, d, first, "cases");
            dump_ast_json_node_list(f, node->case_next, d);
        }
        if (node->default_case) {
            JSON_FIELD(f, d, first, "default");
            dump_ast_json_node(f, node->default_case, d);
        }
        break;

    case ND_CASE:
        JSON_FIELD(f, d, first, "begin");
        fprintf(f, "%ld", node->begin);
        if (node->begin != node->end) {
            JSON_FIELD(f, d, first, "end");
            fprintf(f, "%ld", node->end);
        }
        JSON_FIELD(f, d, first, "body");
        dump_ast_json_node(f, node->lhs, d);
        break;

    case ND_GOTO:
        JSON_FIELD(f, d, first, "label");
        print_escaped_string(f, node->label);
        break;

    case ND_LABEL:
        JSON_FIELD(f, d, first, "label");
        print_escaped_string(f, node->label);
        JSON_FIELD(f, d, first, "body");
        dump_ast_json_node(f, node->lhs, d);
        break;

    case ND_LABEL_VAL:
        JSON_FIELD(f, d, first, "label");
        print_escaped_string(f, node->label);
        break;

    case ND_RETURN:
        if (node->lhs) {
            JSON_FIELD(f, d, first, "value");
            dump_ast_json_node(f, node->lhs, d);
        }
        break;

    case ND_EXPR_STMT:
        if (node->lhs) {
            JSON_FIELD(f, d, first, "expr");
            dump_ast_json_node(f, node->lhs, d);
        }
        break;

    case ND_NEG:
    case ND_NOT:
    case ND_BITNOT:
    case ND_ADDR:
    case ND_DEREF:
    case ND_CAST:
        if (node->lhs) {
            JSON_FIELD(f, d, first, "lhs");
            dump_ast_json_node(f, node->lhs, d);
        }
        break;

    case ND_COND:
        JSON_FIELD(f, d, first, "cond");
        dump_ast_json_node(f, node->cond, d);
        JSON_FIELD(f, d, first, "then");
        dump_ast_json_node(f, node->then, d);
        if (node->els) {
            JSON_FIELD(f, d, first, "else");
            dump_ast_json_node(f, node->els, d);
        }
        break;

    case ND_ASSIGN:
    case ND_ADD:
    case ND_SUB:
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
    case ND_COMMA:
        JSON_FIELD(f, d, first, "lhs");
        dump_ast_json_node(f, node->lhs, d);
        JSON_FIELD(f, d, first, "rhs");
        dump_ast_json_node(f, node->rhs, d);
        break;

    case ND_GOTO_EXPR:
        JSON_FIELD(f, d, first, "target");
        dump_ast_json_node(f, node->lhs, d);
        break;

    case ND_ASM:
        JSON_FIELD(f, d, first, "asm_str");
        print_escaped_string(f, node->asm_str);
        break;

    case ND_MACRO_CALL:
        JSON_FIELD(f, d, first, "macro_name");
        print_escaped_string(f, node->macro_name);
        JSON_FIELD(f, d, first, "macro_arg_count");
        fprintf(f, "%d", node->macro_arg_count);
        break;

    case ND_CAS:
        JSON_FIELD(f, d, first, "cas_addr");
        dump_ast_json_node(f, node->cas_addr, d);
        JSON_FIELD(f, d, first, "cas_old");
        dump_ast_json_node(f, node->cas_old, d);
        JSON_FIELD(f, d, first, "cas_new");
        dump_ast_json_node(f, node->cas_new, d);
        break;

    case ND_EXCH:
        if (node->atomic_addr) {
            JSON_FIELD(f, d, first, "atomic_addr");
            print_escaped_string(f, node->atomic_addr->name);
        }
        JSON_FIELD(f, d, first, "atomic_expr");
        dump_ast_json_node(f, node->atomic_expr, d);
        break;

    case ND_VLA_PTR:
    case ND_MEMZERO:
    case ND_FRAME_ADDR:
    case ND_RETURN_ADDR:
    case ND_NULL_EXPR:
    case ND_UNREACHABLE:
        // Leaf nodes with no extra fields
        break;

    default:
        if (node->lhs) {
            JSON_FIELD(f, d, first, "lhs");
            dump_ast_json_node(f, node->lhs, d);
        }
        if (node->rhs) {
            JSON_FIELD(f, d, first, "rhs");
            dump_ast_json_node(f, node->rhs, d);
        }
        break;
    }

    fprintf(f, "\n");
    print_indent(f, indent);
    fprintf(f, "}");
}

#undef JSON_FIELD

void cc_dump_ast_json(FILE *f, Obj *prog, int verbose) {
    (void)verbose; // reserved for future use
    if (!f || !prog)
        return;

    fprintf(f, "[\n");
    bool first = true;
    for (Obj *obj = prog; obj; obj = obj->next) {
        if (!first) fprintf(f, ",\n");
        first = false;
        print_indent(f, 1);
        dump_ast_json_obj(f, obj, 1);
    }
    fprintf(f, "\n]\n");
}
