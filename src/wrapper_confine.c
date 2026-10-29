// Can an address a throw-away wrapper made (a string literal or static in its
// data, a block's code, a label address) still be reachable once it has run?
// Deny by default: only the node kinds below are accepted, so a kind added
// later is treated as escaping until it is listed here.
#include "./internal.h"
#include "./serialize_internal.h"

// FFI functions that keep none of the pointers or blocks they are given and
// return nothing that aliases a stored one. Sorted for bsearch. TODO: grow on
// demand; an unlisted function only costs the wrapper its reclamation.
static const char *const keeps_no_pointer[] = {
    "atoi",    "fprintf", "fputs",       "memchr",  "memcmp",  "printf",
    "putchar", "puts",    "strcasecmp",  "strchr",  "strcmp",  "strcoll",
    "strcspn", "strlen",  "strncasecmp", "strncmp", "strnlen", "strpbrk",
    "strrchr", "strspn",  "strstr",
};

static int name_cmp(const void *a, const void *b) {
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

static bool callee_keeps_no_pointer(const char *name) {
    if (!strncmp(name, "__builtin_", 10))
        name += 10;
    return bsearch(&name, keeps_no_pointer,
                   sizeof(keeps_no_pointer) / sizeof(*keeps_no_pointer),
                   sizeof(*keeps_no_pointer), name_cmp) != NULL;
}

typedef struct {
    bool ok;
} Confine;

static void scan(Node *root, Confine *c);
static void scan_fn(Obj *fn, Confine *c);

// Does `lv` name storage the wrapper itself owns (a local, a member or element
// of one)? The paused frame's locals and anything through a pointer do not.
static bool owned_lvalue(Node *lv) {
    if (!lv)
        return false;
    switch (lv->kind) {
        case ND_VAR:
            return lv->var->is_local && !lv->var->is_static && !lv->var->is_tls;
        case ND_VLA_PTR:
            return true;
        case ND_MEMBER:
            return owned_lvalue(lv->lhs);
        case ND_COMMA:
            return owned_lvalue(lv->rhs);
        case ND_DEREF: {
            if (lv->dbg_frame_local)
                return false;
            Node *p = lv->lhs;
            while (p &&
                   (p->kind == ND_ADD || p->kind == ND_SUB ||
                    (p->kind == ND_CAST && p->ty && p->ty->kind == TY_PTR)))
                p = p->lhs;
            return p && p->ty && p->ty->kind == TY_ARRAY && owned_lvalue(p);
        }
        default:
            return false;
    }
}

// A block literal, possibly behind the snapshot comma the debugger puts in
// front of one that captures paused-frame locals.
static Node *peel_comma(Node *n) {
    while (n && n->kind == ND_COMMA)
        n = n->rhs;
    return n;
}

static bool call_confined(Node *call) {
    Node *callee = call->lhs;
    if (!callee)
        return false;
    if (peel_comma(callee)->kind == ND_BLOCK_LITERAL)
        return true;
    if (callee->kind != ND_VAR || !callee->var->is_function ||
        obj_fn(callee->var)->body)
        return false;
    if (callee->var->is_builtin_alloca)
        return true;
    for (Node *a = call->args; a; a = a->next) {
        if (!a->ty)
            return false;
        if (!is_numeric(a->ty) && !callee_keeps_no_pointer(callee->var->name))
            return false;
    }
    return true;
}

static void visit(Node *n, void *vctx) {
    Confine *c = vctx;
    if (!c->ok)
        return;
    // A block value is only trusted when it was made here: invoking one that
    // came from outside runs code that was never checked.
    if (n->ty && n->ty->kind == TY_BLOCK && n->kind != ND_BLOCK_LITERAL &&
        n->kind != ND_ASSIGN && n->kind != ND_CAST && n->kind != ND_COMMA &&
        !(n->kind == ND_VAR && n->var->is_local)) {
        c->ok = false;
        return;
    }
    switch (n->kind) {
        case ND_NULL_EXPR:
        case ND_ADD:
        case ND_SUB:
        case ND_MUL:
        case ND_DIV:
        case ND_NEG:
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
        case ND_COND:
        case ND_COMMA:
        case ND_MEMBER:
        case ND_ADDR:
        case ND_DEREF:
        case ND_NOT:
        case ND_BITNOT:
        case ND_LOGAND:
        case ND_LOGOR:
        case ND_RETURN:
        case ND_IF:
        case ND_FOR:
        case ND_DO:
        case ND_SWITCH:
        case ND_CASE:
        case ND_BLOCK:
        case ND_GOTO:
        case ND_GOTO_EXPR:
        case ND_LABEL:
        case ND_LABEL_VAL:
        case ND_EXPR_STMT:
        case ND_STMT_EXPR:
        case ND_VLA_PTR:
        case ND_NUM:
        case ND_MEMZERO:
            break;
        case ND_VAR:
            // A cleanup handler gets the local's address.
            if (obj_attr(n->var)->cleanup_fn)
                c->ok = false;
            break;
        case ND_CAST:
            // A pointer laundered into an integer is no longer tracked.
            if (n->ty && is_integer(n->ty) && n->lhs && n->lhs->ty &&
                (n->lhs->ty->kind == TY_PTR || n->lhs->ty->kind == TY_ARRAY ||
                 n->lhs->ty->kind == TY_FUNC))
                c->ok = false;
            break;
        case ND_ASSIGN:
            c->ok = owned_lvalue(n->lhs);
            break;
        case ND_FUNCALL:
            c->ok = call_confined(n);
            break;
        case ND_BLOCK_CALL:
            c->ok =
                n->lhs && (peel_comma(n->lhs)->kind == ND_BLOCK_LITERAL ||
                           (n->lhs->kind == ND_VAR && n->lhs->var->is_local));
            break;
        case ND_BLOCK_LITERAL:
            // The block's body is not reached through the node's own children.
            scan_fn(node_misc(n)->block_fn, c);
            break;
        default:
            c->ok = false;
            break;
    }
}

static void scan(Node *root, Confine *c) {
    serialize_ast_walk(root, visit, c);
}

// A function's body and the functions nested in it.
static void scan_fn(Obj *fn, Confine *c) {
    if (!fn)
        return;
    scan(obj_fn(fn)->body, c);
    for (Obj *child = obj_fn(fn)->nested_children; child && c->ok;
         child      = obj_fn(child)->next_nested_sibling)
        scan_fn(child, c);
}

bool cc_wrapper_is_confined(VirtualMachine *vm, Obj *wrapper,
                            bool result_dropped) {
    (void)vm;
    if (!wrapper || !obj_fn(wrapper)->body)
        return false;
    Type *ret = wrapper->ty ? wrapper->ty->return_ty : NULL;
    if (!result_dropped && ret && ret->kind != TY_VOID && !is_numeric(ret))
        return false;
    Confine c = {.ok = true};
    scan(obj_fn(wrapper)->body, &c);
    return c.ok;
}
