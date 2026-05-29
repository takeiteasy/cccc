/*
 JCC: JIT C Compiler

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

// Pragma macro compilation and execution subsystem
// Compiles #pragma macro functions and expands macro calls in the AST

#include "./internal.h"

// External declaration from reflect.c
extern JCC *__jcc_current_vm;

// Forward declarations for reflection API functions (to register as FFI)
extern JCC *__jcc_get_vm(void);
extern Type *ast_find_type(JCC *vm, const char *name);
extern bool ast_type_exists(JCC *vm, const char *name);
extern Type *ast_get_type(JCC *vm, const char *name);
extern Node *ast_int_literal(JCC *vm, int64_t value);
extern Node *ast_float_literal(JCC *vm, double value);
extern Node *ast_string_literal(JCC *vm, const char *str);
extern Node *ast_var_ref(JCC *vm, const char *name);
extern Node *ast_binary(JCC *vm, NodeKind op, Node *left, Node *right);
extern Node *ast_unary(JCC *vm, NodeKind op, Node *operand);
extern Node *ast_cast(JCC *vm, Node *expr, Type *target_type);
extern Node *ast_return(JCC *vm, Node *expr);
extern Node *ast_if(JCC *vm, Node *cond, Node *then_body, Node *else_body);
extern Node *ast_switch(JCC *vm, Node *cond);
extern void ast_switch_add_case(JCC *vm, Node *switch_node, Node *value,
                                Node *body);
extern void ast_switch_set_default(JCC *vm, Node *switch_node, Node *body);
extern int ast_enum_count(JCC *vm, Type *enum_type);
extern EnumConstant *ast_enum_at(JCC *vm, Type *enum_type, int index);
extern const char *ast_enum_constant_name(EnumConstant *ec);
extern int ast_enum_constant_value(EnumConstant *ec);
extern Type *ast_make_pointer(JCC *vm, Type *base);
extern Type *ast_make_array(JCC *vm, Type *base, int len);

// Function generation
extern Obj *ast_function(JCC *vm, const char *name, Type *return_type);
extern void ast_function_add_param(JCC *vm, Obj *fn, const char *name,
                                   Type *type);
extern void ast_function_set_body(JCC *vm, Obj *fn, Node *body);
extern void ast_function_set_static(Obj *fn, bool is_static);
extern void ast_function_set_inline(Obj *fn, bool is_inline);
extern void ast_function_set_variadic(Obj *fn, bool is_variadic);
extern Node *ast_param_ref(JCC *vm, Obj *fn, const char *name);

// Register reflection API functions as FFI
static void register_reflection_ffi(JCC *vm) {
    // VM accessor
    cc_register_cfunc(vm, "__jcc_get_vm", (void *)__jcc_get_vm, 0, 0);

    // Type lookup
    cc_register_cfunc(vm, "ast_find_type", (void *)ast_find_type, 2, 0);
    cc_register_cfunc(vm, "ast_type_exists", (void *)ast_type_exists, 2, 0);
    cc_register_cfunc(vm, "ast_get_type", (void *)ast_get_type, 2, 0);

    // Type construction
    cc_register_cfunc(vm, "ast_make_pointer", (void *)ast_make_pointer, 2, 0);
    cc_register_cfunc(vm, "ast_make_array", (void *)ast_make_array, 3, 0);

    // Literal construction
    cc_register_cfunc(vm, "ast_int_literal", (void *)ast_int_literal, 2, 0);
    cc_register_cfunc(vm, "ast_float_literal", (void *)ast_float_literal, 2, 0);
    cc_register_cfunc(vm, "ast_string_literal", (void *)ast_string_literal, 2,
                      0);
    cc_register_cfunc(vm, "ast_var_ref", (void *)ast_var_ref, 2, 0);

    // Expression construction
    cc_register_cfunc(vm, "ast_binary", (void *)ast_binary, 4, 0);
    cc_register_cfunc(vm, "ast_unary", (void *)ast_unary, 3, 0);
    cc_register_cfunc(vm, "ast_cast", (void *)ast_cast, 3, 0);

    // Statement construction
    cc_register_cfunc(vm, "ast_return", (void *)ast_return, 2, 0);
    cc_register_cfunc(vm, "ast_if", (void *)ast_if, 4, 0);
    cc_register_cfunc(vm, "ast_switch", (void *)ast_switch, 2, 0);
    cc_register_cfunc(vm, "ast_switch_add_case", (void *)ast_switch_add_case, 4,
                      0);
    cc_register_cfunc(vm, "ast_switch_set_default",
                      (void *)ast_switch_set_default, 3, 0);

    // Enum reflection
    cc_register_cfunc(vm, "ast_enum_count", (void *)ast_enum_count, 2, 0);
    cc_register_cfunc(vm, "ast_enum_at", (void *)ast_enum_at, 3, 0);
    cc_register_cfunc(vm, "ast_enum_constant_name",
                      (void *)ast_enum_constant_name, 1, 0);
    cc_register_cfunc(vm, "ast_enum_constant_value",
                      (void *)ast_enum_constant_value, 1, 0);

    // Function generation
    cc_register_cfunc(vm, "ast_function", (void *)ast_function, 3, 0);
    cc_register_cfunc(vm, "ast_function_add_param",
                      (void *)ast_function_add_param, 4, 0);
    cc_register_cfunc(vm, "ast_function_set_body",
                      (void *)ast_function_set_body, 3, 0);
    cc_register_cfunc(vm, "ast_function_set_static",
                      (void *)ast_function_set_static, 2, 0);
    cc_register_cfunc(vm, "ast_function_set_inline",
                      (void *)ast_function_set_inline, 2, 0);
    cc_register_cfunc(vm, "ast_function_set_variadic",
                      (void *)ast_function_set_variadic, 2, 0);
    cc_register_cfunc(vm, "ast_param_ref", (void *)ast_param_ref, 3, 0);
}

static Token *copy_macro_token(JCC *vm, Token *tok) {
    Token *copy = arena_alloc(&vm->compiler.parser_arena, sizeof(Token));
    *copy = *tok;
    copy->next = NULL;
    copy->origin = tok;
    return copy;
}

static Token *new_macro_punct(JCC *vm, char *str, Token *tmpl) {
    Token *tok = arena_alloc(&vm->compiler.parser_arena, sizeof(Token));
    memset(tok, 0, sizeof(Token));
    tok->kind = TK_PUNCT;
    tok->loc = str;
    tok->len = strlen(str);
    if (tmpl) {
        tok->file = tmpl->file;
        tok->filename = tmpl->filename;
        tok->line_no = tmpl->line_no;
        tok->col_no = tmpl->col_no;
    }
    return tok;
}

static Token *new_macro_eof(JCC *vm, Token *tmpl) {
    Token *tok = arena_alloc(&vm->compiler.parser_arena, sizeof(Token));
    memset(tok, 0, sizeof(Token));
    tok->kind = TK_EOF;
    if (tmpl) {
        tok->loc = tmpl->loc;
        tok->len = tmpl->len;
        tok->file = tmpl->file;
        tok->filename = tmpl->filename;
        tok->line_no = tmpl->line_no;
        tok->col_no = tmpl->col_no;
    }
    return tok;
}

static Token *append_macro_prototype(JCC *vm, Token *cur, PragmaMacro *pm) {
    Token *last = pm->body_tokens;
    for (Token *tok = pm->body_tokens; tok && tok->kind != TK_EOF;
         tok = tok->next) {
        last = tok;
        if (equal(tok, "{"))
            break;
        cur = cur->next = copy_macro_token(vm, tok);
    }
    cur = cur->next = new_macro_punct(vm, ";", last);
    return cur;
}

static Token *append_macro_definition(JCC *vm, Token *cur, PragmaMacro *pm) {
    Token *last = pm->body_tokens;
    for (Token *tok = pm->body_tokens; tok && tok->kind != TK_EOF;
         tok = tok->next) {
        last = tok;
        cur = cur->next = copy_macro_token(vm, tok);
    }
    (void)last;
    return cur;
}

static Token *append_token_list(JCC *vm, Token *cur, Token *tokens) {
    for (Token *tok = tokens; tok && tok->kind != TK_EOF; tok = tok->next)
        cur = cur->next = copy_macro_token(vm, tok);
    return cur;
}

static Token *implicit_reflection_tokens(JCC *vm) {
    char *header = get_std_header("reflection.h");
    if (!header)
        error("could not load embedded reflection.h");

    Token *tokens = tokenize_string(vm, "<implicit-reflection.h>", header);
    return preprocess(vm, tokens);
}

static Token *build_combined_macro_tokens(JCC *vm, Token *reflection_tokens,
                                          PragmaMacro **macros, int count) {
    Token head = {};
    Token *cur = &head;

    cur = append_token_list(vm, cur, reflection_tokens);

    for (int i = 0; i < count; i++)
        cur = append_macro_prototype(vm, cur, macros[i]);
    for (int i = 0; i < count; i++)
        cur = append_macro_definition(vm, cur, macros[i]);

    Token *tmpl = count > 0 ? macros[count - 1]->body_tokens : NULL;
    cur->next = new_macro_eof(vm, tmpl);
    return head.next;
}

static Obj *find_macro_function(Obj *prog, const char *name) {
    for (Obj *obj = prog; obj; obj = obj->next) {
        if (obj->is_function && obj->body && strcmp(obj->name, name) == 0)
            return obj;
    }
    return NULL;
}

static void init_macro_globals(JCC *vm, Obj *macro_prog) {
    int num_globals = 0;
    for (Obj *var = macro_prog; var; var = var->next) {
        if (!var->is_function)
            num_globals++;
    }

    if (num_globals == 0)
        return;

    Obj **globals_arr = alloca(num_globals * sizeof(Obj *));
    int idx = num_globals - 1;
    for (Obj *var = macro_prog; var; var = var->next) {
        if (!var->is_function)
            globals_arr[idx--] = var;
    }

    for (int i = 0; i < num_globals; i++) {
        Obj *var = globals_arr[i];

        long long offset = vm->data_ptr - vm->data_seg;
        offset = (offset + 7) & ~7;
        vm->data_ptr = vm->data_seg + offset;
        var->offset = vm->data_ptr - vm->data_seg;

        if (var->init_data)
            memcpy(vm->data_ptr, var->init_data, var->ty->size);
        if (var->rel)
            error("pragma macro global relocations are not supported");

        vm->data_ptr += var->ty->size;
    }
}

static void patch_macro_call_addresses(JCC *vm, Obj *macro_prog) {
    for (int i = 0; i < vm->compiler.num_call_patches; i++) {
        Obj *fn = vm->compiler.call_patches[i].function;
        long long *loc = vm->compiler.call_patches[i].location;
        Obj *fn_def = find_macro_function(macro_prog, fn->name);
        if (!fn_def)
            error("undefined function in pragma macro bytecode: %s", fn->name);
        *loc = (long long)(vm->text_seg + fn_def->code_addr);
    }

    for (int i = 0; i < vm->compiler.num_func_addr_patches; i++) {
        Obj *fn = vm->compiler.func_addr_patches[i].function;
        long long *loc = vm->compiler.func_addr_patches[i].location;
        Obj *fn_def = find_macro_function(macro_prog, fn->name);
        if (!fn_def)
            error("undefined function address in pragma macro bytecode: %s",
                  fn->name);
        *loc = fn_def->code_addr * (long long)sizeof(long long);
    }
}

// Compile all pragma macros as one compile-time program so macro bytecode can
// make ordinary function calls to other pragma macros.
static bool compile_pragma_macro_program(JCC *vm) {
    int count = 0;
    for (PragmaMacro *pm = vm->compiler.pragma_macros; pm; pm = pm->next)
        count++;
    if (count == 0)
        return true;

    PragmaMacro **macros = alloca(count * sizeof(PragmaMacro *));
    int idx = count - 1;
    for (PragmaMacro *pm = vm->compiler.pragma_macros; pm; pm = pm->next)
        macros[idx--] = pm;

    Obj *saved_locals = vm->compiler.locals;
    Obj *saved_current_fn = vm->compiler.current_fn;
    Obj *saved_globals = vm->compiler.globals;
    int saved_num_call_patches = vm->compiler.num_call_patches;
    int saved_num_func_addr_patches = vm->compiler.num_func_addr_patches;

    vm->compiler.in_macro_mode = true;
    vm->compiler.locals = NULL;
    vm->compiler.num_call_patches = 0;
    vm->compiler.num_func_addr_patches = 0;

    Token *reflection_tokens = implicit_reflection_tokens(vm);
    Token *tokens =
        build_combined_macro_tokens(vm, reflection_tokens, macros, count);
    tokens = preprocess(vm, tokens);
    Obj *macro_prog = parse(vm, tokens);
    if (!macro_prog) {
        vm->compiler.locals = saved_locals;
        vm->compiler.current_fn = saved_current_fn;
        vm->compiler.globals = saved_globals;
        vm->compiler.in_macro_mode = false;
        vm->compiler.num_call_patches = saved_num_call_patches;
        vm->compiler.num_func_addr_patches = saved_num_func_addr_patches;
        return false;
    }

    for (int i = 0; i < count; i++) {
        Obj *func = find_macro_function(macro_prog, macros[i]->name);
        if (!func) {
            if (vm->debug_vm)
                fprintf(stderr,
                        "Could not find pragma macro function '%s' after parsing\n",
                        macros[i]->name);
            vm->compiler.locals = saved_locals;
            vm->compiler.current_fn = saved_current_fn;
            vm->compiler.globals = saved_globals;
            vm->compiler.in_macro_mode = false;
            vm->compiler.num_call_patches = saved_num_call_patches;
            vm->compiler.num_func_addr_patches = saved_num_func_addr_patches;
            return false;
        }
        macros[i]->compiled_fn = func;
        macros[i]->is_compiled = true;
    }

    init_macro_globals(vm, macro_prog);

    for (Obj *fn = macro_prog; fn; fn = fn->next) {
        if (fn->is_function && fn->body)
            gen_function(vm, fn);
    }

    patch_macro_call_addresses(vm, macro_prog);

    if (macro_prog) {
        Obj *last = macro_prog;
        while (last->next)
            last = last->next;
        last->next = saved_globals;
    }

    vm->compiler.locals = saved_locals;
    vm->compiler.current_fn = saved_current_fn;
    vm->compiler.in_macro_mode = false;
    vm->compiler.num_call_patches = saved_num_call_patches;
    vm->compiler.num_func_addr_patches = saved_num_func_addr_patches;

    if (vm->debug_vm) {
        for (int i = 0; i < count; i++) {
            printf("Compiled pragma macro '%s' at code address %lld\n",
                   macros[i]->name, macros[i]->compiled_fn->code_addr);
        }
    }

    return true;
}

// Compile all pragma macros
static void compile_all_pragma_macros(JCC *vm) {
    if (!vm->compiler.pragma_macros)
        return;

    if (vm->debug_vm)
        printf("Compiling %d pragma macro(s)...\n", ({
                   int n = 0;
                   for (PragmaMacro *p = vm->compiler.pragma_macros; p;
                        p = p->next)
                       n++;
                   n;
               }));

    // Register reflection API as FFI
    register_reflection_ffi(vm);

    if (!compile_pragma_macro_program(vm))
        fprintf(stderr, "Warning: Failed to compile pragma macros\n");
}

// Execute a pragma macro and return the generated AST node
static Node *execute_pragma_macro(JCC *vm, PragmaMacro *pm, Node *args,
                                  int arg_count) {
    if (!pm || !pm->is_compiled || !pm->compiled_fn)
        return NULL;

    if (vm->debug_vm)
        printf("Executing pragma macro '%s' with %d args...\n", pm->name,
               arg_count);

    // Set global VM pointer for __jcc_get_vm()
    __jcc_current_vm = vm;

    // Save VM execution state
    long long *saved_pc = vm->pc;
    long long *saved_sp = vm->sp;
    long long *saved_bp = vm->bp;
    long long saved_regs[NUM_REGS];
    memcpy(saved_regs, vm->regs, sizeof(saved_regs));

    // Reset stack for macro execution
    vm->sp = vm->initial_sp;
    vm->bp = vm->initial_bp;

    // Pass arguments via registers (REG_A0-A7 in the VM calling convention)
    // Arguments are Node* pointers to the AST nodes
    int arg_idx = 0;
    for (Node *arg = args; arg && arg_idx < 8; arg = arg->next) {
        vm->regs[REG_A0 + arg_idx] = (long long)arg;
        arg_idx++;
    }

    // Push sentinel return address (0) so we can detect when function
    // returns
    *(--vm->sp) = 0;

    // Set PC to function entry point
    vm->pc = vm->text_seg + pm->compiled_fn->code_addr;

    // Execute the macro function
    int saved_debug = vm->debug_vm;
    vm->debug_vm = 0; // Disable debug output during macro execution
    vm_eval(vm);
    vm->debug_vm = saved_debug;

    // Get the returned Node* from regs[REG_A0]
    Node *result = (Node *)vm->regs[REG_A0];

    // Clear VM pointer
    __jcc_current_vm = NULL;

    // Restore VM execution state
    vm->pc = saved_pc;
    vm->sp = saved_sp;
    vm->bp = saved_bp;
    memcpy(vm->regs, saved_regs, sizeof(saved_regs));

    if (vm->debug_vm && result)
        printf("Pragma macro '%s' returned node of kind %d\n", pm->name,
               result->kind);

    return result;
}

// Find pragma macro by name
static PragmaMacro *find_pragma_macro_by_name(JCC *vm, const char *name) {
    for (PragmaMacro *pm = vm->compiler.pragma_macros; pm; pm = pm->next) {
        if (strcmp(pm->name, name) == 0)
            return pm;
    }
    return NULL;
}

// Recursively transform macro calls in an AST node
static Node *transform_node(JCC *vm, Node *node);

static Node *transform_node(JCC *vm, Node *node) {
    if (!node)
        return NULL;

    // Handle ND_MACRO_CALL - this is where the magic happens
    if (node->kind == ND_MACRO_CALL) {
        if (vm->debug_vm)
            printf("  Found ND_MACRO_CALL for '%s'\n", node->macro_name);

        PragmaMacro *pm = find_pragma_macro_by_name(vm, node->macro_name);
        if (!pm) {
            error_tok(vm, node->tok, "undefined pragma macro: %s",
                      node->macro_name);
            return node;
        }

        if (!pm->is_compiled) {
            error_tok(vm, node->tok, "pragma macro '%s' failed to compile",
                      node->macro_name);
            return node;
        }

        // Execute the macro to get the replacement AST
        if (vm->debug_vm)
            printf("  Executing macro '%s'...\n", pm->name);

        Node *result =
            execute_pragma_macro(vm, pm, node->args, node->macro_arg_count);

        if (vm->debug_vm)
            printf("  Macro returned %p (kind=%d)\n", (void *)result,
                   result ? result->kind : -1);

        if (!result) {
            error_tok(vm, node->tok, "pragma macro '%s' returned NULL",
                      node->macro_name);
            // Return a placeholder
            Node *placeholder =
                arena_alloc(&vm->compiler.parser_arena, sizeof(Node));
            memset(placeholder, 0, sizeof(Node));
            placeholder->kind = ND_NUM;
            placeholder->val = 0;
            placeholder->ty = ty_int;
            placeholder->tok = node->tok;
            return placeholder;
        }

        // Run add_type on the result to ensure types are set
        add_type(vm, result);

        // Recursively transform in case the macro result contains more
        // macro calls
        return transform_node(vm, result);
    }

    // Recursively transform all child nodes
    node->lhs = transform_node(vm, node->lhs);
    node->rhs = transform_node(vm, node->rhs);
    node->cond = transform_node(vm, node->cond);
    node->then = transform_node(vm, node->then);
    node->els = transform_node(vm, node->els);
    node->init = transform_node(vm, node->init);
    node->inc = transform_node(vm, node->inc);

    // For ND_BLOCK, body is a chain of statements linked via ->next
    // We need to transform each statement in the chain
    if (node->body) {
        node->body = transform_node(vm, node->body);
        // Also transform sibling statements in the chain
        for (Node *stmt = node->body; stmt; stmt = stmt->next) {
            if (stmt->next) {
                stmt->next = transform_node(vm, stmt->next);
            }
        }
    }

    // Transform argument lists (also a chain)
    if (node->args) {
        node->args = transform_node(vm, node->args);
        for (Node *arg = node->args; arg && arg->next; arg = arg->next) {
            arg->next = transform_node(vm, arg->next);
        }
    }

    // Transform case lists for switch
    for (Node *c = node->case_next; c; c = c->case_next) {
        c->body = transform_node(vm, c->body);
    }
    if (node->default_case) {
        node->default_case->body = transform_node(vm, node->default_case->body);
    }

    return node;
}

// Initialize VM segments for macro compilation (extracted from cc_compile)
static void init_vm_segments_for_macros(JCC *vm) {
    if (vm->text_seg)
        return; // Already initialized

    // allocate memory
    if (!(vm->text_seg = malloc(vm->poolsize * sizeof(long long)))) {
        error("could not malloc for text area");
    }
    if (!(vm->data_seg = malloc(vm->poolsize))) {
        error("could not malloc for data area");
    }
    if (!(vm->stack_seg = malloc(vm->poolsize * sizeof(long long)))) {
        error("could not malloc for stack area");
    }
    if (!(vm->heap_seg = malloc(vm->poolsize))) {
        error("could not malloc for heap area");
    }

    // Allocate shadow stack for CFI if enabled
    if (vm->flags & JCC_CFI) {
        if (!(vm->shadow_stack = malloc(vm->poolsize * sizeof(long long)))) {
            error("could not malloc for shadow stack (CFI)");
        }
    }

    memset(vm->text_seg, 0, vm->poolsize * sizeof(long long));
    memset(vm->data_seg, 0, vm->poolsize);
    memset(vm->stack_seg, 0, vm->poolsize * sizeof(long long));
    memset(vm->heap_seg, 0, vm->poolsize);

    if (vm->flags & JCC_CFI) {
        memset(vm->shadow_stack, 0, vm->poolsize * sizeof(long long));
    }

    vm->old_text_seg = vm->text_seg;
    vm->text_ptr = vm->text_seg;
    vm->data_ptr = vm->data_seg;
    vm->heap_ptr = vm->heap_seg;
    vm->heap_end = vm->heap_seg + vm->poolsize;
    vm->free_list = NULL;

    // Initialize codegen state
    vm->compiler.current_codegen_fn = NULL;

    // Initialize stack pointers for macro execution
    vm->sp = vm->stack_seg + vm->poolsize - 1;
    vm->bp = vm->sp;
    vm->initial_sp = vm->sp;
    vm->initial_bp = vm->bp;
}

// Expand all pragma macro calls in the program
void cc_expand_pragma_macros(JCC *vm, Obj *prog) {
    if (!vm || !prog)
        return;

    // If no pragma macros were captured, nothing to do
    if (!vm->compiler.pragma_macros)
        return;

    if (vm->debug_vm)
        printf("Expanding pragma macros in program...\n");

    // Initialize VM segments if not already done (needed for macro
    // compilation)
    init_vm_segments_for_macros(vm);

    // Enter macro expansion mode
    vm->compiler.in_macro_expansion = true;

    // First, compile all pragma macros
    compile_all_pragma_macros(vm);

    // Then walk the AST and expand macro calls
    for (Obj *fn = prog; fn; fn = fn->next) {
        if (!fn->is_function || !fn->body)
            continue;

        if (vm->debug_vm)
            printf("Expanding macros in function '%s'...\n", fn->name);

        // Set current function context
        vm->compiler.current_fn = fn;

        // Transform the function body
        fn->body = transform_node(vm, fn->body);
    }

    // Also check global initializers
    for (Obj *var = prog; var; var = var->next) {
        if (var->is_function)
            continue;
        if (var->init_expr) {
            var->init_expr = transform_node(vm, var->init_expr);
        }
    }

    vm->compiler.in_macro_expansion = false;

    if (vm->debug_vm)
        printf("Pragma macro expansion complete.\n");
}
