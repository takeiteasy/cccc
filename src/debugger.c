/*
 JCC Debugger Implementation

 Copyright (C) 2025 George Watson

 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.
*/

#include "jcc.h"
#include "./internal.h"

static int is_valid_vm_address(JCC *vm, void *addr) {
    long long ptr = (long long)addr;
    // Text segment
    if (ptr >= (long long)vm->text_seg && ptr < (long long)vm->text_ptr) return 1;
    // Data segment
    if (ptr >= (long long)vm->data_seg && ptr < (long long)vm->data_ptr) return 1;
    // Heap segment
    if (ptr >= (long long)vm->heap_seg && ptr < (long long)vm->heap_ptr) return 1;
    // Stack segment (assuming stack grows up from stack_seg)
    if (ptr >= (long long)vm->stack_seg && ptr < (long long)(vm->stack_seg + vm->poolsize)) return 1;
    return 0;
}

void debugger_init(JCC *vm) {
    vm->flags |= JCC_ENABLE_DEBUGGER;  // Make sure it's enabled
    vm->dbg.num_breakpoints = 0;
    vm->dbg.num_watchpoints = 0;  // Initialize watchpoint counter
    vm->dbg.single_step = 0;
    vm->dbg.step_over = 0;
    vm->dbg.step_out = 0;
    vm->dbg.step_over_return_addr = NULL;
    vm->dbg.step_out_bp = NULL;
    vm->dbg.debugger_attached = 0;

    // Initialize all breakpoints
    for (int i = 0; i < MAX_BREAKPOINTS; i++) {
        vm->dbg.breakpoints[i].pc = NULL;
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

int cc_add_breakpoint(JCC *vm, long long *pc) {
    if (vm->dbg.num_breakpoints >= MAX_BREAKPOINTS) {
        printf("Error: Maximum number of breakpoints (%d) reached\n", MAX_BREAKPOINTS);
        return -1;
    }

    // Check if breakpoint already exists at this PC
    for (int i = 0; i < MAX_BREAKPOINTS; i++) {
        if (vm->dbg.breakpoints[i].enabled && vm->dbg.breakpoints[i].pc == pc) {
            printf("Breakpoint already exists at PC %p\n", (void*)pc);
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
            long long offset = (long long)pc - (long long)vm->text_seg;
            printf("Breakpoint #%d set at PC %p (offset: %lld)\n", i, (void*)pc, offset);
            return i;
        }
    }

    return -1;
}

void cc_remove_breakpoint(JCC *vm, int index) {
    if (index < 0 || index >= MAX_BREAKPOINTS) {
        printf("Error: Invalid breakpoint index %d\n", index);
        return;
    }

    if (!vm->dbg.breakpoints[index].enabled) {
        printf("Error: No breakpoint at index %d\n", index);
        return;
    }

    vm->dbg.breakpoints[index].enabled = 0;
    vm->dbg.breakpoints[index].pc = NULL;
    vm->dbg.breakpoints[index].hit_count = 0;
    if (vm->dbg.breakpoints[index].condition) {
        free(vm->dbg.breakpoints[index].condition);
        vm->dbg.breakpoints[index].condition = NULL;
    }
    vm->dbg.num_breakpoints--;

    printf("Breakpoint #%d removed\n", index);
}

static int debugger_eval_condition(JCC *vm, const char *condition_str);

int debugger_check_breakpoint(JCC *vm) {
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

void debugger_list_breakpoints(JCC *vm) {
    if (vm->dbg.num_breakpoints == 0) {
        printf("No breakpoints set.\n");
        return;
    }

    printf("\nBreakpoints:\n");
    printf("%-5s %-18s %-12s %-10s %-20s\n", "Num", "Address", "Offset", "Hit Count", "Condition");
    printf("%-5s %-18s %-12s %-10s %-20s\n", "---", "-------", "------", "---------", "---------");

    for (int i = 0; i < MAX_BREAKPOINTS; i++) {
        if (vm->dbg.breakpoints[i].enabled) {
            long long offset = (long long)vm->dbg.breakpoints[i].pc - (long long)vm->text_seg;
            printf("%-5d 0x%-16llx %-12lld %-10d",
                   i,
                   (long long)vm->dbg.breakpoints[i].pc,
                   offset,
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

void debugger_print_registers(JCC *vm) {
    printf("\n=== Registers ===\n");
    printf("  A0 (return):  0x%016llx (%lld)\n", vm->regs[REG_A0], vm->regs[REG_A0]);
    printf("  FA0 (float):  %f\n", vm->fregs[FREG_A0]);
    printf("  pc:           %p", (void*)vm->pc);
    if (vm->pc >= vm->text_seg && vm->pc < vm->text_ptr) {
        long long offset = (long long)vm->pc - (long long)vm->text_seg;
        printf(" (offset: %lld)", offset);
    }
    printf("\n");
    printf("  bp:           %p\n", (void*)vm->bp);
    printf("  sp:           %p\n", (void*)vm->sp);
    printf("  cycle:        %lld\n", vm->cycle);
    // Print first few general registers
    printf("  T0-T3:        %lld, %lld, %lld, %lld\n", 
           vm->regs[REG_T0], vm->regs[REG_T1], vm->regs[REG_T2], vm->regs[REG_T3]);
    printf("\n");
}

void debugger_print_stack(JCC *vm, int count) {
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
#define X(NAME) #NAME,
        OPS_X
#undef X
    };
    if (op >= 0 && op < (int)(sizeof(names) / sizeof(names[0])))
        return names[op];
    return "UNKNOWN";
}

// Returns the number of words consumed by the instruction (including opcode)
static int disassemble_instruction(long long *pc, long long *text_seg, long long *text_end) {
    if (pc >= text_end) return 0;
    
    long long offset = (long long)pc - (long long)text_seg;
    int op = (int)*pc;
    const char *name = opcode_name(op);
    
    printf("0x%llx (offset %lld): %-6s", (long long)pc, offset, name);
    
    int size = 1; // Default size (just opcode)
    
    switch (op) {
        // Multi-register opcodes (RRR format: 1 operand word with 3 registers)
        case ADD3:
        case SUB3:
        case MUL3:
        case DIV3:
        case MOD3:
        case AND3:
        case OR3:
        case XOR3:
        case SHL3:
        case SHR3:
        case SEQ3:
        case SNE3:
        case SLT3:
        case SLE3:
        case SGT3:
        case SGE3:
        case MOV3:
        case NEG3:
        case NOT3:
        case BNOT3:
            if (pc + 1 < text_end) {
                int rd = (int)(pc[1] & 0xFF);
                int rs1 = (int)((pc[1] >> 8) & 0xFF);
                int rs2 = (int)((pc[1] >> 16) & 0xFF);
                printf(" r%d, r%d, r%d", rd, rs1, rs2);
            }
            size = 2;
            break;

        // Multi-register opcodes (RI format: 1 register word + 1 immediate word)
        case LI3:
        case LEA3:
        case ADDI3:
            if (pc + 2 < text_end) {
                int rd = (int)(pc[1] & 0xFF);
                long long imm = pc[2];
                printf(" r%d, %lld", rd, imm);
            }
            size = 3;
            break;

        // Control flow with operand
        case JMP:
        case CALL:
        case JMPT:
        case JMPI:
        case ADJ:
            if (pc + 1 < text_end) {
                printf(" %lld", pc[1]);
            }
            size = 2;
            break;

        // JZ3/JNZ3: register + target
        case JZ3:
        case JNZ3:
            if (pc + 2 < text_end) {
                int rs = (int)(pc[1] & 0xFF);
                printf(" r%d, %lld", rs, pc[2]);
            }
            size = 3;
            break;

        // Register-based calling convention opcodes
        case ENT3:
            // ENT3 has 2 operands: [stack_size:32|param_count:32] [float_param_mask]
            if (pc + 2 < text_end) {
                int stack_size = (int)(pc[1] & 0xFFFFFFFF);
                int param_count = (int)((pc[1] >> 32) & 0xFFFFFFFF);
                long long float_mask = pc[2];
                printf(" stack=%d, params=%d, floatmask=0x%llx", stack_size, param_count, float_mask);
            }
            size = 3;
            break;

        case LEV3:
            // LEV3 has no operands
            size = 1;
            break;

        // Load/store opcodes (RR format)
        case LDR_B:
        case LDR_H:
        case LDR_W:
        case LDR_D:
        case STR_B:
        case STR_H:
        case STR_W:
        case STR_D:
        case FLDR:
        case FSTR:
            if (pc + 1 < text_end) {
                int rd = (int)(pc[1] & 0xFF);
                int rs = (int)((pc[1] >> 8) & 0xFF);
                printf(" r%d, r%d", rd, rs);
            }
            size = 2;
            break;

        // Float operations (FRRR format)
        case FADD3:
        case FSUB3:
        case FMUL3:
        case FDIV3:
        case FEQ3:
        case FNE3:
        case FLT3:
        case FLE3:
        case FGT3:
        case FGE3:
            if (pc + 1 < text_end) {
                int rd = (int)(pc[1] & 0xFF);
                int rs1 = (int)((pc[1] >> 8) & 0xFF);
                int rs2 = (int)((pc[1] >> 16) & 0xFF);
                printf(" f%d, f%d, f%d", rd, rs1, rs2);
            }
            size = 2;
            break;

        case FNEG3:
        case I2F3:
        case F2I3:
        case FR2R:
            if (pc + 1 < text_end) {
                int rd = (int)(pc[1] & 0xFF);
                int rs = (int)((pc[1] >> 8) & 0xFF);
                printf(" r%d, r%d", rd, rs);
            }
            size = 2;
            break;

        // Safety opcodes — single immediate (offset or scope_id)
        case CHKI:
        case MARKI:
        case SCOPEIN:
        case SCOPEOUT:
        case CHKL:
        case MARKR:
        case MARKW:
            if (pc + 1 < text_end)
                printf(" %lld", pc[1]);
            size = 2;
            break;

        // CHKB: [rs1_base:8|rs2_offset:8] operand word (RR, no trailing imm)
        case CHKB:
            if (pc + 1 < text_end) {
                int rs1 = (int)(pc[1] & 0xFF);
                int rs2 = (int)((pc[1] >> 8) & 0xFF);
                printf(" r%d, r%d", rs1, rs2);
            }
            size = 2;
            break;

        // CHKPA: [rs:8] operand word
        case CHKPA:
            if (pc + 1 < text_end) {
                int rs = (int)(pc[1] & 0xFF);
                printf(" r%d", rs);
            }
            size = 2;
            break;

        // MARKA: [rs_ptr:8] [offset] [size] [scope_id]
        case MARKA:
            if (pc + 4 < text_end) {
                int rs = (int)(pc[1] & 0xFF);
                printf(" r%d, %lld, %lld, %lld", rs, pc[2], pc[3], pc[4]);
            }
            size = 5;
            break;

        // MARKP: [rs_ptr:8|rs_base:8] [origin_type] [size]
        case MARKP:
            if (pc + 3 < text_end) {
                int rs1 = (int)(pc[1] & 0xFF);
                int rs2 = (int)((pc[1] >> 8) & 0xFF);
                printf(" r%d, r%d, %lld, %lld", rs1, rs2, pc[2], pc[3]);
            }
            size = 4;
            break;

        // CHKP3/CHKA3/CHKT3 (register-based safety)
        case CHKP3:
        case CHKA3:
        case CHKT3:
            if (pc + 1 < text_end) {
                int rs = (int)(pc[1] & 0xFF);
                printf(" r%d", rs);
            }
            size = 2;
            break;

        // PSH3/POP3 (register-based stack operations)
        case PSH3:
        case POP3:
            if (pc + 1 < text_end) {
                int rs = (int)(pc[1] & 0xFF);
                printf(" r%d", rs);
            }
            size = 2;
            break;
            
        // Type conversion
        case SX1:
        case SX2:
        case SX4:
        case ZX1:
        case ZX2:
        case ZX4:
            if (pc + 1 < text_end) {
                int rd = (int)(pc[1] & 0xFF);
                int rs = (int)((pc[1] >> 8) & 0xFF);
                printf(" r%d, r%d", rd, rs);
            }
            size = 2;
            break;

        // CALLI (indirect call via register)
        case CALLI:
            if (pc + 1 < text_end) {
                int rs = (int)(pc[1] & 0xFF);
                printf(" r%d", rs);
            }
            size = 2;
            break;

        // R2FR (int to float bit transfer - already covered by FR2R case)
        case R2FR:
            if (pc + 1 < text_end) {
                int rd = (int)(pc[1] & 0xFF);
                int rs = (int)((pc[1] >> 8) & 0xFF);
                printf(" f%d, r%d", rd, rs);
            }
            size = 2;
            break;

        default:
            // 0 operands (size 1)
            size = 1;
            break;
    }
    
    printf("\n");
    return size;
}

void cc_disassemble(JCC *vm) {
    if (!vm || !vm->text_seg) return;
    
    printf("=== Disassembly ===\n");
    // text_seg[0] is the entry point offset, not an instruction
    printf("Entry point: 0x%llx (offset %lld)\n", 
           (long long)(vm->text_seg + vm->text_seg[0]), vm->text_seg[0]);
    
    long long *pc = vm->text_seg + 1;
    while (pc < vm->text_ptr) {
        int size = disassemble_instruction(pc, vm->text_seg, vm->text_ptr);
        if (size == 0) break;
        pc += size;
    }
    printf("===================\n");
}

void debugger_disassemble_current(JCC *vm) {
    if (!vm->pc || vm->pc < vm->text_seg || vm->pc >= vm->text_ptr) {
        printf("PC out of text segment range\n");
        return;
    }
    disassemble_instruction(vm->pc, vm->text_seg, vm->text_ptr);
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

static void debugger_print_source_location(JCC *vm) {
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

void cc_debug_repl(JCC *vm) {
    char line[256];
    char cmd[64];

    vm->dbg.debugger_attached = 1;

    printf("\n========================================\n");
    printf("    JCC Debugger\n");
    printf("========================================\n");
    printf("Type 'help' or '?' for command list\n\n");

    debugger_print_registers(vm);
    debugger_print_source_location(vm);
    debugger_disassemble_current(vm);

    while (1) {
        printf("(jcc-dbg) ");
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
        if (strcmp(cmd, "help") == 0 || strcmp(cmd, "h") == 0 || strcmp(cmd, "?") == 0) {
            print_help();
        }
        // Continue
        else if (strcmp(cmd, "continue") == 0 || strcmp(cmd, "c") == 0) {
            vm->dbg.single_step = 0;
            vm->dbg.step_over = 0;
            vm->dbg.step_out = 0;
            break;
        }
        // Single step
        else if (strcmp(cmd, "step") == 0 || strcmp(cmd, "s") == 0) {
            vm->dbg.single_step = 1;
            vm->dbg.step_over = 0;
            vm->dbg.step_out = 0;
            break;
        }
        // Step over
        else if (strcmp(cmd, "next") == 0 || strcmp(cmd, "n") == 0) {
            vm->dbg.single_step = 0;
            vm->dbg.step_over = 1;
            vm->dbg.step_out = 0;
            // Save current return address (on top of stack after CALL)
            if (vm->sp < vm->stack_seg) {
                vm->dbg.step_over_return_addr = (long long *)*vm->sp;
            }
            break;
        }
        // Step out
        else if (strcmp(cmd, "finish") == 0 || strcmp(cmd, "f") == 0) {
            vm->dbg.single_step = 0;
            vm->dbg.step_over = 0;
            vm->dbg.step_out = 1;
            vm->dbg.step_out_bp = vm->bp;
            break;
        }
        // Print registers
        else if (strcmp(cmd, "registers") == 0 || strcmp(cmd, "r") == 0) {
            debugger_print_registers(vm);
        }
        // Print stack
        else if (strcmp(cmd, "stack") == 0 || strcmp(cmd, "st") == 0) {
            int count = 10;
            sscanf(line, "%*s %d", &count);
            debugger_print_stack(vm, count);
        }
        // Disassemble
        else if (strcmp(cmd, "disasm") == 0 || strcmp(cmd, "dis") == 0) {
            debugger_disassemble_current(vm);
        }
        // Set breakpoint
        else if (strcmp(cmd, "break") == 0 || strcmp(cmd, "b") == 0) {
            char arg[128];
            char *condition = NULL;
            long long *bp_pc = NULL;

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
                    if (!bp_pc) {
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
                        if (!bp_pc) {
                            printf("Error: Could not find code for line %lld\n", num);
                        }
                    } else {
                        // Large number, treat as bytecode offset
                        bp_pc = vm->text_seg + num;
                        if (bp_pc < vm->text_seg || bp_pc >= vm->text_ptr) {
                            printf("Error: Offset %lld is out of range\n", num);
                            bp_pc = NULL;
                        }
                    }
                }
                // Otherwise treat as function name
                else {
                    bp_pc = cc_find_function_entry(vm, arg);
                    if (!bp_pc) {
                        printf("Error: Function '%s' not found\n", arg);
                    }
                }

                if (bp_pc) {
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
        else if (strcmp(cmd, "delete") == 0 || strcmp(cmd, "d") == 0) {
            int num;
            if (sscanf(line, "%*s %d", &num) == 1) {
                cc_remove_breakpoint(vm, num);
            } else {
                printf("Usage: delete <breakpoint_number>\n");
            }
        }
        // List breakpoints
        else if (strcmp(cmd, "list") == 0 || strcmp(cmd, "l") == 0) {
            debugger_list_breakpoints(vm);
        }
        // Memory inspection
        else if (strcmp(cmd, "memory") == 0 || strcmp(cmd, "m") == 0) {
            long long addr;
            if (sscanf(line, "%*s %llx", &addr) == 1) {
                if (is_valid_vm_address(vm, (void*)addr)) {
                    printf("Memory at 0x%llx: 0x%016llx (%lld)\n",
                           addr, *(long long*)addr, *(long long*)addr);
                } else {
                    printf("Error: Invalid memory address 0x%llx\n", addr);
                }
            } else {
                printf("Usage: memory <hex_address>\n");
            }
        }
        // Watch (write watchpoint)
        else if (strcmp(cmd, "watch") == 0 || strcmp(cmd, "w") == 0) {
            char expr[128];
            if (sscanf(line, "%*s %127s", expr) == 1) {
                // Try to parse as hex address
                if (expr[0] == '0' && expr[1] == 'x') {
                    long long addr;
                    if (sscanf(expr, "%llx", &addr) == 1) {
                        if (is_valid_vm_address(vm, (void*)addr)) {
                            cc_add_watchpoint(vm, (void*)addr, 8, WATCH_WRITE | WATCH_CHANGE, expr);
                        } else {
                            printf("Error: Invalid memory address 0x%llx\n", addr);
                        }
                    }
                } else {
                    // Try to look up as variable name
                    DebugSymbol *sym = cc_lookup_symbol(vm, expr);
                    if (sym) {
                        void *addr = sym->is_local ?
                            (void*)(vm->bp + sym->offset) :
                            (void*)(vm->data_seg + sym->offset);
                        if (is_valid_vm_address(vm, addr)) {
                            int size = sym->ty ? sym->ty->size : 8;
                            cc_add_watchpoint(vm, addr, size, WATCH_WRITE | WATCH_CHANGE, expr);
                        } else {
                            printf("Error: Invalid address for variable '%s'\n", expr);
                        }
                    } else {
                        printf("Error: Variable '%s' not found (symbol table not yet implemented)\n", expr);
                        printf("Use hex address instead: watch 0x<address>\n");
                    }
                }
            } else {
                printf("Usage: watch <variable> | watch 0x<address>\n");
            }
        }
        // RWatch (read watchpoint)
        else if (strcmp(cmd, "rwatch") == 0) {
            char expr[128];
            if (sscanf(line, "%*s %127s", expr) == 1) {
                long long addr;
                if (sscanf(expr, "%llx", &addr) == 1) {
                    if (is_valid_vm_address(vm, (void*)addr)) {
                        cc_add_watchpoint(vm, (void*)addr, 8, WATCH_READ, expr);
                    } else {
                        printf("Error: Invalid memory address 0x%llx\n", addr);
                    }
                } else {
                    printf("Error: Symbol lookup not yet implemented, use hex address\n");
                    printf("Usage: rwatch 0x<address>\n");
                }
            } else {
                printf("Usage: rwatch 0x<address>\n");
            }
        }
        // AWatch (access watchpoint - read or write)
        else if (strcmp(cmd, "awatch") == 0) {
            char expr[128];
            if (sscanf(line, "%*s %127s", expr) == 1) {
                long long addr;
                if (sscanf(expr, "%llx", &addr) == 1) {
                    if (is_valid_vm_address(vm, (void*)addr)) {
                        cc_add_watchpoint(vm, (void*)addr, 8, WATCH_READ | WATCH_WRITE, expr);
                    } else {
                        printf("Error: Invalid memory address 0x%llx\n", addr);
                    }
                } else {
                    printf("Error: Symbol lookup not yet implemented, use hex address\n");
                    printf("Usage: awatch 0x<address>\n");
                }
            } else {
                printf("Usage: awatch 0x<address>\n");
            }
        }
        // Info watch (list watchpoints)
        else if (strcmp(cmd, "info") == 0) {
            char subcmd[64];
            if (sscanf(line, "%*s %63s", subcmd) == 1 && strcmp(subcmd, "watch") == 0) {
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
        else if (strcmp(cmd, "quit") == 0 || strcmp(cmd, "q") == 0) {
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

int debugger_run(JCC *vm, int argc, char **argv) {
    // Find main function
    Obj *main_fn = NULL;
    for (Obj *obj = vm->compiler.globals; obj; obj = obj->next) {
        if (obj->is_function && obj->name && strcmp(obj->name, "main") == 0) {
            main_fn = obj;
            break;
        }
    }

    if (!main_fn) {
        printf("Error: main function not found\n");
        return -1;
    }

    printf("\n========================================\n");
    printf("    JCC Debugger\n");
    printf("========================================\n");
    printf("Starting at entry point (PC: %p, offset: %lld)\n",
            (void*)vm->pc, (long long)(vm->pc - vm->text_seg));
    printf("Type 'help' for commands, 'c' to continue\n\n");

    // Set PC to main (code_addr is an offset from text_seg)
    vm->pc = vm->text_seg + main_fn->code_addr;

    // Setup stack for main(argc, argv)
    vm->sp = vm->stack_seg;
    vm->bp = vm->stack_seg;
    vm->initial_sp = vm->stack_seg;
    vm->initial_bp = vm->stack_seg;

    // Setup shadow stack for CFI if enabled
    if (vm->flags & JCC_CFI) {
        vm->shadow_sp = vm->shadow_stack;
    }

    // Push argv (pointer to array of strings)
    *--vm->sp = (long long)argv;
    // Push argc
    *--vm->sp = argc;
    // Push dummy return address (NULL to detect exit)
    *--vm->sp = 0;

    printf("Starting debugger at main (PC: %p)\n", (void*)vm->pc);
    printf("Type 'help' for debugger commands\n\n");

    // Enter debugger at start
    cc_debug_repl(vm);

    // Main execution loop with debugger support
    // Call vm_eval which handles all the debugger hooks (breakpoints, stepping, etc.)
    return vm_eval(vm);
}

// ============================================================================
// Source Mapping Functions (for source-level debugging)
// ============================================================================

int cc_get_source_location(JCC *vm, long long *pc, File **out_file, int *out_line, int *out_col) {
    if (!(vm->flags & JCC_ENABLE_DEBUGGER) || !vm->dbg.source_map || vm->dbg.source_map_count == 0) {
        return 0;
    }

    long long pc_offset = pc - vm->text_seg;

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

long long *cc_find_pc_for_source(JCC *vm, File *file, int line) {
    if (!(vm->flags & JCC_ENABLE_DEBUGGER) || !vm->dbg.source_map || vm->dbg.source_map_count == 0) {
        return NULL;
    }

    // Linear search for the first matching source location
    // TODO: Could optimize with secondary index
    for (int i = 0; i < vm->dbg.source_map_count; i++) {
        if (vm->dbg.source_map[i].line_no == line) {
            if (!file || vm->dbg.source_map[i].file == file) {
                return vm->text_seg + vm->dbg.source_map[i].pc_offset;
            }
        }
    }

    return NULL;
}

long long *cc_find_function_entry(JCC *vm, const char *name) {
    if (!name) {
        return NULL;
    }

    // Search through global symbols for function
    for (Obj *fn = vm->compiler.globals; fn; fn = fn->next) {
        if (fn->is_function && fn->name && strcmp(fn->name, name) == 0) {
            if (fn->code_addr >= 0) {
                return vm->text_seg + fn->code_addr;
            }
        }
    }

    return NULL;
}

DebugSymbol *cc_lookup_symbol(JCC *vm, const char *name) {
    if (!(vm->flags & JCC_ENABLE_DEBUGGER) || !name) {
        return NULL;
    }

    // Search in reverse order to find most recent (innermost scope)
    for (int i = vm->dbg.num_debug_symbols - 1; i >= 0; i--) {
        if (vm->dbg.debug_symbols[i].name && strcmp(vm->dbg.debug_symbols[i].name, name) == 0) {
            return &vm->dbg.debug_symbols[i];
        }
    }

    return NULL;
}

// ============================================================================
// Condition Evaluator for Conditional Breakpoints
// ============================================================================

// Evaluate an AST node in the current debugger context
// Returns the evaluated value, or 0 on error
static long long eval_ast_node(JCC *vm, Node *node, int *error) {
    if (!node) {
        *error = 1;
        return 0;
    }

    switch (node->kind) {
        case ND_NUM:
            return node->val;

        case ND_VAR: {
            // Look up variable in symbol table
            if (!node->var || !node->var->name) {
                printf("Error: Variable has no name\n");
                *error = 1;
                return 0;
            }

            DebugSymbol *sym = cc_lookup_symbol(vm, node->var->name);
            if (!sym) {
                printf("Error: Variable '%s' not found in current scope\n", node->var->name);
                *error = 1;
                return 0;
            }

            // Read value from memory
            long long *addr;
            if (sym->is_local) {
                // Local variable - BP-relative
                long long offset = sym->offset;
                // Adjust for stack canaries if enabled
                // Use same value as STACK_CANARY_SLOTS in vm.c
                if ((vm->flags & JCC_STACK_CANARIES) && offset < 0) {
                    offset -= 1;  // STACK_CANARY_SLOTS
                }
                addr = vm->bp + offset;
            } else {
                // Global variable - data segment
                addr = (long long *)(vm->data_seg + sym->offset);
            }

            // Read based on type size
            if (sym->ty) {
                switch (sym->ty->size) {
                    case 1: return (long long)*(char *)addr;
                    case 2: return (long long)*(short *)addr;
                    case 4: return (long long)*(int *)addr;
                    case 8: return *addr;
                    default: return *addr;
                }
            }
            return *addr;
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
            return left % right;
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
            return left < right;
        }

        case ND_LE: {
            long long left = eval_ast_node(vm, node->lhs, error);
            if (*error) return 0;
            long long right = eval_ast_node(vm, node->rhs, error);
            if (*error) return 0;
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

        default:
            printf("Error: Unsupported node kind %d in condition\n", node->kind);
            *error = 1;
            return 0;
    }
}

// Evaluate a condition string and return true/false
// Returns: 1 if condition is true, 0 if false or error
static int debugger_eval_condition(JCC *vm, const char *condition_str) {
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

    // Parse as expression
    Token *rest = NULL;
    Node *expr = cc_parse_expr(vm, &rest, tok);
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

int cc_add_watchpoint(JCC *vm, void *address, int size, int type, const char *expr) {
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
            vm->dbg.watchpoints[i].old_value = 0;  // Will be updated on first check
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

void cc_remove_watchpoint(JCC *vm, int index) {
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
int debugger_check_watchpoint(JCC *vm, void *addr, int size, int access_type) {
    // Safety checks
    if (!vm || !(vm->flags & JCC_ENABLE_DEBUGGER) || vm->dbg.num_watchpoints == 0 || !addr) {
        return -1;
    }

    // Don't check watchpoints if we're already in the debugger REPL
    if (vm->dbg.debugger_attached) {
        return -1;
    }

    // Don't check if VM isn't fully initialized (pc, bp, sp should be valid)
    if (!vm->pc || !vm->bp || !vm->sp || !vm->text_seg) {
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
