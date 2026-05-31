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

#include "jcc.h"
#include "./internal.h"

// Bytecode file format (V4 - 32-bit text words):
//   Magic: "JCC\0" (4 bytes)
//   Version: 1 (4 bytes)
//   Flags: JCCFlags bitfield (4 bytes)
//   Text size: size in bytes (8 bytes)
//   Data size: size in bytes (8 bytes)
//   Main offset: instruction index of main() (8 bytes)
//   Data relocation count (8 bytes)
//   Text segment: bytecode (text_size bytes)
//   Data segment: global data (data_size bytes)
//   Data relocations: data_offset, target_segment, target_offset, addend

static int get_opcode_operand_count(int op) {
    return cc_opcode_operand_words(op);
}

int cc_save_bytecode(JCC *vm, const char *path) {
    if (!vm || !path) {
        fprintf(stderr, "error: invalid arguments to cc_save_bytecode\n");
        return -1;
    }

    if (!vm->text_seg || !vm->data_seg) {
        fprintf(stderr, "error: no bytecode to save (compile first)\n");
        return -1;
    }

    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "error: failed to open %s for writing: %s\n", path, strerror(errno));
        return -1;
    }

    // Calculate sizes
    long long text_size = ((long long)vm->text_ptr + 1) * (long long)sizeof(JCCInstrWord);
    long long data_size = vm->data_ptr - vm->data_seg;
    long long main_offset = vm->text_seg[0];  // main() instruction index
    long long num_instructions = text_size / (long long)sizeof(JCCInstrWord);
    long long data_reloc_count = vm->compiler.num_data_relocs;

    JCCInstrWord *text_copy = malloc(text_size);
    if (!text_copy) {
        fprintf(stderr, "error: failed to allocate temporary buffer\n");
        fclose(f);
        return -1;
    }
    memcpy(text_copy, vm->text_seg, text_size);

    char *data_copy = NULL;
    if (data_size > 0) {
        data_copy = malloc(data_size);
        if (!data_copy) {
            fprintf(stderr, "error: failed to allocate temporary data buffer\n");
            free(text_copy);
            fclose(f);
            return -1;
        }
        memcpy(data_copy, vm->data_seg, data_size);
        for (long long i = 0; i < data_reloc_count; i++) {
            long long slot = vm->compiler.data_relocs[i].data_offset;
            if (slot < 0 || slot + (long long)sizeof(long long) > data_size) {
                fprintf(stderr, "error: invalid data relocation offset\n");
                free(data_copy);
                free(text_copy);
                fclose(f);
                return -1;
            }
            *(long long *)(data_copy + slot) = vm->compiler.data_relocs[i].addend;
        }
    }

    // Mark which positions are operands (not opcodes)
    char *is_operand = calloc(num_instructions, 1);
    if (!is_operand) {
        fprintf(stderr, "error: failed to allocate operand map\n");
        free(text_copy);
        fclose(f);
        return -1;
    }
    // Note: text_seg[0] is metadata (main entry offset), skip it
    is_operand[0] = 1;  // Mark position 0 as "operand" to skip it
    for (long long i = 1; i < num_instructions; i++) {
        if (is_operand[i]) continue;
        int op = text_copy[i];
        int operand_count = get_opcode_operand_count(op);
        if (operand_count < 0) {
            fprintf(stderr, "error: unknown opcode %d while saving bytecode\n", op);
            free(is_operand);
            free(data_copy);
            free(text_copy);
            fclose(f);
            return -1;
        }
        if (i + operand_count >= num_instructions) {
            fprintf(stderr, "error: truncated opcode %d while saving bytecode\n", op);
            free(is_operand);
            free(data_copy);
            free(text_copy);
            fclose(f);
            return -1;
        }
        for (int j = 1; j <= operand_count && i + j < num_instructions; j++) {
            is_operand[i + j] = 1;
        }
    }

    free(is_operand);

    // Write header
    if (fwrite(JCC_MAGIC, 1, 4, f) != 4) goto write_error;

    int version = JCC_VERSION;
    if (fwrite(&version, sizeof(int), 1, f) != 1) goto write_error;

    uint32_t flags = vm->flags;
    if (fwrite(&flags, sizeof(uint32_t), 1, f) != 1) goto write_error;

    if (fwrite(&text_size, sizeof(long long), 1, f) != 1) goto write_error;
    if (fwrite(&data_size, sizeof(long long), 1, f) != 1) goto write_error;
    if (fwrite(&main_offset, sizeof(long long), 1, f) != 1) goto write_error;
    if (fwrite(&data_reloc_count, sizeof(long long), 1, f) != 1) goto write_error;

    // Write text segment
    if (fwrite(text_copy, 1, text_size, f) != (size_t)text_size) goto write_error;
    free(text_copy);
    text_copy = NULL;

    // Write data segment
    if (data_size > 0) {
        if (fwrite(data_copy, 1, data_size, f) != (size_t)data_size) goto write_error;
    }
    free(data_copy);
    data_copy = NULL;

    for (long long i = 0; i < data_reloc_count; i++) {
        long long target_segment = vm->compiler.data_relocs[i].target_segment;
        if (fwrite(&vm->compiler.data_relocs[i].data_offset,
                   sizeof(long long), 1, f) != 1) goto write_error;
        if (fwrite(&target_segment, sizeof(long long), 1, f) != 1) goto write_error;
        if (fwrite(&vm->compiler.data_relocs[i].target_offset,
                   sizeof(long long), 1, f) != 1) goto write_error;
        if (fwrite(&vm->compiler.data_relocs[i].addend,
                   sizeof(long long), 1, f) != 1) goto write_error;
    }

    fclose(f);

    if (vm->debug_vm) {
        printf("Saved bytecode to %s:\n", path);
        printf("  Text size: %lld bytes (%lld instructions)\n", text_size, num_instructions);
        printf("  Data size: %lld bytes\n", data_size);
        printf("  Data relocations: %lld\n", data_reloc_count);
        printf("  Main offset: %lld\n", main_offset);
    }

    return 0;

write_error:
    fprintf(stderr, "error: failed to write bytecode: %s\n", strerror(errno));
    if (data_copy) free(data_copy);
    if (text_copy) free(text_copy);
    fclose(f);
    return -1;
}

static int load_bytecode(JCC *vm, const char *data, size_t size) {
    const char *cursor = data;
    const char *end = data + size;

#define READ_AND_INCR(VAR, TYPE)                                     \
    if (cursor + sizeof(TYPE) > end) {                               \
        fprintf(stderr, "error: unexpected end of bytecode data\n"); \
        return -1;                                                   \
    }                                                                \
    TYPE VAR = *(TYPE *)cursor;                                      \
    cursor += sizeof(TYPE);

    // Read magic
    if (cursor + 4 > end || memcmp(cursor, JCC_MAGIC, 4) != 0) {
        fprintf(stderr, "error: invalid bytecode file (bad magic)\n");
        return -1;
    }
    cursor += 4;

    // Read version - only accept the current 32-bit instruction format.
    READ_AND_INCR(version, int);
    if (version != JCC_VERSION) {
        fprintf(stderr, "error: unsupported bytecode version %d (expected %d)\n",
                version, JCC_VERSION);
        return -1;
    }

    // Read flags
    READ_AND_INCR(flags, uint32_t);
    vm->flags = flags;

    // Read sizes
    READ_AND_INCR(text_size, long long);
    READ_AND_INCR(data_size, long long);
    READ_AND_INCR(main_offset, long long);
    READ_AND_INCR(data_reloc_count, long long);

    if (text_size < 0 || data_size < 0 || data_reloc_count < 0 ||
        data_reloc_count > MAX_CALLS ||
        cursor + text_size + data_size + data_reloc_count * 4 * (long long)sizeof(long long) > end ||
        text_size > vm->poolsize * (long long)sizeof(JCCInstrWord) ||
        text_size % (long long)sizeof(JCCInstrWord) != 0 ||
        data_size > vm->poolsize) {
        fprintf(stderr, "error: invalid bytecode sizes\n");
        return -1;
    }

    // Allocate segments
    vm->text_seg = calloc(vm->poolsize, sizeof(JCCInstrWord));
    vm->data_seg = calloc(vm->poolsize, 1);
    vm->stack_seg = calloc(vm->poolsize, sizeof(long long));
    vm->heap_seg = calloc(vm->poolsize, 1);
    if (!vm->text_seg || !vm->data_seg || !vm->stack_seg || !vm->heap_seg) {
        fprintf(stderr, "error: failed to allocate memory segments\n");
        return -1;
    }

    // Copy text segment
    memcpy(vm->text_seg, cursor, text_size);
    cursor += text_size;

    // Copy data segment
    if (data_size > 0) {
        memcpy(vm->data_seg, cursor, data_size);
        cursor += data_size;
    }

    vm->compiler.num_data_relocs = 0;
    for (long long i = 0; i < data_reloc_count; i++) {
        READ_AND_INCR(data_offset, long long);
        READ_AND_INCR(target_segment, long long);
        READ_AND_INCR(target_offset, long long);
        READ_AND_INCR(addend, long long);

        if (data_offset < 0 ||
            data_offset + (long long)sizeof(long long) > data_size ||
            (target_segment != 0 && target_segment != 1)) {
            fprintf(stderr, "error: invalid data relocation record\n");
            return -1;
        }

        vm->compiler.data_relocs[vm->compiler.num_data_relocs].data_offset =
            data_offset;
        vm->compiler.data_relocs[vm->compiler.num_data_relocs].target_segment =
            (int)target_segment;
        vm->compiler.data_relocs[vm->compiler.num_data_relocs].target_offset =
            target_offset;
        vm->compiler.data_relocs[vm->compiler.num_data_relocs].addend = addend;
        vm->compiler.num_data_relocs++;
    }

    long long num_instructions = text_size / (long long)sizeof(JCCInstrWord);

    // Mark operand positions
    char *is_operand = calloc(num_instructions, 1);
    if (!is_operand) {
        fprintf(stderr, "error: failed to allocate operand map\n");
        return -1;
    }
    // Note: text_seg[0] is metadata (main entry offset), skip it
    is_operand[0] = 1;  // Mark position 0 as "operand" to skip it
    for (long long i = 1; i < num_instructions; i++) {
        if (is_operand[i]) continue;
        int op = vm->text_seg[i];
        int operand_count = get_opcode_operand_count(op);
        if (operand_count < 0) {
            fprintf(stderr, "error: unknown opcode %d in bytecode\n", op);
            free(is_operand);
            return -1;
        }
        if (i + operand_count >= num_instructions) {
            fprintf(stderr, "error: truncated opcode %d in bytecode\n", op);
            free(is_operand);
            return -1;
        }
        for (int j = 1; j <= operand_count && i + j < num_instructions; j++) {
            is_operand[i + j] = 1;
        }
    }

    free(is_operand);

    // Set up pointers
    vm->text_ptr = (JCCPc)(text_size / (long long)sizeof(JCCInstrWord)) - 1;
    vm->data_ptr = vm->data_seg + data_size;
    vm->heap_ptr = vm->heap_seg;
    vm->heap_end = vm->heap_seg + vm->poolsize;
    vm->free_list = NULL;
    vm->text_seg[0] = main_offset;  // Restore main offset

    for (int i = 0; i < vm->compiler.num_data_relocs; i++) {
        long long value;
        if (vm->compiler.data_relocs[i].target_segment == 0) {
            value = (long long)(vm->data_seg +
                                vm->compiler.data_relocs[i].target_offset +
                                vm->compiler.data_relocs[i].addend);
        } else {
            value = vm->compiler.data_relocs[i].target_offset +
                    vm->compiler.data_relocs[i].addend;
        }
        *(long long *)(vm->data_seg + vm->compiler.data_relocs[i].data_offset) =
            value;
    }

    if (vm->debug_vm) {
        printf("Loaded bytecode:\n");
        printf("  Text size: %lld bytes (%lld instructions)\n", text_size, num_instructions);
        printf("  Data size: %lld bytes\n", data_size);
        printf("  Data relocations: %lld\n", data_reloc_count);
        printf("  Main offset: %lld\n", main_offset);
    }

    return 0;
#undef READ_AND_INCR
}

int cc_load_bytecode(JCC *vm, const char *path) {
    if (!vm || !path) {
        fprintf(stderr, "error: invalid arguments to cc_load_bytecode\n");
        return -1;
    }

    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "error: failed to open %s: %s\n", path, strerror(errno));
        return -1;
    }

    fseek(f, 0, SEEK_END);
    size_t file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *data = malloc(file_size);
    if (!data) {
        fprintf(stderr, "error: failed to allocate memory for bytecode\n");
        fclose(f);
        return -1;
    }

    if (fread(data, 1, file_size, f) != file_size) {
        fprintf(stderr, "error: failed to read bytecode file\n");
        free(data);
        fclose(f);
        return -1;
    }

    int result = load_bytecode(vm, data, file_size);
    free(data);
    fclose(f);
    return result;
}

void cc_compile(JCC *vm, Obj *prog) {
    if (!vm) {
        error("VM instance is NULL");
    }

    // Initialize VM memory if not already done
    if (!vm->text_seg) {
        // allocate memory
        if (!(vm->text_seg = malloc(vm->poolsize * sizeof(JCCInstrWord)))) {
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

        memset(vm->text_seg, 0, vm->poolsize * sizeof(JCCInstrWord));
        memset(vm->data_seg, 0, vm->poolsize);
        memset(vm->stack_seg, 0, vm->poolsize * sizeof(long long));
        memset(vm->heap_seg, 0, vm->poolsize);

        if (vm->flags & JCC_CFI) {
            memset(vm->shadow_stack, 0, vm->poolsize * sizeof(long long));
        }

        vm->old_text_seg = vm->text_seg;
        vm->text_ptr = 0;
        vm->data_ptr = vm->data_seg;
        vm->heap_ptr = vm->heap_seg;
        vm->heap_end = vm->heap_seg + vm->poolsize;
        vm->free_list = NULL;

        // Initialize codegen state
        vm->compiler.current_codegen_fn = NULL;

        // Initialize source map for debugger (if enabled)
        if (vm->flags & JCC_ENABLE_DEBUGGER) {
            vm->dbg.source_map_capacity = 1024;
            vm->dbg.source_map = malloc(vm->dbg.source_map_capacity * sizeof(SourceMap));
            if (!vm->dbg.source_map) {
                error("could not malloc for source map");
            }
            vm->dbg.source_map_count = 0;
            vm->dbg.last_debug_file = NULL;
            vm->dbg.last_debug_line = -1;
            vm->dbg.num_debug_symbols = 0;
            vm->dbg.num_watchpoints = 0;
        }
    }

    // Store the merged program for variable lookup during codegen
    vm->compiler.globals = prog;

    // Generate bytecode from AST using new register-based codegen
    gen(vm, prog);

    // Run optimizer if enabled
    if (vm->compiler.opt_level > 0) {
        cc_optimize(vm, vm->compiler.opt_level);
    }
}
