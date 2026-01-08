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

// Compile a single pragma macro
// The macro is parsed using the existing scope which already has types from
// reflection.h (since user must #include <reflection.h> before #pragma macro)
static bool compile_single_pragma_macro(JCC *vm, PragmaMacro *pm) {
    if (vm->debug_vm) {
        printf("Compiling pragma macro '%s'...\n", pm->name);
        printf("  Macro body tokens:\n");
        for (Token *t = pm->body_tokens; t && t->kind != TK_EOF; t = t->next) {
            printf("    [%d] '%.*s'\n", t->kind, t->len, t->loc);
        }
    }

    // Save current parser state - keep scope/globals so types remain available
    Obj *saved_locals = vm->compiler.locals;
    Obj *saved_current_fn = vm->compiler.current_fn;
    Obj *saved_globals = vm->compiler.globals;

    // Enter macro mode
    vm->compiler.in_macro_mode = true;
    vm->compiler.locals = NULL;

    // Parse the macro function - scope already has reflection.h types
    Obj *macro_prog = parse(vm, pm->body_tokens);

    if (!macro_prog) {
        if (vm->debug_vm)
            fprintf(stderr, "Failed to parse pragma macro '%s'\n", pm->name);

        // Restore parser state
        vm->compiler.locals = saved_locals;
        vm->compiler.current_fn = saved_current_fn;
        vm->compiler.in_macro_mode = false;
        return false;
    }

    // Find the function in the parsed program
    Obj *func = NULL;
    for (Obj *obj = macro_prog; obj; obj = obj->next) {
        if (obj->is_function && strcmp(obj->name, pm->name) == 0) {
            func = obj;
            break;
        }
    }

    if (!func) {
        if (vm->debug_vm)
            fprintf(stderr,
                    "Could not find pragma macro function '%s' after parsing\n",
                    pm->name);

        // Restore parser state
        vm->compiler.locals = saved_locals;
        vm->compiler.current_fn = saved_current_fn;
        vm->compiler.in_macro_mode = false;
        return false;
    }

    // Initialize global variables (including string literals) in data segment
    // This must be done BEFORE gen_function since codegen references data_seg
    // addresses
    //
    // IMPORTANT: The globals list is in reverse order (most recently parsed
    // first). We need to initialize them in the correct (source) order so that
    // offsets match what the codegen expects.

    // Count globals
    int num_globals = 0;
    for (Obj *var = macro_prog; var; var = var->next) {
        if (!var->is_function)
            num_globals++;
    }

    if (num_globals > 0) {
        // Build array in reverse order (to get source order)
        Obj **globals_arr = alloca(num_globals * sizeof(Obj *));
        int idx = num_globals - 1;
        for (Obj *var = macro_prog; var; var = var->next) {
            if (!var->is_function)
                globals_arr[idx--] = var;
        }

        // Initialize in source order
        for (int i = 0; i < num_globals; i++) {
            Obj *var = globals_arr[i];

            // Align data pointer to 8-byte boundary
            long long offset = vm->data_ptr - vm->data_seg;
            offset = (offset + 7) & ~7;
            vm->data_ptr = vm->data_seg + offset;

            // Store the offset in the variable
            var->offset = vm->data_ptr - vm->data_seg;

            // Copy init_data if present (e.g., string literals)
            if (var->init_data) {
                memcpy(vm->data_ptr, var->init_data, var->ty->size);
            }

            vm->data_ptr += var->ty->size;
        }
    }

    // Compile just this function to bytecode
    gen_function(vm, func);

    // Store the compiled function
    pm->compiled_fn = func;
    pm->is_compiled = true;

    // Link macro's new globals (string literals, etc.) to the main program's
    // globals. Find the end of macro_prog list and link to saved_globals.
    if (macro_prog) {
        Obj *last = macro_prog;
        while (last->next)
            last = last->next;
        last->next = saved_globals;
    }
    // vm->compiler.globals now includes both macro globals and main program
    // globals

    // Restore parser state (keep the macro's compiled code in text segment)
    vm->compiler.locals = saved_locals;
    vm->compiler.current_fn = saved_current_fn;
    vm->compiler.in_macro_mode = false;

    if (vm->debug_vm)
        printf("Compiled pragma macro '%s' at code address %lld\n", pm->name,
               func->code_addr);

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

    for (PragmaMacro *pm = vm->compiler.pragma_macros; pm; pm = pm->next) {
        if (!compile_single_pragma_macro(vm, pm)) {
            fprintf(stderr, "Warning: Failed to compile pragma macro '%s'\n",
                    pm->name);
        }
    }
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
