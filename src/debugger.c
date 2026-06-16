/*
 CCCC Debugger Implementation

 Copyright (C) 2025 George Watson

 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.
*/

#include "cccc.h"
#include "./internal.h"

#define STREQ_LIT(s, lit) (strncmp((s), (lit), sizeof(lit)) == 0)

static int is_valid_vm_address(VirtualMachine *vm, void *addr) {
    long long ptr = (long long)addr;
    // Text segment
    if (ptr >= (long long)vm->text_seg &&
        ptr < (long long)(vm->text_seg + vm->text_ptr + 1)) return 1;
    // Data segment
    if (ptr >= (long long)vm->data_seg && ptr < (long long)vm->data_ptr) return 1;
    // Heap segment
    if (ptr >= (long long)vm->heap_seg && ptr < (long long)vm->heap_ptr) return 1;
    // Stack segment (assuming stack grows up from stack_seg)
    if (ptr >= (long long)vm->stack_seg && ptr < (long long)(vm->stack_seg + vm->poolsize)) return 1;
    return 0;
}

static Obj *debugger_current_function(VirtualMachine *vm) {
    if (!vm || vm->pc == CCCC_INVALID_PC)
        return NULL;

    long long pc_offset = vm->pc;
    for (Obj *fn = vm->compiler.globals; fn; fn = fn->next) {
        if (!fn->is_function || !fn->body)
            continue;
        if (pc_offset >= fn->code_addr && pc_offset < fn->code_end_addr)
            return fn;
    }
    return NULL;
}

static void *debugger_symbol_address(VirtualMachine *vm, DebugSymbol *sym) {
    if (!sym)
        return NULL;
    if (sym->is_local) {
        // The canary one-slot shift is already baked into sym->offset by
        // assign_stack_offsets (#445), so no extra adjustment is needed here.
        return (void *)(vm->bp + sym->offset);
    }
    return (void *)(vm->data_seg + sym->offset);
}

static int debugger_resolve_watch_expr(VirtualMachine *vm, const char *expr, void **addr,
                                       int *size) {
    long long raw_addr;
    if (sscanf(expr, "%llx", &raw_addr) == 1) {
        *addr = (void *)raw_addr;
        *size = 8;
        return is_valid_vm_address(vm, *addr);
    }

    DebugSymbol *sym = cc_lookup_symbol(vm, expr);
    if (!sym)
        return 0;

    *addr = debugger_symbol_address(vm, sym);
    *size = sym->ty ? sym->ty->size : 8;
    return is_valid_vm_address(vm, *addr);
}

void debugger_init(VirtualMachine *vm) {
    vm->flags |= CCCC_ENABLE_DEBUGGER;  // Make sure it's enabled
    vm->dbg.num_breakpoints = 0;
    vm->dbg.num_watchpoints = 0;  // Initialize watchpoint counter
    vm->dbg.single_step = 0;
    vm->dbg.step_over = 0;
    vm->dbg.step_out = 0;
    vm->dbg.step_over_return_addr = CCCC_INVALID_PC;
    vm->dbg.step_out_bp = NULL;
    vm->dbg.debugger_attached = 0;

    // Initialize all breakpoints
    for (int i = 0; i < MAX_BREAKPOINTS; i++) {
        vm->dbg.breakpoints[i].pc = CCCC_INVALID_PC;
        vm->dbg.breakpoints[i].enabled = 0;
        vm->dbg.breakpoints[i].hit_count = 0;
        vm->dbg.breakpoints[i].condition = NULL;
    }

    // Initialize all watchpoints
    for (int i = 0; i < MAX_WATCHPOINTS; i++) {
        vm->dbg.watchpoints[i].address = NULL;
        vm->dbg.watchpoints[i].enabled = 0;
        vm->dbg.watchpoints[i].size = 0;
        vm->dbg.watchpoints[i].type = 0;
        vm->dbg.watchpoints[i].expr = NULL;
        vm->dbg.watchpoints[i].hit_count = 0;
        vm->dbg.watchpoints[i].old_value = 0;
    }
}

int cc_add_breakpoint(VirtualMachine *vm, Pc pc) {
    if (vm->dbg.num_breakpoints >= MAX_BREAKPOINTS) {
        printf("Error: Maximum number of breakpoints (%d) reached\n", MAX_BREAKPOINTS);
        return -1;
    }

    // Check if breakpoint already exists at this PC
    for (int i = 0; i < MAX_BREAKPOINTS; i++) {
        if (vm->dbg.breakpoints[i].enabled && vm->dbg.breakpoints[i].pc == pc) {
            printf("Breakpoint already exists at PC %u\n", pc);
            return i;
        }
    }

    // Find first available slot
    for (int i = 0; i < MAX_BREAKPOINTS; i++) {
        if (!vm->dbg.breakpoints[i].enabled) {
            vm->dbg.breakpoints[i].pc = pc;
            vm->dbg.breakpoints[i].enabled = 1;
            vm->dbg.breakpoints[i].hit_count = 0;
            vm->dbg.breakpoints[i].condition = NULL;
            vm->dbg.num_breakpoints++;

            // Calculate offset from text_seg for display
            printf("Breakpoint #%d set at PC %u\n", i, pc);
            return i;
        }
    }

    return -1;
}

void cc_remove_breakpoint(VirtualMachine *vm, int index) {
    if (index < 0 || index >= MAX_BREAKPOINTS) {
        printf("Error: Invalid breakpoint index %d\n", index);
        return;
    }

    if (!vm->dbg.breakpoints[index].enabled) {
        printf("Error: No breakpoint at index %d\n", index);
        return;
    }

    vm->dbg.breakpoints[index].enabled = 0;
    vm->dbg.breakpoints[index].pc = CCCC_INVALID_PC;
    vm->dbg.breakpoints[index].hit_count = 0;
    if (vm->dbg.breakpoints[index].condition) {
        free(vm->dbg.breakpoints[index].condition);
        vm->dbg.breakpoints[index].condition = NULL;
    }
    vm->dbg.num_breakpoints--;

    printf("Breakpoint #%d removed\n", index);
}

static int debugger_eval_condition(VirtualMachine *vm, const char *condition_str);

int debugger_check_breakpoint(VirtualMachine *vm) {
    // Safety check
    if (!vm) {
        return 0;
    }

    for (int i = 0; i < MAX_BREAKPOINTS; i++) {
        if (vm->dbg.breakpoints[i].enabled && vm->dbg.breakpoints[i].pc == vm->pc) {
            // Check condition if one exists
            char *cond = vm->dbg.breakpoints[i].condition;
            if (cond != NULL) {
                if (!debugger_eval_condition(vm, cond)) {
                    // Condition not met, don't trigger breakpoint
                    continue;
                }
            }

            vm->dbg.breakpoints[i].hit_count++;
            return 1;
        }
    }
    return 0;
}

void debugger_list_breakpoints(VirtualMachine *vm) {
    if (vm->dbg.num_breakpoints == 0) {
        printf("No breakpoints set.\n");
        return;
    }

    printf("\nBreakpoints:\n");
    printf("%-5s %-12s %-10s %-20s\n", "Num", "PC", "Hit Count", "Condition");
    printf("%-5s %-12s %-10s %-20s\n", "---", "--", "---------", "---------");

    for (int i = 0; i < MAX_BREAKPOINTS; i++) {
        if (vm->dbg.breakpoints[i].enabled) {
            printf("%-5d %-12u %-10d",
                   i,
                   vm->dbg.breakpoints[i].pc,
                   vm->dbg.breakpoints[i].hit_count);

            // Print condition if it exists
            if (vm->dbg.breakpoints[i].condition) {
                printf(" if %s", vm->dbg.breakpoints[i].condition);
            }
            printf("\n");
        }
    }
    printf("\n");
}

void debugger_print_registers(VirtualMachine *vm) {
    printf("\n=== Registers ===\n");
    printf("  A0 (return):  0x%016llx (%lld)\n", vm->regs[REG_A0], vm->regs[REG_A0]);
    printf("  FA0 (f64):    %f\n", cccc_freg_get_f64(vm, FREG_A0));
    printf("  pc:           %u\n", vm->pc);
    printf("  bp:           %p\n", (void*)vm->bp);
    printf("  sp:           %p\n", (void*)vm->sp);
    printf("  cycle:        %lld\n", vm->cycle);
    // Print first few general registers
    printf("  T0-T3:        %lld, %lld, %lld, %lld\n",
           vm->regs[REG_T0], vm->regs[REG_T1], vm->regs[REG_T2], vm->regs[REG_T3]);
    printf("\n");
}

void debugger_print_stack(VirtualMachine *vm, int count) {
    printf("\n=== Stack (top %d entries) ===\n", count);

    long long *sp = vm->sp;
    for (int i = 0; i < count; i++) {
        if (!is_valid_vm_address(vm, sp)) break;
        printf("  sp[%2d] = 0x%016llx  (%lld)\n", i, *sp, *sp);
        sp++;
    }
    printf("\n");
}

static const char* opcode_name(int op) {
    static const char *names[] = {
#define X(NAME, OPERANDS) #NAME,
        OPS_X
#undef X
    };
    if (op >= 0 && op < (int)(sizeof(names) / sizeof(names[0])))
        return names[op];
    return "UNKNOWN";
}

// Returns the number of words consumed by the instruction (including opcode)
static int disassemble_instruction(VirtualMachine *vm, Pc pc) {
    if (pc > vm->text_ptr) return 0;

    int op = (int)vm->text_seg[pc];
    const char *name = opcode_name(op);

    printf("%u: %-6s", pc, name);

    int size = cc_instr_words(op);
    if (size <= 0)
        size = 1;

    for (int i = 1; i < size && pc + (Pc)i <= vm->text_ptr; i++) {
        printf(" %u", vm->text_seg[pc + (Pc)i]);
    }
    if (op == JMPT && pc + 3 <= vm->text_ptr) {
        Pc table_pc = vm->text_seg[pc + 1];
        InstrWord count = vm->text_seg[pc + 2];
        if (table_pc == pc + 4 && table_pc + (Pc)count <= vm->text_ptr + 1)
            size += (int)count;
    }
    printf("\n");
    return size;
}

void cc_disassemble(VirtualMachine *vm) {
    if (!vm || !vm->text_seg) return;

    printf("=== Disassembly ===\n");
    // text_seg[0] is the entry point offset, not an instruction
    printf("Entry point: %u\n", vm->text_seg[0]);

    Pc pc = 1;
    while (pc <= vm->text_ptr) {
        int size = disassemble_instruction(vm, pc);
        if (size == 0) break;
        pc += size;
    }
    printf("===================\n");
}

void debugger_disassemble_current(VirtualMachine *vm) {
    if (vm->pc == CCCC_INVALID_PC || vm->pc > vm->text_ptr) {
        printf("PC out of text segment range\n");
        return;
    }
    disassemble_instruction(vm, vm->pc);
}

static void print_help(void) {
    printf("\n=== Debugger Commands ===\n");
    printf("\nBreakpoints:\n");
    printf("  break/b <line>           - Set breakpoint at line number in current file\n");
    printf("  break/b <file:line>      - Set breakpoint at file:line\n");
    printf("  break/b <function>       - Set breakpoint at function entry\n");
    printf("  break/b <offset>         - Set breakpoint at bytecode offset\n");
    printf("  break/b <location> if <expr> - Set conditional breakpoint (e.g., break 22 if x > 5)\n");
    printf("  delete/d <num>           - Delete breakpoint by number\n");
    printf("  list/l                   - List all breakpoints\n");
    printf("\nWatchpoints (Data Breakpoints):\n");
    printf("  watch/w <var|addr> - Break on write to variable or address\n");
    printf("  rwatch <addr>      - Break on read from address\n");
    printf("  awatch <addr>      - Break on read or write to address\n");
    printf("  info watch         - List all watchpoints\n");
    printf("\nExecution Control:\n");
    printf("  continue/c         - Continue execution\n");
    printf("  step/s             - Single step (into functions)\n");
    printf("  next/n             - Step over (skip function calls)\n");
    printf("  finish/f           - Step out (run until return)\n");
    printf("\nInspection:\n");
    printf("  registers/r        - Print register values\n");
    printf("  stack/st [count]   - Print stack (default 10 entries)\n");
    printf("  disasm/dis         - Disassemble current instruction\n");
    printf("  memory/m <addr>    - Inspect memory at address (hex)\n");
    printf("\nOther:\n");
    printf("  help/h/?           - Show this help\n");
    printf("  quit/q             - Exit debugger and program\n");
    printf("\n");
}

static void debugger_print_source_location(VirtualMachine *vm) {
    File *file = NULL;
    int line_no = 0;
    int col_no = 0;

    if (cc_get_source_location(vm, vm->pc, &file, &line_no, &col_no)) {
        printf("At %s:%d:%d\n", file->name ? file->name : "<unknown>", line_no, col_no);

        // Try to show source line if file contents available
        if (file->contents && line_no > 0) {
            // Find the line in file contents
            const char *p = file->contents;
            int current_line = 1;
            const char *line_start = p;

            while (*p && current_line <= line_no) {
                if (*p == '\n') {
                    if (current_line == line_no) {
                        // Print the line
                        int len = p - line_start;
                        printf("  %4d: %.*s\n", line_no, len, line_start);
                        break;
                    }
                    current_line++;
                    line_start = p + 1;
                }
                p++;
            }

            // If we didn't find a newline at end of file
            if (current_line == line_no && *p == '\0') {
                printf("  %4d: %s\n", line_no, line_start);
            }
        }
    } else {
        printf("No source location available for current PC\n");
    }
}

void cc_debug_repl(VirtualMachine *vm) {
    char line[256];
    char cmd[64];

    vm->dbg.debugger_attached = 1;

    printf("\n========================================\n");
    printf("    CCCC Debugger\n");
    printf("========================================\n");
    printf("Type 'help' or '?' for command list\n\n");

    debugger_print_registers(vm);
    debugger_print_source_location(vm);
    debugger_disassemble_current(vm);

    while (1) {
        printf("(cccc-dbg) ");
        fflush(stdout);

        if (!fgets(line, sizeof(line), stdin)) {
            break;
        }

        // Remove newline
        line[strcspn(line, "\n")] = 0;

        // Skip empty lines
        if (line[0] == '\0') {
            continue;
        }

        // Parse command
        if (sscanf(line, "%63s", cmd) != 1) {
            continue;
        }

        // Help command
        if (STREQ_LIT(cmd, "help") || STREQ_LIT(cmd, "h") || STREQ_LIT(cmd, "?")) {
            print_help();
        }
        // Continue
        else if (STREQ_LIT(cmd, "continue") || STREQ_LIT(cmd, "c")) {
            vm->dbg.single_step = 0;
            vm->dbg.step_over = 0;
            vm->dbg.step_out = 0;
            break;
        }
        // Single step
        else if (STREQ_LIT(cmd, "step") || STREQ_LIT(cmd, "s")) {
            vm->dbg.single_step = 1;
            vm->dbg.step_over = 0;
            vm->dbg.step_out = 0;
            break;
        }
        // Step over
        else if (STREQ_LIT(cmd, "next") || STREQ_LIT(cmd, "n")) {
            vm->dbg.single_step = 0;
            vm->dbg.step_over = 1;
            vm->dbg.step_out = 0;
            int op = (int)vm->text_seg[vm->pc];
            if (op == CALL || op == CALLT || op == CALLI) {
                // CALL/CALLI: return address is two words after the opcode.
                // (op word + one operand word). Stop there after the call
                // returns instead of stepping into the callee.
                vm->dbg.step_over_return_addr =
                    vm->pc + (Pc)cc_instr_words(op);
            } else {
                // Not a call instruction: nothing to skip, just single-step.
                vm->dbg.step_over = 0;
                vm->dbg.single_step = 1;
            }
            break;
        }
        // Step out
        else if (STREQ_LIT(cmd, "finish") || STREQ_LIT(cmd, "f")) {
            vm->dbg.single_step = 0;
            vm->dbg.step_over = 0;
            vm->dbg.step_out = 1;
            vm->dbg.step_out_bp = vm->bp;
            break;
        }
        // Print registers
        else if (STREQ_LIT(cmd, "registers") || STREQ_LIT(cmd, "r")) {
            debugger_print_registers(vm);
        }
        // Print stack
        else if (STREQ_LIT(cmd, "stack") || STREQ_LIT(cmd, "st")) {
            int count = 10;
            sscanf(line, "%*s %d", &count);
            debugger_print_stack(vm, count);
        }
        // Disassemble
        else if (STREQ_LIT(cmd, "disasm") || STREQ_LIT(cmd, "dis")) {
            debugger_disassemble_current(vm);
        }
        // Set breakpoint
        else if (STREQ_LIT(cmd, "break") || STREQ_LIT(cmd, "b")) {
            char arg[128];
            char *condition = NULL;
            Pc bp_pc = CCCC_INVALID_PC;

            // Try to parse arguments and extract condition if present
            // Format: break <location> [if <condition>]
            char *if_pos = strstr(line, " if ");
            if (if_pos) {
                // Extract condition (everything after " if ")
                condition = if_pos + 4;  // Skip " if "
                // Trim leading whitespace
                while (*condition && isspace(*condition)) condition++;
                if (*condition) {
                    condition = strdup(condition);  // Make a copy
                }
                // Temporarily null-terminate before " if " for location parsing
                *if_pos = '\0';
            }

            // Try to parse location argument
            if (sscanf(line, "%*s %127s", arg) == 1) {
                // Check if it's file:line format
                char *colon = strchr(arg, ':');
                if (colon) {
                    *colon = '\0';  // Split at colon
                    char *filename = arg;
                    int line_num = atoi(colon + 1);

                    // Find file (simple match on filename, not full path)
                    File *target_file = NULL;
                    for (int i = 0; vm->compiler.input_files && vm->compiler.input_files[i]; i++) {
                        if (strstr(vm->compiler.input_files[i]->name, filename)) {
                            target_file = vm->compiler.input_files[i];
                            break;
                        }
                    }

                    bp_pc = cc_find_pc_for_source(vm, target_file, line_num);
                    if (bp_pc == CCCC_INVALID_PC) {
                        printf("Error: Could not find code for %s:%d\n", filename, line_num);
                    }
                }
                // Check if it's a pure number (offset or line number)
                else if (isdigit(arg[0])) {
                    long long num = atoll(arg);

                    // If it's a small number, treat as line number in current file
                    if (num < 10000) {
                        File *current_file = NULL;
                        cc_get_source_location(vm, vm->pc, &current_file, NULL, NULL);
                        bp_pc = cc_find_pc_for_source(vm, current_file, num);
                        if (bp_pc == CCCC_INVALID_PC) {
                            printf("Error: Could not find code for line %lld\n", num);
                        }
                    } else {
                        // Large number, treat as bytecode instruction index.
                        bp_pc = (num >= 0 && num <= vm->text_ptr)
                                    ? (Pc)num
                                    : CCCC_INVALID_PC;
                        if (bp_pc == CCCC_INVALID_PC) {
                            printf("Error: Offset %lld is out of range\n", num);
                        }
                    }
                }
                // Otherwise treat as function name
                else {
                    bp_pc = cc_find_function_entry(vm, arg);
                    if (bp_pc == CCCC_INVALID_PC) {
                        printf("Error: Function '%s' not found\n", arg);
                    }
                }

                if (bp_pc != CCCC_INVALID_PC) {
                    int bp_idx = cc_add_breakpoint(vm, bp_pc);
                    // Set condition if provided
                    if (bp_idx >= 0 && condition) {
                        vm->dbg.breakpoints[bp_idx].condition = condition;
                        printf("Condition: %s\n", condition);
                        condition = NULL;  // Prevent double-free
                    }
                }
            } else {
                printf("Usage: break <line> | <file:line> | <function> | <offset> [if <condition>]\n");
            }

            // Clean up condition if not used
            if (condition) {
                free(condition);
            }
        }
        // Delete breakpoint
        else if (STREQ_LIT(cmd, "delete") || STREQ_LIT(cmd, "d")) {
            int num;
            if (sscanf(line, "%*s %d", &num) == 1) {
                cc_remove_breakpoint(vm, num);
            } else {
                printf("Usage: delete <breakpoint_number>\n");
            }
        }
        // List breakpoints
        else if (STREQ_LIT(cmd, "list") || STREQ_LIT(cmd, "l")) {
            debugger_list_breakpoints(vm);
        }
        // Memory inspection
        else if (STREQ_LIT(cmd, "memory") || STREQ_LIT(cmd, "m")) {
            long long addr;
            if (sscanf(line, "%*s %llx", &addr) == 1) {
                if (is_valid_vm_address(vm, (void*)addr) &&
                    is_valid_vm_address(vm, (void*)(addr + sizeof(long long) - 1))) {
                    long long value;
                    memcpy(&value, (void *)addr, sizeof(value));
                    printf("Memory at 0x%llx: 0x%016llx (%lld)\n",
                           addr, value, value);
                } else {
                    printf("Error: Invalid memory address 0x%llx\n", addr);
                }
            } else {
                printf("Usage: memory <hex_address>\n");
            }
        }
        // Watch (write watchpoint)
        else if (STREQ_LIT(cmd, "watch") || STREQ_LIT(cmd, "w")) {
            char expr[128];
            if (sscanf(line, "%*s %127s", expr) == 1) {
                void *addr = NULL;
                int size = 0;
                if (debugger_resolve_watch_expr(vm, expr, &addr, &size)) {
                    cc_add_watchpoint(vm, addr, size, WATCH_WRITE | WATCH_CHANGE, expr);
                } else {
                    printf("Error: Unable to resolve watch expression '%s'\n", expr);
                }
            } else {
                printf("Usage: watch <variable> | watch 0x<address>\n");
            }
        }
        // RWatch (read watchpoint)
        else if (STREQ_LIT(cmd, "rwatch")) {
            char expr[128];
            if (sscanf(line, "%*s %127s", expr) == 1) {
                void *addr = NULL;
                int size = 0;
                if (debugger_resolve_watch_expr(vm, expr, &addr, &size)) {
                    cc_add_watchpoint(vm, addr, size, WATCH_READ, expr);
                } else {
                    printf("Error: Unable to resolve watch expression '%s'\n", expr);
                }
            } else {
                printf("Usage: rwatch <variable> | rwatch 0x<address>\n");
            }
        }
        // AWatch (access watchpoint - read or write)
        else if (STREQ_LIT(cmd, "awatch")) {
            char expr[128];
            if (sscanf(line, "%*s %127s", expr) == 1) {
                void *addr = NULL;
                int size = 0;
                if (debugger_resolve_watch_expr(vm, expr, &addr, &size)) {
                    cc_add_watchpoint(vm, addr, size, WATCH_READ | WATCH_WRITE, expr);
                } else {
                    printf("Error: Unable to resolve watch expression '%s'\n", expr);
                }
            } else {
                printf("Usage: awatch <variable> | awatch 0x<address>\n");
            }
        }
        // Info watch (list watchpoints)
        else if (STREQ_LIT(cmd, "info")) {
            char subcmd[64];
            if (sscanf(line, "%*s %63s", subcmd) == 1 && STREQ_LIT(subcmd, "watch")) {
                if (vm->dbg.num_watchpoints == 0) {
                    printf("No watchpoints set.\n");
                } else {
                    printf("\nWatchpoints:\n");
                    printf("%-4s %-10s %-18s %-8s %s\n", "Num", "Type", "Address", "Hits", "Expression");
                    printf("------------------------------------------------------------\n");
                    for (int i = 0; i < MAX_WATCHPOINTS; i++) {
                        if (vm->dbg.watchpoints[i].enabled) {
                            const char *type_str = "";
                            int type = vm->dbg.watchpoints[i].type;
                            if ((type & WATCH_READ) && (type & WATCH_WRITE)) {
                                type_str = "access";
                            } else if (type & WATCH_WRITE) {
                                type_str = "write";
                            } else if (type & WATCH_READ) {
                                type_str = "read";
                            }
                            printf("%-4d %-10s %p       %-8d %s\n",
                                   i, type_str, vm->dbg.watchpoints[i].address,
                                   vm->dbg.watchpoints[i].hit_count,
                                   vm->dbg.watchpoints[i].expr ? vm->dbg.watchpoints[i].expr : "");
                        }
                    }
                    printf("\n");
                }
            } else {
                printf("Usage: info watch\n");
            }
        }
        // Quit
        else if (STREQ_LIT(cmd, "quit") || STREQ_LIT(cmd, "q")) {
            printf("Exiting debugger...\n");
            exit(0);
        }
        else {
            printf("Unknown command: %s\n", cmd);
            printf("Type 'help' or '?' for command list\n");
        }
    }

    vm->dbg.debugger_attached = 0;
}

int debugger_run(VirtualMachine *vm, int argc, char **argv) {
    // Find main function
    Obj *main_fn = NULL;
    for (Obj *obj = vm->compiler.globals; obj; obj = obj->next) {
        if (obj->is_function && obj->name &&
            STREQ_LIT(obj->name, "main")) {
            main_fn = obj;
            break;
        }
    }

    if (!main_fn) {
        printf("Error: main function not found\n");
        return -1;
    }

    printf("\n========================================\n");
    printf("    CCCC Debugger\n");
    printf("========================================\n");
    printf("Type 'help' for commands, 'c' to continue\n\n");

    // Override PC to main; cc_run_at already set up the stack, registers,
    // and sentinel return address correctly using poolsize_max.
    vm->pc = (Pc)main_fn->code_addr;

    printf("Starting debugger at main (PC: %u)\n", vm->pc);

    // Enter debugger at start
    cc_debug_repl(vm);

    // Main execution loop with debugger support
    // Call vm_eval which handles all the debugger hooks (breakpoints, stepping, etc.)
    return vm_eval(vm);
}

// ============================================================================
// Source Mapping Functions (for source-level debugging)
// ============================================================================

int cc_get_source_location(VirtualMachine *vm, Pc pc, File **out_file, int *out_line, int *out_col) {
    if (!(vm->flags & CCCC_ENABLE_DEBUGGER) || !vm->dbg.source_map || vm->dbg.source_map_count == 0) {
        return 0;
    }

    long long pc_offset = pc;

    // Binary search for the source mapping
    // Find the largest offset <= pc_offset
    int left = 0;
    int right = vm->dbg.source_map_count - 1;
    int best_idx = -1;

    while (left <= right) {
        int mid = left + (right - left) / 2;
        if (vm->dbg.source_map[mid].pc_offset <= pc_offset) {
            best_idx = mid;
            left = mid + 1;
        } else {
            right = mid - 1;
        }
    }

    if (best_idx == -1) {
        return 0;  // No mapping found
    }

    // Return the found mapping
    if (out_file) {
        *out_file = vm->dbg.source_map[best_idx].file;
    }
    if (out_line) {
        *out_line = vm->dbg.source_map[best_idx].line_no;
    }
    if (out_col) {
        *out_col = vm->dbg.source_map[best_idx].col_no;
    }

    return 1;
}

Pc cc_find_pc_for_source(VirtualMachine *vm, File *file, int line) {
    if (!(vm->flags & CCCC_ENABLE_DEBUGGER) || !vm->dbg.source_index || vm->dbg.source_index_count == 0) {
        return CCCC_INVALID_PC;
    }

    // When file is NULL (search any file), linear scan through the deduplicated
    // index — still faster than scanning the full source_map.
    if (!file) {
        for (int i = 0; i < vm->dbg.source_index_count; i++) {
            if (vm->dbg.source_index[i].line_no == line)
                return vm->dbg.source_index[i].first_pc;
        }
        return CCCC_INVALID_PC;
    }

    // Binary search by (file, line_no) in the sorted source_index
    uintptr_t ufile = (uintptr_t)file;
    int left = 0;
    int right = vm->dbg.source_index_count - 1;

    while (left <= right) {
        int mid = left + (right - left) / 2;
        SourceIndex *entry = &vm->dbg.source_index[mid];

        if (entry->file == file && entry->line_no == line)
            return entry->first_pc;

        if ((uintptr_t)entry->file < ufile ||
            ((uintptr_t)entry->file == ufile && entry->line_no < line)) {
            left = mid + 1;
        } else {
            right = mid - 1;
        }
    }

    return CCCC_INVALID_PC;
}

Pc cc_find_function_entry(VirtualMachine *vm, const char *name) {
    if (!name) {
        return CCCC_INVALID_PC;
    }

    // Search through global symbols for function
    for (Obj *fn = vm->compiler.globals; fn; fn = fn->next) {
        if (fn->is_function && fn->name && strlen(fn->name) == strlen(name) &&
            strncmp(fn->name, name, strlen(name)) == 0) {
            if (fn->code_addr >= 0) {
                return (Pc)fn->code_addr;
            }
        }
    }

    return CCCC_INVALID_PC;
}

DebugSymbol *cc_lookup_symbol(VirtualMachine *vm, const char *name) {
    if (!(vm->flags & CCCC_ENABLE_DEBUGGER) || !name) {
        return NULL;
    }

    Obj *current_fn = debugger_current_function(vm);

    // Search locals in the current function first.
    for (int i = vm->dbg.num_debug_symbols - 1; i >= 0; i--) {
        if (vm->dbg.debug_symbols[i].name &&
            vm->dbg.debug_symbols[i].is_local &&
            vm->dbg.debug_symbols[i].owner_fn == current_fn &&
            strlen(vm->dbg.debug_symbols[i].name) == strlen(name) &&
            strncmp(vm->dbg.debug_symbols[i].name, name, strlen(name)) == 0) {
            return &vm->dbg.debug_symbols[i];
        }
    }

    // Then fall back to globals.
    for (int i = vm->dbg.num_debug_symbols - 1; i >= 0; i--) {
        if (vm->dbg.debug_symbols[i].name &&
            !vm->dbg.debug_symbols[i].is_local &&
            strlen(vm->dbg.debug_symbols[i].name) == strlen(name) &&
            strncmp(vm->dbg.debug_symbols[i].name, name, strlen(name)) == 0) {
            return &vm->dbg.debug_symbols[i];
        }
    }

    return NULL;
}

// ============================================================================
// Condition Evaluator for Conditional Breakpoints
// ============================================================================

static long long eval_ast_node(VirtualMachine *vm, Node *node, int *error);

static int debugger_find_ffi_function(VirtualMachine *vm, const char *name) {
    if (!vm || !name)
        return -1;

    size_t len = strlen(name);
    for (int i = 0; i < vm->compiler.ffi_count; i++) {
        if (vm->compiler.ffi_table[i].name &&
            vm->compiler.ffi_table[i].name_len == len &&
            memcmp(vm->compiler.ffi_table[i].name, name, len) == 0)
            return i;
    }

    return -1;
}

static void debugger_add_condition_scope_var(VarScopeNode **vars, Obj *var) {
    if (!var || !var->name || !*var->name)
        return;

    VarScopeNode *node = calloc(1, sizeof(VarScopeNode));
    if (!node)
        error("out of memory");
    node->var = var;
    node->name = var->name;
    node->name_len = strlen(var->name);
    node->next = *vars;
    *vars = node;
}

static void debugger_free_condition_scope(VarScopeNode *vars) {
    while (vars) {
        VarScopeNode *next = vars->next;
        free(vars);
        vars = next;
    }
}

static int debugger_type_is_scalar_integer(Type *ty) {
    return ty && (is_integer(ty) || ty->kind == TY_PTR || ty->kind == TY_FUNC ||
                  ty->kind == TY_NULLPTR_T);
}

static long long debugger_cast_integer(long long val, Type *ty) {
    if (!ty)
        return val;
    if (ty->kind == TY_BOOL)
        return val != 0;
    if (!is_integer(ty))
        return val;

    switch (ty->size) {
    case 1:
        return ty->is_unsigned ? (long long)(uint8_t)val : (long long)(int8_t)val;
    case 2:
        return ty->is_unsigned ? (long long)(uint16_t)val : (long long)(int16_t)val;
    case 4:
        return ty->is_unsigned ? (long long)(uint32_t)val : (long long)(int32_t)val;
    default:
        return val;
    }
}

static long long debugger_read_scalar(void *addr, Type *ty) {
    if (!addr)
        return 0;
    if (!ty)
        return *(long long *)addr;

    switch (ty->kind) {
    case TY_BOOL:
        return *(uint8_t *)addr != 0;
    case TY_CHAR:
        return ty->is_unsigned ? (long long)*(uint8_t *)addr
                               : (long long)*(int8_t *)addr;
    case TY_SHORT:
        return ty->is_unsigned ? (long long)*(uint16_t *)addr
                               : (long long)*(int16_t *)addr;
    case TY_INT:
    case TY_ENUM:
        return ty->is_unsigned ? (long long)*(uint32_t *)addr
                               : (long long)*(int32_t *)addr;
    default:
        return *(long long *)addr;
    }
}

static void debugger_write_scalar(void *addr, Type *ty, long long val) {
    if (!addr)
        return;
    if (!ty) {
        *(long long *)addr = val;
        return;
    }

    switch (ty->kind) {
    case TY_BOOL:
    case TY_CHAR:
        *(uint8_t *)addr = (uint8_t)val;
        return;
    case TY_SHORT:
        *(uint16_t *)addr = (uint16_t)val;
        return;
    case TY_INT:
    case TY_ENUM:
        *(uint32_t *)addr = (uint32_t)val;
        return;
    default:
        *(long long *)addr = val;
        return;
    }
}

static long long eval_ast_addr(VirtualMachine *vm, Node *node, int *error) {
    if (!node) {
        *error = 1;
        return 0;
    }

    switch (node->kind) {
    case ND_VAR: {
        if (!node->var || !node->var->name) {
            printf("Error: Variable has no name\n");
            *error = 1;
            return 0;
        }

        if (node->var->is_function)
            return cc_pc_to_byte_offset((Pc)node->var->code_addr);

        DebugSymbol *sym = cc_lookup_symbol(vm, node->var->name);
        if (sym)
            return (long long)debugger_symbol_address(vm, sym);

        if (!node->var->is_local)
            return (long long)(vm->data_seg + node->var->offset);

        printf("Error: Variable '%s' not found in current scope\n", node->var->name);
        *error = 1;
        return 0;
    }
    case ND_DEREF:
        return eval_ast_node(vm, node->lhs, error);
    case ND_MEMBER: {
        long long base = eval_ast_addr(vm, node->lhs, error);
        if (*error)
            return 0;
        return base + (node->member ? node->member->offset : 0);
    }
    case ND_COMMA:
        (void)eval_ast_node(vm, node->lhs, error);
        if (*error)
            return 0;
        return eval_ast_addr(vm, node->rhs, error);
    default:
        printf("Error: Expression is not an lvalue in condition\n");
        *error = 1;
        return 0;
    }
}

static long long debugger_read_bitfield(Node *node, void *addr) {
    Member *mem = node->member;
    unsigned long long raw = debugger_read_scalar(addr, mem->ty);
    unsigned long long mask = (1ULL << mem->bit_width) - 1;
    unsigned long long val = (raw >> mem->bit_offset) & mask;

    if (!mem->ty->is_unsigned && mem->bit_width < 64 &&
        (val & (1ULL << (mem->bit_width - 1))))
        val |= ~mask;

    return (long long)val;
}

static void debugger_write_bitfield(Node *node, void *addr, long long val) {
    Member *mem = node->member;
    unsigned long long raw = debugger_read_scalar(addr, mem->ty);
    unsigned long long mask = (1ULL << mem->bit_width) - 1;
    raw &= ~(mask << mem->bit_offset);
    raw |= (((unsigned long long)val & mask) << mem->bit_offset);
    debugger_write_scalar(addr, mem->ty, (long long)raw);
}

static long long debugger_eval_direct_call(VirtualMachine *vm, Node *node, int *error) {
    // PLACEHOLDER: support full debugger condition call ABI including floats,
    // structs/unions, variadics, indirect calls, nested static links, and
    // stack-passed arguments.
    // Ticket: https://todo.sr.ht/~takeiteasy/cccc/113
    if (!node->func_ty || node->func_ty->is_variadic) {
        printf("Error: Variadic function calls in conditions are not supported\n");
        *error = 1;
        return 0;
    }
    if (!debugger_type_is_scalar_integer(node->ty)) {
        printf("Error: Non-scalar function return in condition is not supported\n");
        *error = 1;
        return 0;
    }

    int nargs = 0;
    for (Node *arg = node->args; arg; arg = arg->next)
        nargs++;

    long long *args = NULL;
    if (nargs > 0) {
        args = alloca((size_t)nargs * sizeof(long long));
    }

    int arg_idx = 0;
    for (Node *arg = node->args; arg; arg = arg->next) {
        if (!debugger_type_is_scalar_integer(arg->ty)) {
            printf("Error: Non-integer function arguments in conditions are not supported\n");
            *error = 1;
            return 0;
        }
        args[arg_idx++] = eval_ast_node(vm, arg, error);
        if (*error)
            return 0;
    }

    if (node->lhs->kind == ND_VAR && node->lhs->var &&
        node->lhs->var->is_function) {
        Obj *fn = node->lhs->var;
        int ffi_idx = debugger_find_ffi_function(vm, fn->name);
        if (ffi_idx >= 0) {
            ForeignFunc *ff = &vm->compiler.ffi_table[ffi_idx];
            if (ff->is_variadic || ff->returns_double) {
                printf("Error: Full FFI call ABI in conditions is not supported\n");
                *error = 1;
                return 0;
            }
            if (nargs > 8) {
                printf("Error: Stack-passed FFI arguments in conditions are not supported\n");
                *error = 1;
                return 0;
            }
            switch (nargs) {
            case 0: return ((long long (*)(void))ff->func_ptr)();
            case 1: return ((long long (*)(long long))ff->func_ptr)(args[0]);
            case 2: return ((long long (*)(long long, long long))ff->func_ptr)(args[0], args[1]);
            case 3: return ((long long (*)(long long, long long, long long))ff->func_ptr)(args[0], args[1], args[2]);
            case 4: return ((long long (*)(long long, long long, long long, long long))ff->func_ptr)(args[0], args[1], args[2], args[3]);
            case 5: return ((long long (*)(long long, long long, long long, long long, long long))ff->func_ptr)(args[0], args[1], args[2], args[3], args[4]);
            case 6: return ((long long (*)(long long, long long, long long, long long, long long, long long))ff->func_ptr)(args[0], args[1], args[2], args[3], args[4], args[5]);
            case 7: return ((long long (*)(long long, long long, long long, long long, long long, long long, long long))ff->func_ptr)(args[0], args[1], args[2], args[3], args[4], args[5], args[6]);
            case 8: return ((long long (*)(long long, long long, long long, long long, long long, long long, long long, long long))ff->func_ptr)(args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7]);
            }
        }

        if (fn->is_nested) {
            printf("Error: Nested function calls in conditions are not supported\n");
            *error = 1;
            return 0;
        }

        long long saved_regs[32];
        FReg saved_fregs[32];
        memcpy(saved_regs, vm->regs, sizeof(saved_regs));
        memcpy(saved_fregs, vm->fregs, sizeof(saved_fregs));
        Pc saved_pc = vm->pc;
        long long *saved_bp = vm->bp;
        long long *saved_sp = vm->sp;
        long long *saved_shadow_sp = vm->shadow_sp;
        uint32_t saved_flags = vm->flags;
        int saved_single_step = vm->dbg.single_step;
        int saved_step_over = vm->dbg.step_over;
        int saved_step_out = vm->dbg.step_out;
        Pc saved_step_over_return_addr = vm->dbg.step_over_return_addr;
        long long *saved_step_out_bp = vm->dbg.step_out_bp;
        int saved_debugger_attached = vm->dbg.debugger_attached;

        for (int i = nargs - 1; i >= 8; i--)
            *--vm->sp = args[i];
        for (int i = 0; i < nargs && i < 8; i++)
            vm->regs[REG_A0 + i] = args[i];

        vm->flags &= ~CCCC_ENABLE_DEBUGGER;
        vm->dbg.single_step = 0;
        vm->dbg.step_over = 0;
        vm->dbg.step_out = 0;
        vm->dbg.debugger_attached = 0;
        if (vm->flags & CCCC_CFI)
            *--vm->shadow_sp = 0;
        *--vm->sp = 0;
        vm->pc = (Pc)fn->code_addr;
        int rc = vm_eval(vm);
        long long result = vm->regs[REG_A0];

        memcpy(vm->regs, saved_regs, sizeof(saved_regs));
        memcpy(vm->fregs, saved_fregs, sizeof(saved_fregs));
        vm->pc = saved_pc;
        vm->bp = saved_bp;
        vm->sp = saved_sp;
        vm->shadow_sp = saved_shadow_sp;
        vm->flags = saved_flags;
        vm->dbg.single_step = saved_single_step;
        vm->dbg.step_over = saved_step_over;
        vm->dbg.step_out = saved_step_out;
        vm->dbg.step_over_return_addr = saved_step_over_return_addr;
        vm->dbg.step_out_bp = saved_step_out_bp;
        vm->dbg.debugger_attached = saved_debugger_attached;

        if (rc < 0) {
            printf("Error: Function call in condition failed\n");
            *error = 1;
            return 0;
        }
        return debugger_cast_integer(result, node->ty);
    }

    printf("Error: Indirect function calls in conditions are not supported\n");
    *error = 1;
    return 0;
}

// Evaluate an AST node in the current debugger context.
static long long eval_ast_node(VirtualMachine *vm, Node *node, int *error) {
    if (!node) {
        *error = 1;
        return 0;
    }

    add_type(vm, node);

    switch (node->kind) {
        case ND_NUM:
            return node->val;

        case ND_VAR: {
            long long addr = eval_ast_addr(vm, node, error);
            if (*error)
                return 0;
            if (node->ty && (node->ty->kind == TY_ARRAY ||
                             node->ty->kind == TY_STRUCT ||
                             node->ty->kind == TY_UNION ||
                             node->ty->kind == TY_FUNC))
                return addr;
            return debugger_read_scalar((void *)addr, node->ty);
        }

        case ND_ADD: {
            long long left = eval_ast_node(vm, node->lhs, error);
            if (*error) return 0;
            long long right = eval_ast_node(vm, node->rhs, error);
            if (*error) return 0;
            return left + right;
        }

        case ND_SUB: {
            long long left = eval_ast_node(vm, node->lhs, error);
            if (*error) return 0;
            long long right = eval_ast_node(vm, node->rhs, error);
            if (*error) return 0;
            return left - right;
        }

        case ND_MUL: {
            long long left = eval_ast_node(vm, node->lhs, error);
            if (*error) return 0;
            long long right = eval_ast_node(vm, node->rhs, error);
            if (*error) return 0;
            return left * right;
        }

        case ND_DIV: {
            long long left = eval_ast_node(vm, node->lhs, error);
            if (*error) return 0;
            long long right = eval_ast_node(vm, node->rhs, error);
            if (*error) return 0;
            if (right == 0) {
                printf("Error: Division by zero in condition\n");
                *error = 1;
                return 0;
            }
            if (node->ty && node->ty->is_unsigned)
                return (long long)((uint64_t)left / (uint64_t)right);
            return left / right;
        }

        case ND_MOD: {
            long long left = eval_ast_node(vm, node->lhs, error);
            if (*error) return 0;
            long long right = eval_ast_node(vm, node->rhs, error);
            if (*error) return 0;
            if (right == 0) {
                printf("Error: Modulo by zero in condition\n");
                *error = 1;
                return 0;
            }
            if (node->ty && node->ty->is_unsigned)
                return (long long)((uint64_t)left % (uint64_t)right);
            return left % right;
        }

        case ND_SHL: {
            long long left = eval_ast_node(vm, node->lhs, error);
            if (*error) return 0;
            long long right = eval_ast_node(vm, node->rhs, error);
            if (*error) return 0;
            return left << right;
        }

        case ND_SHR: {
            long long left = eval_ast_node(vm, node->lhs, error);
            if (*error) return 0;
            long long right = eval_ast_node(vm, node->rhs, error);
            if (*error) return 0;
            if (node->lhs && node->lhs->ty && node->lhs->ty->is_unsigned)
                return (long long)((uint64_t)left >> right);
            return left >> right;
        }

        case ND_EQ: {
            long long left = eval_ast_node(vm, node->lhs, error);
            if (*error) return 0;
            long long right = eval_ast_node(vm, node->rhs, error);
            if (*error) return 0;
            return left == right;
        }

        case ND_NE: {
            long long left = eval_ast_node(vm, node->lhs, error);
            if (*error) return 0;
            long long right = eval_ast_node(vm, node->rhs, error);
            if (*error) return 0;
            return left != right;
        }

        case ND_LT: {
            long long left = eval_ast_node(vm, node->lhs, error);
            if (*error) return 0;
            long long right = eval_ast_node(vm, node->rhs, error);
            if (*error) return 0;
            if (node->lhs && node->lhs->ty && node->lhs->ty->is_unsigned)
                return (uint64_t)left < (uint64_t)right;
            return left < right;
        }

        case ND_LE: {
            long long left = eval_ast_node(vm, node->lhs, error);
            if (*error) return 0;
            long long right = eval_ast_node(vm, node->rhs, error);
            if (*error) return 0;
            if (node->lhs && node->lhs->ty && node->lhs->ty->is_unsigned)
                return (uint64_t)left <= (uint64_t)right;
            return left <= right;
        }

        case ND_LOGAND: {
            long long left = eval_ast_node(vm, node->lhs, error);
            if (*error) return 0;
            if (!left) return 0;  // Short-circuit
            long long right = eval_ast_node(vm, node->rhs, error);
            if (*error) return 0;
            return right != 0;
        }

        case ND_LOGOR: {
            long long left = eval_ast_node(vm, node->lhs, error);
            if (*error) return 0;
            if (left) return 1;  // Short-circuit
            long long right = eval_ast_node(vm, node->rhs, error);
            if (*error) return 0;
            return right != 0;
        }

        case ND_NEG: {
            long long val = eval_ast_node(vm, node->lhs, error);
            if (*error) return 0;
            return -val;
        }

        case ND_NOT: {
            long long val = eval_ast_node(vm, node->lhs, error);
            if (*error) return 0;
            return !val;
        }

        case ND_BITNOT: {
            long long val = eval_ast_node(vm, node->lhs, error);
            if (*error) return 0;
            return ~val;
        }

        case ND_BITAND: {
            long long left = eval_ast_node(vm, node->lhs, error);
            if (*error) return 0;
            long long right = eval_ast_node(vm, node->rhs, error);
            if (*error) return 0;
            return left & right;
        }

        case ND_BITOR: {
            long long left = eval_ast_node(vm, node->lhs, error);
            if (*error) return 0;
            long long right = eval_ast_node(vm, node->rhs, error);
            if (*error) return 0;
            return left | right;
        }

        case ND_BITXOR: {
            long long left = eval_ast_node(vm, node->lhs, error);
            if (*error) return 0;
            long long right = eval_ast_node(vm, node->rhs, error);
            if (*error) return 0;
            return left ^ right;
        }

        case ND_CAST: {
            long long val = eval_ast_node(vm, node->lhs, error);
            if (*error) return 0;
            return debugger_cast_integer(val, node->ty);
        }

        case ND_ADDR:
            return eval_ast_addr(vm, node->lhs, error);

        case ND_DEREF: {
            long long addr = eval_ast_node(vm, node->lhs, error);
            if (*error) return 0;
            if (node->ty && (node->ty->kind == TY_ARRAY ||
                             node->ty->kind == TY_STRUCT ||
                             node->ty->kind == TY_UNION))
                return addr;
            return debugger_read_scalar((void *)addr, node->ty);
        }

        case ND_MEMBER: {
            long long addr = eval_ast_addr(vm, node, error);
            if (*error) return 0;
            if (node->member && node->member->is_bitfield)
                return debugger_read_bitfield(node, (void *)addr);
            if (node->ty && (node->ty->kind == TY_ARRAY ||
                             node->ty->kind == TY_STRUCT ||
                             node->ty->kind == TY_UNION))
                return addr;
            return debugger_read_scalar((void *)addr, node->ty);
        }

        case ND_ASSIGN: {
            long long val = eval_ast_node(vm, node->rhs, error);
            if (*error) return 0;
            long long addr = eval_ast_addr(vm, node->lhs, error);
            if (*error) return 0;
            if (node->lhs->kind == ND_MEMBER && node->lhs->member &&
                node->lhs->member->is_bitfield)
                debugger_write_bitfield(node->lhs, (void *)addr, val);
            else
                debugger_write_scalar((void *)addr, node->lhs->ty, val);
            return debugger_cast_integer(val, node->ty);
        }

        case ND_COND: {
            long long cond = eval_ast_node(vm, node->cond, error);
            if (*error) return 0;
            return eval_ast_node(vm, cond ? node->then : node->els, error);
        }

        case ND_COMMA:
            (void)eval_ast_node(vm, node->lhs, error);
            if (*error) return 0;
            return eval_ast_node(vm, node->rhs, error);

        case ND_FUNCALL:
            return debugger_eval_direct_call(vm, node, error);

        default:
            printf("Error: Unsupported node kind %d in condition\n", node->kind);
            *error = 1;
            return 0;
    }
}

// Evaluate a condition string and return true/false
// Returns: 1 if condition is true, 0 if false or error
static int debugger_eval_condition(VirtualMachine *vm, const char *condition_str) {
    if (!condition_str || !*condition_str) {
        return 1;  // Empty condition is always true
    }

    // Create a temporary buffer for tokenization
    char buf[512];
    snprintf(buf, sizeof(buf), "%s\n", condition_str);

    // Create a temporary file struct for tokenization
    File temp_file = {
        .name = "<condition>",
        .file_no = 0,
        .contents = buf,
        .display_name = NULL,
        .line_delta = 0
    };

    // Tokenize the condition
    Token *tok = tokenize(vm, &temp_file);
    if (!tok) {
        printf("Error: Failed to tokenize condition\n");
        return 0;
    }
    convert_pp_tokens(vm, tok);

    // Parse as expression
    Obj *current_fn = debugger_current_function(vm);
    Scope condition_scope = {0};
    for (Obj *obj = vm->compiler.globals; obj; obj = obj->next)
        debugger_add_condition_scope_var(&condition_scope.vars, obj);
    if (current_fn) {
        for (Obj *obj = current_fn->locals; obj; obj = obj->next)
            debugger_add_condition_scope_var(&condition_scope.vars, obj);
    }

    Scope *saved_scope = vm->compiler.scope;
    condition_scope.next = saved_scope;
    vm->compiler.scope = &condition_scope;
    Token *rest = NULL;
    Node *expr = cc_parse_expr(vm, &rest, tok);
    vm->compiler.scope = saved_scope;
    debugger_free_condition_scope(condition_scope.vars);
    if (!expr) {
        printf("Error: Failed to parse condition expression\n");
        return 0;
    }

    // Evaluate the expression
    int error = 0;
    long long result = eval_ast_node(vm, expr, &error);

    if (error) {
        return 0;  // Error during evaluation
    }

    return result != 0;  // Return true if non-zero
}

// ============================================================================
// Watchpoint Management Functions
// ============================================================================

int cc_add_watchpoint(VirtualMachine *vm, void *address, int size, int type, const char *expr) {
    if (vm->dbg.num_watchpoints >= MAX_WATCHPOINTS) {
        printf("Error: Maximum number of watchpoints (%d) reached\n", MAX_WATCHPOINTS);
        return -1;
    }

    // Find first available slot
    for (int i = 0; i < MAX_WATCHPOINTS; i++) {
        if (!vm->dbg.watchpoints[i].enabled) {
            vm->dbg.watchpoints[i].address = address;
            vm->dbg.watchpoints[i].size = size;
            vm->dbg.watchpoints[i].type = type;
            switch (size) {
                case 1: vm->dbg.watchpoints[i].old_value = *(char *)address; break;
                case 2: vm->dbg.watchpoints[i].old_value = *(short *)address; break;
                case 4: vm->dbg.watchpoints[i].old_value = *(int *)address; break;
                case 8: vm->dbg.watchpoints[i].old_value = *(long long *)address; break;
                default: vm->dbg.watchpoints[i].old_value = 0; break;
            }
            vm->dbg.watchpoints[i].expr = expr ? strdup(expr) : NULL;
            vm->dbg.watchpoints[i].enabled = 1;
            vm->dbg.watchpoints[i].hit_count = 0;
            vm->dbg.num_watchpoints++;

            const char *type_str = "";
            if (type & WATCH_READ && type & WATCH_WRITE) {
                type_str = "access";
            } else if (type & WATCH_WRITE) {
                type_str = "write";
            } else if (type & WATCH_READ) {
                type_str = "read";
            }

            printf("Watchpoint #%d (%s) set at %p", i, type_str, address);
            if (expr) {
                printf(" (%s)", expr);
            }
            printf("\n");

            return i;
        }
    }

    return -1;
}

void cc_remove_watchpoint(VirtualMachine *vm, int index) {
    if (index < 0 || index >= MAX_WATCHPOINTS) {
        printf("Error: Invalid watchpoint index %d\n", index);
        return;
    }

    if (!vm->dbg.watchpoints[index].enabled) {
        printf("Error: No watchpoint at index %d\n", index);
        return;
    }

    vm->dbg.watchpoints[index].enabled = 0;
    vm->dbg.watchpoints[index].address = NULL;
    if (vm->dbg.watchpoints[index].expr) {
        free(vm->dbg.watchpoints[index].expr);
        vm->dbg.watchpoints[index].expr = NULL;
    }
    vm->dbg.num_watchpoints--;

    printf("Watchpoint #%d removed\n", index);
}

// Check if a memory access triggers any watchpoints
// Returns: watchpoint index if triggered, -1 otherwise
int debugger_check_watchpoint(VirtualMachine *vm, void *addr, int size, int access_type) {
    // Safety checks
    if (!vm || !(vm->flags & CCCC_ENABLE_DEBUGGER) || vm->dbg.num_watchpoints == 0 || !addr) {
        return -1;
    }

    // Don't check watchpoints if we're already in the debugger REPL
    if (vm->dbg.debugger_attached) {
        return -1;
    }

    // Don't check if VM isn't fully initialized (pc, bp, sp should be valid)
    if (vm->pc == CCCC_INVALID_PC || !vm->bp || !vm->sp || !vm->text_seg) {
        return -1;
    }

    for (int i = 0; i < MAX_WATCHPOINTS; i++) {
        if (!vm->dbg.watchpoints[i].enabled) {
            continue;
        }

        Watchpoint *wp = &vm->dbg.watchpoints[i];

        // Check if the access overlaps with this watchpoint
        // Watchpoint range: [wp->address, wp->address + wp->size)
        // Access range: [addr, addr + size)
        long long wp_start = (long long)wp->address;
        long long wp_end = wp_start + wp->size;
        long long access_start = (long long)addr;
        long long access_end = access_start + size;

        // Check for overlap
        if (access_start >= wp_end || access_end <= wp_start) {
            continue;  // No overlap
        }

        // Check if this watchpoint type matches the access type
        if ((wp->type & access_type) == 0) {
            continue;  // Type doesn't match
        }

        // If this is a WATCH_CHANGE watchpoint, check if value changed
        if ((wp->type & WATCH_CHANGE) && (access_type & WATCH_WRITE)) {
            // Read current value
            long long current_value = 0;
            switch (wp->size) {
                case 1: current_value = *(char *)wp->address; break;
                case 2: current_value = *(short *)wp->address; break;
                case 4: current_value = *(int *)wp->address; break;
                case 8: current_value = *(long long *)wp->address; break;
                default: current_value = *(long long *)wp->address; break;
            }

            // Check if value changed
            if (current_value == wp->old_value) {
                continue;  // Value didn't change, don't trigger
            }

            // Update old value for next check
            wp->old_value = current_value;
        }

        // Watchpoint triggered!
        wp->hit_count++;

        // Get source location if available
        File *file = NULL;
        int line_no = 0;
        cc_get_source_location(vm, vm->pc, &file, &line_no, NULL);

        // Print watchpoint info
        printf("\n========== WATCHPOINT TRIGGERED ==========\n");
        printf("Watchpoint #%d hit\n", i);

        const char *type_str = "";
        if ((wp->type & WATCH_READ) && (wp->type & WATCH_WRITE)) {
            type_str = "access";
        } else if (wp->type & WATCH_WRITE) {
            type_str = "write";
        } else if (wp->type & WATCH_READ) {
            type_str = "read";
        }

        const char *access_str = (access_type & WATCH_READ) ? "read" : "write";

        printf("Type:       %s watchpoint (%s access)\n", type_str, access_str);
        printf("Address:    %p\n", wp->address);
        printf("Size:       %d bytes\n", wp->size);
        if (wp->expr) {
            printf("Expression: %s\n", wp->expr);
        }
        printf("Hit count:  %d\n", wp->hit_count);

        if (file && line_no) {
            printf("Location:   %s:%d\n", file->name, line_no);
        }

        printf("==========================================\n\n");

        // Enter debugger REPL
        cc_debug_repl(vm);

        return i;
    }

    return -1;
}

// ========== Random Canary Generation ==========

long long generate_random_canary(void) {
    long long canary = 0;

#if defined(_WIN32) || defined(_WIN64)
    srand((unsigned int)time(NULL));
    canary = ((long long)rand() << 32) | rand();
#else
    FILE *f = fopen("/dev/urandom", "rb");
    if (f) {
        if (fread(&canary, sizeof(canary), 1, f) != 1) {
            canary = STACK_CANARY ^ (long long)time(NULL);
        }
        fclose(f);
    } else {
        canary = STACK_CANARY ^ (long long)time(NULL);
    }
#endif

    // Ensure canary has null bytes to make exploitation harder
    canary &= ~0xFF00000000000000LL;
    canary |= 0x00FF000000000000LL;

    return canary;
}
