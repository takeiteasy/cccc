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

// Interactive top-level read-eval-print loop (ticket #661), distinct from
// the breakpoint-time debugger REPL in src/debugger.c.
//
// Architecture summary (see man/TOOLING.md "Interactive REPL" for the
// user-facing description):
//   - One VM and one persistent global Scope live for the whole session.
//     parse() is called exactly once, on an empty token stream, to enter
//     the global scope and declare builtins; every later line is parsed
//     directly against that same scope (see cc_parse_repl_unit, src/parse.c)
//     so declarations accumulate exactly like typing more source into an
//     already-open translation unit.
//   - Each line is classified as a declaration or an expression by peeking
//     the first token (is_decl_start, consulting the live typedef table).
//   - Declarations are compiled incrementally with cc_repl_compile_new
//     (src/codegen.c), which never rewrites already-compiled globals or
//     functions -- unlike a full recompile, this keeps runtime mutations
//     and any addresses computed by earlier lines valid.
//   - Expressions are wrapped in a synthetic zero-argument function (built
//     via the same __builtin_ast_* API [[cccc::comptime]] macros use to
//     splice code, see src/reflection.c), compiled the same incremental
//     way, executed once on the VM, and its typed result is printed. The
//     wrapper Obj is then unlinked from vm->compiler.globals (its bytecode
//     is not reclaimed -- known limitation, ticket #667).
//   - A failed parse rolls back the scope/globals list to a snapshot taken
//     before the line was parsed, via vm->error_jmp_buf (see
//     cc_expr_snapshot/cc_expr_snapshot_restore, declared in cccc.h).

#include "./internal.h"
#include <setjmp.h>

#if defined(_WIN32)
#include <io.h>
#define CCCC_ISATTY _isatty
#define CCCC_FILENO _fileno
#else
#include <unistd.h>
#define CCCC_ISATTY isatty
#define CCCC_FILENO fileno
#endif

#ifdef HAVE_READLINE
#include <readline/history.h>
#include <readline/readline.h>
#endif

// AST-builder / gensym entry points, implemented in src/reflection.c. The
// public prototypes live in include/cccc/reflection.h, which is the header
// injected into *user* [[cccc::comptime]] programs; compiler-internal
// callers instead use local extern declarations, the same pattern
// src/macros.c uses for the same functions. Both read __builtin_current_vm
// internally now rather than taking a VirtualMachine* parameter; that global
// is seeded by cc_init for the whole compile (see src/vm.c), so it is valid
// here even though this call happens outside a macro-execution window.
extern const char *__builtin_gensym(const char *prefix);
extern Obj *__builtin_ast_function(const char *name, Type *return_type);
extern Node *__builtin_ast_return(Node *expr);
extern void __builtin_ast_function_set_body(Obj *fn, Node *body);

#define REPL_PROMPT      "cccc> "
#define REPL_CONT_PROMPT "  ... "

// ---------------------------------------------------------------------
// Line input: interactive (readline, or plain fgets when readline is not
// linked) or a file being fed in by :load.
// ---------------------------------------------------------------------

typedef struct {
    char *(*next)(void *ctx, const char *prompt); // malloc'd line, or NULL at EOF
    void *ctx;
} LineSource;

static char *repl_read_stdin_line(void *ctx, const char *prompt) {
    (void)ctx;
#ifdef HAVE_READLINE
    // Only hand input to readline when stdin is a real terminal -- on a
    // pipe/redirect it has no line-editing to offer and, worse, echoes back
    // every character it reads (there being no terminal driver doing that
    // for it), which would duplicate piped/scripted input in the output.
    if (CCCC_ISATTY(CCCC_FILENO(stdin))) {
        char *line = readline(prompt);
        if (line && *line)
            add_history(line);
        return line;
    }
#endif
    fputs(prompt, stdout);
    fflush(stdout);
    char buf[65536];
    if (!fgets(buf, sizeof(buf), stdin))
        return NULL;
    size_t len = strlen(buf);
    if (len > 0 && buf[len - 1] == '\n')
        buf[len - 1] = '\0';
    return strdup(buf);
}

static char *repl_read_file_line(void *ctx, const char *prompt) {
    (void)prompt;
    FILE *f = (FILE *)ctx;
    char buf[65536];
    if (!fgets(buf, sizeof(buf), f))
        return NULL;
    size_t len = strlen(buf);
    if (len > 0 && buf[len - 1] == '\n')
        buf[len - 1] = '\0';
    return strdup(buf);
}

// ---------------------------------------------------------------------
// Multi-line continuation: brace/paren/bracket balance, aware of strings,
// character literals, and comments so punctuation inside them is ignored.
// ---------------------------------------------------------------------

typedef struct {
    int depth;
    bool in_string, in_char, in_block_comment, escape;
} BalanceState;

// Re-scan the whole accumulated buffer from scratch each time (REPL input is
// small, so this is cheap) rather than trying to resume an incremental scan
// across line-append boundaries.
static void repl_scan_balance(BalanceState *st, const char *buf) {
    memset(st, 0, sizeof(*st));
    bool in_line_comment = false;
    for (const char *p = buf; *p; p++) {
        char c = *p;
        if (c == '\n') {
            in_line_comment = false;
            continue;
        }
        if (in_line_comment)
            continue;
        if (st->in_block_comment) {
            if (c == '*' && p[1] == '/') {
                st->in_block_comment = false;
                p++;
            }
            continue;
        }
        if (st->in_string) {
            if (st->escape) { st->escape = false; continue; }
            if (c == '\\') { st->escape = true; continue; }
            if (c == '"') st->in_string = false;
            continue;
        }
        if (st->in_char) {
            if (st->escape) { st->escape = false; continue; }
            if (c == '\\') { st->escape = true; continue; }
            if (c == '\'') st->in_char = false;
            continue;
        }
        if (c == '/' && p[1] == '/') { in_line_comment = true; p++; continue; }
        if (c == '/' && p[1] == '*') { st->in_block_comment = true; p++; continue; }
        if (c == '"') { st->in_string = true; continue; }
        if (c == '\'') { st->in_char = true; continue; }
        if (c == '{' || c == '(' || c == '[') st->depth++;
        else if (c == '}' || c == ')' || c == ']') st->depth--;
    }
}

// Read one logical unit of input: one or more lines, newline-joined,
// continuing while braces/parens/brackets are unbalanced, a string/char
// literal or block comment is still open, or the line ends with an explicit
// '\' continuation. Returns a malloc'd buffer (caller frees), or NULL at EOF
// with nothing accumulated yet.
//
// When allow_commands is set, a ':'-prefixed first line is returned as-is
// (verbatim, un-accumulated) so the caller can dispatch it as a session
// command; :load'ed files pass allow_commands=false so a stray ':' in a file
// is just parsed (and will error) rather than swallowed as a command.
static char *repl_read_unit(LineSource *src, bool interactive, bool allow_commands) {
    char *buf = NULL;
    size_t buf_len = 0;
    bool first = true;

    for (;;) {
        const char *prompt = interactive ? (first ? REPL_PROMPT : REPL_CONT_PROMPT) : "";
        char *line = src->next(src->ctx, prompt);
        if (!line) {
            if (!buf)
                return NULL;
            break; // EOF mid-continuation: submit whatever was accumulated
        }

        if (first && allow_commands && line[0] == ':') {
            free(buf);
            return line;
        }

        bool explicit_cont = false;
        size_t line_len = strlen(line);
        if (line_len > 0 && line[line_len - 1] == '\\') {
            explicit_cont = true;
            line[line_len - 1] = '\0';
            line_len--;
        }

        size_t sep = (buf_len > 0) ? 1 : 0; // room for the joining '\n'
        buf = realloc(buf, buf_len + sep + line_len + 1);
        if (sep) buf[buf_len] = '\n';
        memcpy(buf + buf_len + sep, line, line_len + 1);
        buf_len += sep + line_len;
        free(line);
        first = false;

        BalanceState st;
        repl_scan_balance(&st, buf);
        if (!explicit_cont && st.depth <= 0 && !st.in_string && !st.in_char &&
            !st.in_block_comment)
            break;
    }
    return buf;
}

// ---------------------------------------------------------------------
// Persistent global-scope snapshot / rollback (ticket #661).
//
// CcExprSnapshot/cc_expr_snapshot*/cc_expr_exec_wrapper are declared in
// cccc.h and shared with the debugger's conditional breakpoint evaluator
// (src/debugger.c, ticket 113), which faces the same problem: compile a
// synthesized one-off expression against the live scope, and either keep it
// or roll back on failure.
// ---------------------------------------------------------------------

// hashmap_snapshot/hashmap_restore (src/hashmap.c) assume an *owning* map --
// they deep-copy and later free each entry's key. The global scope's
// var_map/tag_map are populated via hashmap_put_borrowed/hashmap_put2_borrowed
// (see push_scope/push_tag_scope, src/parse.c), whose keys are arena pointers
// with the same borrowed-and-never-freed convention as leave_scope's
// hashmap_deinit_borrowed. Using the owning snapshot/restore on a borrowed
// map would try to free() an arena pointer and abort. These variants copy
// only the bucket *array* (which insertions do mutate/reallocate) and never
// touch key memory, matching the borrowed convention.
static HashMap repl_hashmap_snapshot_borrowed(const HashMap *map) {
    HashMap snap = *map;
    if (map->capacity > 0) {
        snap.buckets = malloc((size_t)map->capacity * sizeof(HashEntry));
        if (!snap.buckets) {
            fprintf(stderr, "FATAL: out of memory in repl_hashmap_snapshot_borrowed\n");
            exit(1);
        }
        memcpy(snap.buckets, map->buckets, (size_t)map->capacity * sizeof(HashEntry));
    }
    return snap;
}

static void repl_hashmap_discard_borrowed(HashMap *snap) {
    free(snap->buckets);
}

static void repl_hashmap_restore_borrowed(HashMap *map, HashMap snapshot) {
    free(map->buckets); // only the bucket array is owned; keys are borrowed
    *map = snapshot;
}

CcExprSnapshot cc_expr_snapshot(VirtualMachine *vm) {
    Scope *sc = vm->compiler.scope;
    CcExprSnapshot snap;
    snap.var_map_snap = repl_hashmap_snapshot_borrowed(&sc->var_map);
    snap.tag_map_snap = repl_hashmap_snapshot_borrowed(&sc->tag_map);
    snap.vars_head = sc->vars;
    snap.tags_head = sc->tags;
    snap.globals_head = vm->compiler.globals;
    snap.locals_head = vm->compiler.locals;
    return snap;
}

// Discard a snapshot taken for a unit that succeeded -- just frees the
// (now-unneeded) copied bucket array.
void cc_expr_snapshot_discard(CcExprSnapshot *snap) {
    repl_hashmap_discard_borrowed(&snap->var_map_snap);
    repl_hashmap_discard_borrowed(&snap->tag_map_snap);
}

// Restore scope/globals/locals to the state captured by cc_expr_snapshot,
// discarding anything the failed unit added. Obj/Node/scope-entry memory
// from the failed unit is arena-allocated and simply becomes unreachable
// rather than being freed (documented known limitation).
void cc_expr_snapshot_restore(VirtualMachine *vm, CcExprSnapshot *snap) {
    Scope *sc = vm->compiler.scope;
    repl_hashmap_restore_borrowed(&sc->var_map, snap->var_map_snap);
    repl_hashmap_restore_borrowed(&sc->tag_map, snap->tag_map_snap);
    sc->vars = snap->vars_head;
    sc->tags = snap->tags_head;
    vm->compiler.globals = snap->globals_head;
    vm->compiler.locals = snap->locals_head;
}

static void repl_print_pending_error(VirtualMachine *vm) {
    if (vm->error_message) {
        fputs(vm->error_message, stderr);
        vm->error_message = NULL;
    }
}

// ---------------------------------------------------------------------
// Expression execution: run a compiled zero-argument wrapper function once
// on the persistent VM and read its result, mirroring the save/reset/run/
// restore pattern execute_macro_fn (src/macros.c) uses for comptime macro
// bodies -- but reading the *runtime* return registers (REG_A0 / FREG_A0)
// rather than a comptime AST-node result.
// ---------------------------------------------------------------------

void cc_expr_exec_wrapper(VirtualMachine *vm, Obj *fn, long long *out_i,
                          double *out_f) {
    Pc saved_pc = vm->pc;
    long long *saved_sp = vm->sp;
    long long *saved_bp = vm->bp;
    long long saved_regs[NUM_REGS];
    memcpy(saved_regs, vm->regs, sizeof(saved_regs));
    FReg saved_fregs[NUM_REGS];
    memcpy(saved_fregs, vm->fregs, sizeof(saved_fregs));
    Obj *saved_current_fn = vm->compiler.current_fn;

    vm->sp = vm->initial_sp;
    vm->bp = vm->initial_bp;
    *(--vm->sp) = 0; // sentinel return address
    vm->pc = (Pc)fn->code_addr;

    cc_running_vm = vm;
    cccc_gil_acquire(vm);
    int rc = vm_eval(vm);
    cccc_gil_release(vm);
    cc_running_vm = NULL;

    if (rc == CCCC_HOST_SIGNAL_RC)
        fprintf(stderr, "note: evaluation raised a host signal (%d)\n",
                vm->dbg.host_fault_signal);

    if (out_i) *out_i = vm->regs[REG_A0];
    if (out_f) *out_f = vm->fregs[FREG_A0].f64;

    vm->pc = saved_pc;
    vm->sp = saved_sp;
    vm->bp = saved_bp;
    memcpy(vm->regs, saved_regs, sizeof(saved_regs));
    memcpy(vm->fregs, saved_fregs, sizeof(saved_fregs));
    vm->compiler.current_fn = saved_current_fn;
}

// ---------------------------------------------------------------------
// Result formatting (v1 scope: scalars, enums, float/double, pointers, and
// char* as strings; structs/unions/arrays print a placeholder -- rich
// aggregate printing is tracked as REPL follow-up work).
// ---------------------------------------------------------------------

static void repl_print_result(Type *ty, long long ival, double fval) {
    if (!ty || ty->kind == TY_VOID)
        return;

    switch (ty->kind) {
    case TY_BOOL:
        printf("(bool) %s\n", ival ? "true" : "false");
        return;
    case TY_CHAR:
    case TY_SHORT:
    case TY_INT:
    case TY_LONG:
    case TY_ENUM:
        printf("(");
        cc_dump_type(stdout, ty);
        printf(") ");
        if (ty->is_unsigned)
            printf("%llu\n", (unsigned long long)ival);
        else
            printf("%lld\n", ival);
        return;
    case TY_FLOAT:
    case TY_DOUBLE:
    case TY_LDOUBLE:
        printf("(");
        cc_dump_type(stdout, ty);
        printf(") %g\n", fval);
        return;
    case TY_NULLPTR_T:
        printf("(nullptr_t) NULL\n");
        return;
    case TY_PTR:
        printf("(");
        cc_dump_type(stdout, ty);
        printf(") ");
        if (ival == 0) {
            printf("NULL\n");
        } else if (ty->base && ty->base->kind == TY_CHAR) {
            printf("0x%llx \"%s\"\n", (unsigned long long)ival,
                   (const char *)(intptr_t)ival);
        } else {
            printf("0x%llx\n", (unsigned long long)ival);
        }
        return;
    case TY_FUNC:
        printf("(function) 0x%llx\n", (unsigned long long)ival);
        return;
    case TY_BITINT:
        // _BitInt values wider than 64 bits return via a multi-word buffer,
        // not a single register; printing them is not yet supported.
        printf("<_BitInt value: printing not yet supported>\n");
        return;
    case TY_STRUCT:
    case TY_UNION:
    case TY_ARRAY:
    case TY_VLA:
        // Recursive field/element formatting is follow-up work: ticket #666.
        printf("<");
        cc_dump_type(stdout, ty);
        printf(" value: aggregate printing not yet supported>\n");
        return;
    default:
        printf("<value: printing not yet supported for this type>\n");
        return;
    }
}

// ---------------------------------------------------------------------
// Session commands
// ---------------------------------------------------------------------

static void repl_print_help(void) {
    printf(
        "CCCC interactive REPL -- type C declarations or expressions.\n"
        "Declarations (variables, typedefs, structs, functions, ...) persist\n"
        "for the rest of the session. A bare expression is evaluated once and\n"
        "its typed result is printed.\n"
        "\n"
        "Session commands:\n"
        "  :help            show this message\n"
        "  :type <expr>     print the type of <expr> without evaluating it\n"
        "  :load <file>     read declarations/expressions from <file>\n"
        "  :quit            exit the REPL\n");
}

static void repl_cmd_type(VirtualMachine *vm, char *arg) {
    while (*arg == ' ' || *arg == '\t')
        arg++;
    if (!*arg) {
        fprintf(stderr, "usage: :type <expr>\n");
        return;
    }

    CcExprSnapshot snap = cc_expr_snapshot(vm);
    jmp_buf jb;
    jmp_buf *saved_jmp_buf = vm->error_jmp_buf;
    vm->error_jmp_buf = &jb;

    if (setjmp(jb) == 0) {
        Token *tok = tokenize_string(vm, "<repl:type>", arg);
        convert_pp_tokens(vm, tok);
        Token *rest;
        Node *n = cc_parse_expr(vm, &rest, tok);
        add_type(vm, n);
        vm->error_jmp_buf = saved_jmp_buf;
        cc_expr_snapshot_discard(&snap);
        printf("=> ");
        cc_dump_type(stdout, n->ty);
        printf("\n");
    } else {
        vm->error_jmp_buf = saved_jmp_buf;
        repl_print_pending_error(vm);
        cc_expr_snapshot_restore(vm, &snap);
    }
}

static void repl_process_unit(VirtualMachine *vm, const char *text);

static void repl_cmd_load(VirtualMachine *vm, char *arg) {
    while (*arg == ' ' || *arg == '\t')
        arg++;
    if (!*arg) {
        fprintf(stderr, "usage: :load <file>\n");
        return;
    }
    FILE *f = fopen(arg, "r");
    if (!f) {
        fprintf(stderr, "error: could not open '%s': %s\n", arg, strerror(errno));
        return;
    }

    LineSource src = { .next = repl_read_file_line, .ctx = f };
    for (;;) {
        char *unit = repl_read_unit(&src, /*interactive=*/false, /*allow_commands=*/false);
        if (!unit)
            break;
        repl_process_unit(vm, unit);
        free(unit);
    }
    fclose(f);
}

// ---------------------------------------------------------------------
// Core: parse + classify + (compile and) evaluate one unit of input.
// ---------------------------------------------------------------------

static void repl_process_unit(VirtualMachine *vm, const char *text) {
    CcExprSnapshot snap = cc_expr_snapshot(vm);
    jmp_buf jb;
    jmp_buf *saved_jmp_buf = vm->error_jmp_buf;
    vm->error_jmp_buf = &jb;

    if (setjmp(jb) == 0) {
        Token *tok = tokenize_string(vm, "<repl>", (char *)text);
        convert_pp_tokens(vm, tok);

        Node *expr_node = NULL;
        ReplUnitKind kind = cc_parse_repl_unit(vm, tok, &expr_node);

        switch (kind) {
        case REPL_UNIT_EMPTY:
            break;

        case REPL_UNIT_DECL:
            cc_repl_compile_new(vm, snap.globals_head);
            break;

        case REPL_UNIT_EXPR: {
            const char *name = __builtin_gensym("__repl_eval");
            Obj *fn = __builtin_ast_function(name, expr_node->ty);
            Node *ret = __builtin_ast_return(expr_node);
            __builtin_ast_function_set_body(fn, ret);
            cc_repl_compile_new(vm, snap.globals_head);

            long long ival = 0;
            double fval = 0.0;
            cc_expr_exec_wrapper(vm, fn, &ival, &fval);
            repl_print_result(expr_node->ty, ival, fval);

            // One-shot wrapper: drop it from the persistent globals list so
            // it is not recompiled by future units. Its bytecode/data stay
            // in the segments -- known limitation, ticket #667.
            vm->compiler.globals = snap.globals_head;
            break;
        }
        }

        vm->error_jmp_buf = saved_jmp_buf;
        cc_expr_snapshot_discard(&snap);
    } else {
        vm->error_jmp_buf = saved_jmp_buf;
        repl_print_pending_error(vm);
        cc_expr_snapshot_restore(vm, &snap);
    }
}

// ---------------------------------------------------------------------
// Session setup and main loop
// ---------------------------------------------------------------------

// Mirrors the stack-setup portion of cc_run_at (src/vm.c) -- reserving the
// full poolsize_max range but only committing poolsize, and recording
// initial_sp/initial_bp so repl_exec_wrapper can reset to a clean stack
// before every evaluation, exactly like execute_macro_fn does for comptime
// macro bodies.
static void repl_init_stack(VirtualMachine *vm) {
    size_t reserved_stack = (size_t)vm->poolsize_max * sizeof(long long);
    size_t initial_stack = (size_t)vm->poolsize * sizeof(long long);
    vm->sp = (long long *)((char *)vm->stack_seg + reserved_stack);
    vm->bp = vm->sp;
    vm->stack_base =
        (long long *)((char *)vm->stack_seg + reserved_stack - initial_stack);

    if (vm->flags & CCCC_CFI) {
        if (!vm->shadow_stack) {
            size_t stack_top_off = reserved_stack - initial_stack;
            vm->shadow_stack = (long long *)cccc_vm_reserve(reserved_stack);
            if (!vm->shadow_stack)
                error("could not reserve shadow stack");
            if (cccc_vm_commit(vm->shadow_stack, stack_top_off, initial_stack) != 0)
                error("could not commit shadow stack");
        }
        vm->shadow_sp = (long long *)((char *)vm->shadow_stack + reserved_stack);
    }

    vm->initial_sp = vm->sp;
    vm->initial_bp = vm->bp;
}

void cc_run_repl(VirtualMachine *vm) {
    // One-time session setup: enter the persistent global scope and declare
    // builtins by calling parse() on an empty token stream (mirrors how a
    // normal compile bootstraps global scope, but nothing is ever parsed
    // into it directly -- later units go through cc_parse_repl_unit
    // instead, which never resets vm->compiler.globals/scope).
    Token *eof_tok = tokenize_string(vm, "<repl-init>", "");
    convert_pp_tokens(vm, eof_tok);
    parse(vm, eof_tok);

    // Force segment allocation + reserve text_seg[0], same as the first
    // cc_compile() call would, then set up the VM stack once for the whole
    // session.
    cc_repl_compile_new(vm, NULL);
    repl_init_stack(vm);

    printf("CCCC interactive REPL. Type :help for session commands, :quit to exit.\n");

    LineSource src = { .next = repl_read_stdin_line, .ctx = NULL };
    bool interactive = CCCC_ISATTY(CCCC_FILENO(stdin));

    for (;;) {
        char *unit = repl_read_unit(&src, interactive, /*allow_commands=*/true);
        if (!unit) {
            if (interactive) printf("\n");
            break; // EOF
        }

        if (unit[0] == ':') {
            char *cmd = unit + 1;
            size_t cmd_len = strcspn(cmd, " \t");
            char *arg = cmd + cmd_len;

            if ((cmd_len == 4 && strncmp(cmd, "quit", 4) == 0) ||
                (cmd_len == 1 && cmd[0] == 'q')) {
                free(unit);
                break;
            } else if (cmd_len == 4 && strncmp(cmd, "help", 4) == 0) {
                repl_print_help();
            } else if (cmd_len == 4 && strncmp(cmd, "type", 4) == 0) {
                repl_cmd_type(vm, arg);
            } else if (cmd_len == 4 && strncmp(cmd, "load", 4) == 0) {
                repl_cmd_load(vm, arg);
            } else {
                fprintf(stderr, "unknown command: :%.*s (try :help)\n",
                        (int)cmd_len, cmd);
            }
            free(unit);
            continue;
        }

        // Blank/whitespace-only submission: nothing to do.
        const char *p = unit;
        while (*p == ' ' || *p == '\t' || *p == '\n') p++;
        if (*p) repl_process_unit(vm, unit);
        free(unit);
    }
}
