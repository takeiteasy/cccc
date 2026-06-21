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

#include "cccc.h"
#include "./internal.h"

// Bytecode file format (V2 - 32-bit text words):
//   Magic: "CCCC\0" (4 bytes)
//   Version: CCCC_VERSION (4 bytes)
//   Flags: CCCCFlags bitfield (4 bytes)
//   Text size: size in bytes (8 bytes)
//   Data size: size in bytes (8 bytes)
//   Main offset: instruction index of main() (8 bytes)
//   Data relocation count (8 bytes)
//   Text segment: bytecode (text_size bytes)
//   Data segment: global data (data_size bytes)
//   Data relocations: data_offset, target_segment, target_offset, addend
//   Return buffer count (8 bytes)
//   Return buffer size (8 bytes)
//   Return buffer offsets: data-segment byte offset for each buffer
//   FFI table count (8 bytes)
//   FFI entries: name_len (4 bytes), name (name_len bytes),
//                num_args (4), returns_double (4), returns_float (4), is_variadic (4),
//                num_fixed_args (4), double_arg_mask (8), is_dynamic_placeholder (4)
//   [V2] TLS template size (8 bytes)
//   [V2] TLS template bytes (tls_template_size bytes; pointer slots stripped to addend)
//   [V2] TLS relocation count (8 bytes)
//   [V2] TLS relocations: tls_offset, target_segment, target_offset, addend (4×8B each)

static int get_opcode_operand_count(int op) {
    return cc_opcode_operand_words(op);
}

static int get_instruction_word_count(InstrWord *text, long long pc,
                                      long long num_instructions) {
    int op = (int)text[pc];
    int operand_count = get_opcode_operand_count(op);
    if (operand_count < 0)
        return -1;
    if (pc + operand_count >= num_instructions)
        return -2;

    int word_count = operand_count + 1;
    if (op == JMPT) {
        Pc table_pc = text[pc + 1];
        InstrWord count = text[pc + 2];
        if (table_pc == pc + 4) {
            if (table_pc + (Pc)count > (Pc)num_instructions)
                return -2;
            word_count += (int)count;
        }
    }
    return word_count;
}

int cc_write_bytecode(VirtualMachine *vm, FILE *f) {
    if (!vm || !f) {
        fprintf(stderr, "error: invalid arguments to cc_write_bytecode\n");
        return -1;
    }

    if (!vm->text_seg || !vm->data_seg) {
        fprintf(stderr, "error: no bytecode to write (compile first)\n");
        return -1;
    }

    // Calculate sizes
    long long text_size = ((long long)vm->text_ptr + 1) * (long long)sizeof(InstrWord);
    long long data_size = vm->data_ptr - vm->data_seg;
    long long main_offset = vm->text_seg[0];  // main() instruction index
    long long num_instructions = text_size / (long long)sizeof(InstrWord);
    long long data_reloc_count = vm->compiler.num_data_relocs;
    long long return_buffer_count = vm->compiler.return_buffer_count;
    long long return_buffer_size = vm->compiler.return_buffer_size;

    if (return_buffer_count < 0 ||
        return_buffer_count > RETURN_BUFFER_POOL_SIZE ||
        return_buffer_size <= 0) {
        fprintf(stderr, "error: invalid return buffer metadata\n");
        return -1;
    }
    for (long long i = 0; i < return_buffer_count; i++) {
        long long offset = vm->compiler.return_buffer_offsets[i];
        if (offset < 0 || offset + return_buffer_size > data_size) {
            fprintf(stderr, "error: invalid return buffer offset\n");
            return -1;
        }
    }

    InstrWord *text_copy = malloc(text_size);
    if (!text_copy) {
        fprintf(stderr, "error: failed to allocate temporary buffer\n");
        return -1;
    }
    memcpy(text_copy, vm->text_seg, text_size);

    char *data_copy = NULL;
    if (data_size > 0) {
        data_copy = malloc(data_size);
        if (!data_copy) {
            fprintf(stderr, "error: failed to allocate temporary data buffer\n");
            free(text_copy);
            return -1;
        }
        memcpy(data_copy, vm->data_seg, data_size);
        for (long long i = 0; i < data_reloc_count; i++) {
            long long slot = vm->compiler.data_relocs[i].data_offset;
            if (slot < 0 || slot + (long long)sizeof(long long) > data_size) {
                fprintf(stderr, "error: invalid data relocation offset\n");
                free(data_copy);
                free(text_copy);
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
        return -1;
    }
    // Note: text_seg[0] is metadata (main entry offset), skip it
    is_operand[0] = 1;  // Mark position 0 as "operand" to skip it
    for (long long i = 1; i < num_instructions; i++) {
        if (is_operand[i]) continue;
        int op = text_copy[i];
        int word_count =
            get_instruction_word_count(text_copy, i, num_instructions);
        if (word_count == -1) {
            fprintf(stderr, "error: unknown opcode %d while writing bytecode\n", op);
            free(is_operand);
            free(data_copy);
            free(text_copy);
            return -1;
        }
        if (word_count < 0) {
            fprintf(stderr, "error: truncated opcode %d while writing bytecode\n", op);
            free(is_operand);
            free(data_copy);
            free(text_copy);
            return -1;
        }
        for (int j = 1; j < word_count && i + j < num_instructions; j++) {
            is_operand[i + j] = 1;
        }
    }

    free(is_operand);

    // Write header
    if (fwrite(CCCC_MAGIC, 1, 4, f) != 4) goto write_error;

    int version = CCCC_VERSION;
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

    if (fwrite(&return_buffer_count, sizeof(long long), 1, f) != 1) goto write_error;
    if (fwrite(&return_buffer_size, sizeof(long long), 1, f) != 1) goto write_error;
    for (long long i = 0; i < return_buffer_count; i++) {
        if (fwrite(&vm->compiler.return_buffer_offsets[i],
                   sizeof(long long), 1, f) != 1) goto write_error;
    }

    // Write FFI table (name + metadata per entry; func_ptr is resolved at load time)
    long long ffi_count = vm->compiler.ffi_count;
    if (fwrite(&ffi_count, sizeof(long long), 1, f) != 1) goto write_error;
    for (long long i = 0; i < ffi_count; i++) {
        ForeignFunc *ff = &vm->compiler.ffi_table[i];
        int name_len = ff->name ? (int)ff->name_len : 0;
        if (fwrite(&name_len, sizeof(int), 1, f) != 1) goto write_error;
        if (name_len > 0) {
            if (fwrite(ff->name, 1, name_len, f) != (size_t)name_len) goto write_error;
        }
        int num_args = ff->num_args;
        int returns_double = ff->returns_double;
        int returns_float = ff->returns_float;
        int is_variadic = ff->is_variadic;
        int num_fixed_args = ff->num_fixed_args;
        uint64_t double_arg_mask = ff->double_arg_mask;
        int is_dynamic_placeholder = ff->is_dynamic_placeholder;
        int is_asm_passthru = ff->is_asm_passthru;
        int asm_src_len = (is_asm_passthru && ff->asm_src)
                          ? (int)strlen(ff->asm_src) : 0;
        if (fwrite(&num_args, sizeof(int), 1, f) != 1) goto write_error;
        if (fwrite(&returns_double, sizeof(int), 1, f) != 1) goto write_error;
        if (fwrite(&returns_float, sizeof(int), 1, f) != 1) goto write_error;
        if (fwrite(&is_variadic, sizeof(int), 1, f) != 1) goto write_error;
        if (fwrite(&num_fixed_args, sizeof(int), 1, f) != 1) goto write_error;
        if (fwrite(&double_arg_mask, sizeof(uint64_t), 1, f) != 1) goto write_error;
        if (fwrite(&is_dynamic_placeholder, sizeof(int), 1, f) != 1) goto write_error;
        if (fwrite(&is_asm_passthru, sizeof(int), 1, f) != 1) goto write_error;
        if (fwrite(&asm_src_len, sizeof(int), 1, f) != 1) goto write_error;
        if (asm_src_len > 0) {
            if (fwrite(ff->asm_src, 1, asm_src_len, f) != (size_t)asm_src_len)
                goto write_error;
        }
    }

    // Write FFI policy (allow list, deny list, disable flag)
    {
        int disable_ffi = vm->disable_all_ffi;
        if (fwrite(&disable_ffi, sizeof(int), 1, f) != 1) goto write_error;

        int allow_count = vm->ffi_allow_count;
        if (fwrite(&allow_count, sizeof(int), 1, f) != 1) goto write_error;
        for (int i = 0; i < allow_count; i++) {
            int len = (int)strlen(vm->ffi_allow_list[i]);
            if (fwrite(&len, sizeof(int), 1, f) != 1) goto write_error;
            if (fwrite(vm->ffi_allow_list[i], 1, len, f) != (size_t)len) goto write_error;
        }

        int deny_count = vm->ffi_deny_count;
        if (fwrite(&deny_count, sizeof(int), 1, f) != 1) goto write_error;
        for (int i = 0; i < deny_count; i++) {
            int len = (int)strlen(vm->ffi_deny_list[i]);
            if (fwrite(&len, sizeof(int), 1, f) != 1) goto write_error;
            if (fwrite(vm->ffi_deny_list[i], 1, len, f) != (size_t)len) goto write_error;
        }
    }

    // Write TLS section (V2): template bytes + relocation table (#493)
    {
        long long tls_size = (long long)vm->tls_template_size;
        long long tls_reloc_count = vm->compiler.num_tls_relocs;
        if (fwrite(&tls_size, sizeof(long long), 1, f) != 1) goto write_error;

        if (tls_size > 0) {
            // Strip absolute pointer values (replace with addend, matching the
            // data-segment treatment) so the template is position-independent.
            char *tls_copy = malloc((size_t)tls_size);
            if (!tls_copy) {
                fprintf(stderr, "error: failed to allocate TLS template buffer\n");
                return -1;
            }
            memcpy(tls_copy, vm->tls_template, (size_t)tls_size);
            for (long long i = 0; i < tls_reloc_count; i++) {
                long long slot = vm->compiler.tls_relocs[i].tls_offset;
                *(long long *)(tls_copy + slot) = vm->compiler.tls_relocs[i].addend;
            }
            if (fwrite(tls_copy, 1, (size_t)tls_size, f) != (size_t)tls_size) {
                free(tls_copy);
                goto write_error;
            }
            free(tls_copy);
        }

        if (fwrite(&tls_reloc_count, sizeof(long long), 1, f) != 1) goto write_error;
        for (long long i = 0; i < tls_reloc_count; i++) {
            long long tls_offset    = vm->compiler.tls_relocs[i].tls_offset;
            long long target_seg    = vm->compiler.tls_relocs[i].target_segment;
            long long target_offset = vm->compiler.tls_relocs[i].target_offset;
            long long addend        = vm->compiler.tls_relocs[i].addend;
            if (fwrite(&tls_offset,    sizeof(long long), 1, f) != 1) goto write_error;
            if (fwrite(&target_seg,    sizeof(long long), 1, f) != 1) goto write_error;
            if (fwrite(&target_offset, sizeof(long long), 1, f) != 1) goto write_error;
            if (fwrite(&addend,        sizeof(long long), 1, f) != 1) goto write_error;
        }
    }

    if (vm->debug_vm) {
        printf("  Text size: %lld bytes (%lld instructions)\n", text_size, num_instructions);
        printf("  Data size: %lld bytes\n", data_size);
        printf("  Data relocations: %lld\n", data_reloc_count);
        printf("  TLS template: %zu bytes, %d relocs\n",
               vm->tls_template_size, vm->compiler.num_tls_relocs);
        printf("  Return buffers: %lld x %lld bytes\n", return_buffer_count,
               return_buffer_size);
        printf("  FFI entries: %lld\n", ffi_count);
        printf("  FFI allow: %d  deny: %d  disable: %d\n",
               vm->ffi_allow_count, vm->ffi_deny_count, vm->disable_all_ffi);
        printf("  Main offset: %lld\n", main_offset);
    }

    return 0;

write_error:
    fprintf(stderr, "error: failed to write bytecode: %s\n", strerror(errno));
    if (data_copy) free(data_copy);
    if (text_copy) free(text_copy);
    return -1;
}

int cc_save_bytecode(VirtualMachine *vm, const char *path) {
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

    int rc = cc_write_bytecode(vm, f);
    fclose(f);
    if (rc != 0)
        return -1;

    if (vm->debug_vm) {
        printf("Saved bytecode to %s\n", path);
    }

    return 0;
}

static int load_bytecode(VirtualMachine *vm, const char *data, size_t size) {
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
    if (cursor + 4 > end || memcmp(cursor, CCCC_MAGIC, 4) != 0) {
        fprintf(stderr, "error: invalid bytecode file (bad magic)\n");
        return -1;
    }
    cursor += 4;

    // Read version - only accept the current 32-bit instruction format.
    READ_AND_INCR(version, int);
    if (version != CCCC_VERSION) {
        fprintf(stderr, "error: unsupported bytecode version %d (expected %d)\n",
                version, CCCC_VERSION);
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
        cursor + text_size + data_size +
            data_reloc_count * 4 * (long long)sizeof(long long) > end ||
        text_size > vm->poolsize_max * (long long)sizeof(InstrWord) ||
        text_size % (long long)sizeof(InstrWord) != 0 ||
        data_size > vm->poolsize_max) {
        fprintf(stderr, "error: invalid bytecode sizes\n");
        return -1;
    }

    // Reserve and commit all segments (base pointers will never move)
    vm_alloc_segments(vm);

    // Ensure enough committed space for the bytecode being loaded
    Pc num_text_words = (Pc)(text_size / (long long)sizeof(InstrWord));
    if (vm_text_ensure_count(vm, num_text_words) != 0) {
        fprintf(stderr, "error: could not commit text segment for bytecode\n");
        return -1;
    }
    if (vm_data_ensure(vm, data_size) != 0) {
        fprintf(stderr, "error: could not commit data segment for bytecode\n");
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
    if (data_reloc_count > 0) {
        void *_tmp = realloc(vm->compiler.data_relocs,
                             (size_t)data_reloc_count *
                             sizeof(*vm->compiler.data_relocs));
        if (!_tmp) {
            fprintf(stderr, "error: out of memory for data relocations\n");
            return -1;
        }
        vm->compiler.data_relocs = _tmp;
        vm->compiler.data_relocs_cap = (int)data_reloc_count;
    }
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

    READ_AND_INCR(return_buffer_count, long long);
    READ_AND_INCR(return_buffer_size, long long);
    if (return_buffer_count < 0 ||
        return_buffer_count > RETURN_BUFFER_POOL_SIZE ||
        return_buffer_size <= 0 ||
        cursor + return_buffer_count * (long long)sizeof(long long) > end) {
        fprintf(stderr, "error: invalid return buffer metadata\n");
        return -1;
    }

    vm->compiler.return_buffer_count = (int)return_buffer_count;
    vm->compiler.return_buffer_size = (int)return_buffer_size;
    vm->compiler.return_buffer_index = 0;
    vm->runtime_return_buffer_index = 0;
    for (int i = 0; i < RETURN_BUFFER_POOL_SIZE; i++) {
        vm->compiler.return_buffer_offsets[i] = -1;
        vm->compiler.return_buffer_pool[i] = NULL;
    }
    for (long long i = 0; i < return_buffer_count; i++) {
        READ_AND_INCR(return_buffer_offset, long long);
        if (return_buffer_offset < 0 ||
            return_buffer_offset + return_buffer_size > data_size) {
            fprintf(stderr, "error: invalid return buffer offset\n");
            return -1;
        }
        vm->compiler.return_buffer_offsets[i] = return_buffer_offset;
        vm->compiler.return_buffer_pool[i] = vm->data_seg + return_buffer_offset;
    }

    // Read FFI table (name + metadata; func_ptr is resolved by the caller via
    // cc_load_libc / load_requested_libraries)
    READ_AND_INCR(ffi_count, long long);
    if (ffi_count < 0 || ffi_count > MAX_CALLS) {
        fprintf(stderr, "error: invalid FFI count %lld in bytecode\n", ffi_count);
        return -1;
    }
    if (ffi_count > vm->compiler.ffi_capacity) {
        vm->compiler.ffi_capacity = (int)ffi_count;
        vm->compiler.ffi_table = realloc(vm->compiler.ffi_table,
            vm->compiler.ffi_capacity * sizeof(ForeignFunc));
        if (!vm->compiler.ffi_table) {
            fprintf(stderr, "error: failed to allocate FFI table\n");
            return -1;
        }
    }
    for (long long i = 0; i < ffi_count; i++) {
        READ_AND_INCR(name_len, int);
        // Fixed-width fields: 7 ints + 1 uint64_t (plus optional asm_src bytes)
        if (name_len < 0 || name_len > 4096 ||
            cursor + name_len + 7 * (long long)sizeof(int) + (long long)sizeof(uint64_t) > end) {
            fprintf(stderr, "error: invalid FFI entry header at index %lld\n", i);
            return -1;
        }
        char *name = NULL;
        size_t actual_name_len = 0;
        if (name_len > 0) {
            name = malloc((size_t)name_len + 1);
            if (!name) {
                fprintf(stderr, "error: failed to allocate FFI entry name\n");
                return -1;
            }
            memcpy(name, cursor, (size_t)name_len);
            name[name_len] = '\0';
            cursor += name_len;
            actual_name_len = (size_t)name_len;
        }
        READ_AND_INCR(num_args_i, int);
        READ_AND_INCR(returns_double_i, int);
        READ_AND_INCR(returns_float_i, int);
        READ_AND_INCR(is_variadic_i, int);
        READ_AND_INCR(num_fixed_args_i, int);
        READ_AND_INCR(double_arg_mask, uint64_t);
        READ_AND_INCR(is_dynamic_placeholder_i, int);
        READ_AND_INCR(is_asm_passthru_i, int);
        READ_AND_INCR(asm_src_len_i, int);
        char *asm_src = NULL;
        if (asm_src_len_i < 0 || asm_src_len_i > 65536 ||
            cursor + asm_src_len_i > end) {
            fprintf(stderr, "error: invalid asm_src length %d at FFI index %lld\n",
                    asm_src_len_i, i);
            free(name);
            return -1;
        }
        if (asm_src_len_i > 0) {
            asm_src = malloc((size_t)asm_src_len_i + 1);
            if (!asm_src) {
                fprintf(stderr, "error: failed to allocate FFI asm_src\n");
                free(name);
                return -1;
            }
            memcpy(asm_src, cursor, (size_t)asm_src_len_i);
            asm_src[asm_src_len_i] = '\0';
            cursor += asm_src_len_i;
        }

        ForeignFunc *ff = &vm->compiler.ffi_table[i];
        ff->name = name;
        ff->name_len = actual_name_len;
        ff->func_ptr = NULL;
        ff->num_args = num_args_i;
        ff->returns_double = returns_double_i;
        ff->returns_float = returns_float_i;
        ff->is_variadic = is_variadic_i;
        ff->num_fixed_args = num_fixed_args_i;
        ff->double_arg_mask = double_arg_mask;
        ff->is_dynamic_placeholder = is_dynamic_placeholder_i;
        ff->is_asm_passthru = is_asm_passthru_i;
        ff->asm_src = asm_src;
    }
    vm->compiler.ffi_count = (int)ffi_count;

    // Read FFI policy (allow list, deny list, disable flag)
    {
        READ_AND_INCR(disable_ffi_i, int);
        vm->disable_all_ffi |= disable_ffi_i;

        READ_AND_INCR(allow_count_i, int);
        if (allow_count_i < 0 || allow_count_i > MAX_CALLS) {
            fprintf(stderr, "error: invalid FFI allow count in bytecode\n");
            return -1;
        }
        for (int i = 0; i < allow_count_i; i++) {
            READ_AND_INCR(len, int);
            if (len < 0 || len > 4096 || cursor + len > end) {
                fprintf(stderr, "error: invalid FFI allow entry at index %d\n", i);
                return -1;
            }
            char *name = malloc((size_t)len + 1);
            if (!name) {
                fprintf(stderr, "error: failed to allocate FFI allow entry\n");
                return -1;
            }
            memcpy(name, cursor, (size_t)len);
            name[len] = '\0';
            cursor += len;
            cc_ffi_allow(vm, name);
            free(name);
        }

        READ_AND_INCR(deny_count_i, int);
        if (deny_count_i < 0 || deny_count_i > MAX_CALLS) {
            fprintf(stderr, "error: invalid FFI deny count in bytecode\n");
            return -1;
        }
        for (int i = 0; i < deny_count_i; i++) {
            READ_AND_INCR(len, int);
            if (len < 0 || len > 4096 || cursor + len > end) {
                fprintf(stderr, "error: invalid FFI deny entry at index %d\n", i);
                return -1;
            }
            char *name = malloc((size_t)len + 1);
            if (!name) {
                fprintf(stderr, "error: failed to allocate FFI deny entry\n");
                return -1;
            }
            memcpy(name, cursor, (size_t)len);
            name[len] = '\0';
            cursor += len;
            cc_ffi_deny(vm, name);
            free(name);
        }
    }

    // Read TLS section (V2): template bytes + reloc table (#493)
    {
        READ_AND_INCR(tls_size_i, long long);
        if (tls_size_i < 0) {
            fprintf(stderr, "error: invalid TLS template size in bytecode\n");
            return -1;
        }
        if (tls_size_i > 0) {
            if (cursor + tls_size_i > end) {
                fprintf(stderr, "error: TLS template extends past end of bytecode\n");
                return -1;
            }
            // Allocate and populate tls_template from the file
            free(vm->tls_template);
            vm->tls_template = malloc((size_t)tls_size_i);
            if (!vm->tls_template) {
                fprintf(stderr, "error: failed to allocate TLS template\n");
                return -1;
            }
            memcpy(vm->tls_template, cursor, (size_t)tls_size_i);
            vm->tls_template_size = (size_t)tls_size_i;
            vm->tls_template_cap  = (size_t)tls_size_i;
            cursor += tls_size_i;
        } else {
            // No TLS in this bytecode
            vm->tls_template_size = 0;
        }

        READ_AND_INCR(tls_reloc_count_i, long long);
        if (tls_reloc_count_i < 0 || tls_reloc_count_i > MAX_CALLS ||
            cursor + tls_reloc_count_i * 4 * (long long)sizeof(long long) > end) {
            fprintf(stderr, "error: invalid TLS relocation count in bytecode\n");
            return -1;
        }
        vm->compiler.num_tls_relocs = 0;
        if (tls_reloc_count_i > 0) {
            void *_tmp = realloc(vm->compiler.tls_relocs,
                                 (size_t)tls_reloc_count_i *
                                 sizeof(*vm->compiler.tls_relocs));
            if (!_tmp) {
                fprintf(stderr, "error: out of memory for TLS relocations\n");
                return -1;
            }
            vm->compiler.tls_relocs = _tmp;
            vm->compiler.tls_relocs_cap = (int)tls_reloc_count_i;
        }
        for (long long i = 0; i < tls_reloc_count_i; i++) {
            READ_AND_INCR(tls_offset,    long long);
            READ_AND_INCR(target_seg,    long long);
            READ_AND_INCR(target_offset, long long);
            READ_AND_INCR(addend,        long long);
            if (tls_size_i > 0 &&
                (tls_offset < 0 ||
                 tls_offset + (long long)sizeof(long long) > tls_size_i ||
                 (target_seg != 0 && target_seg != 1))) {
                fprintf(stderr, "error: invalid TLS relocation record\n");
                return -1;
            }
            vm->compiler.tls_relocs[vm->compiler.num_tls_relocs].tls_offset    = tls_offset;
            vm->compiler.tls_relocs[vm->compiler.num_tls_relocs].target_segment = (int)target_seg;
            vm->compiler.tls_relocs[vm->compiler.num_tls_relocs].target_offset  = target_offset;
            vm->compiler.tls_relocs[vm->compiler.num_tls_relocs].addend         = addend;
            vm->compiler.num_tls_relocs++;
        }
    }

    long long num_instructions = text_size / (long long)sizeof(InstrWord);

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
        int word_count =
            get_instruction_word_count(vm->text_seg, i, num_instructions);
        if (word_count == -1) {
            fprintf(stderr, "error: unknown opcode %d in bytecode\n", op);
            free(is_operand);
            return -1;
        }
        if (word_count < 0) {
            fprintf(stderr, "error: truncated opcode %d in bytecode\n", op);
            free(is_operand);
            return -1;
        }
        for (int j = 1; j < word_count && i + j < num_instructions; j++) {
            is_operand[i + j] = 1;
        }
    }

    free(is_operand);

    // Set up pointers (heap_ptr/heap_end/free_list already set by vm_alloc_segments)
    vm->text_ptr = (Pc)(text_size / (long long)sizeof(InstrWord)) - 1;
    vm->data_ptr = vm->data_seg + data_size;
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

    // Apply TLS relocations onto tls_template (#493)
    for (int i = 0; i < vm->compiler.num_tls_relocs; i++) {
        long long value;
        if (vm->compiler.tls_relocs[i].target_segment == 0) {
            value = (long long)(vm->data_seg +
                                vm->compiler.tls_relocs[i].target_offset +
                                vm->compiler.tls_relocs[i].addend);
        } else {
            value = vm->compiler.tls_relocs[i].target_offset +
                    vm->compiler.tls_relocs[i].addend;
        }
        *(long long *)(vm->tls_template + vm->compiler.tls_relocs[i].tls_offset) =
            value;
    }

    if (vm->debug_vm) {
        printf("Loaded bytecode:\n");
        printf("  Text size: %lld bytes (%lld instructions)\n", text_size, num_instructions);
        printf("  Data size: %lld bytes\n", data_size);
        printf("  Data relocations: %lld\n", data_reloc_count);
        printf("  TLS template: %zu bytes, %d relocs\n",
               vm->tls_template_size, vm->compiler.num_tls_relocs);
        printf("  Return buffers: %lld x %lld bytes\n", return_buffer_count,
               return_buffer_size);
        printf("  FFI allow: %d  deny: %d  disable: %d\n",
               vm->ffi_allow_count, vm->ffi_deny_count, vm->disable_all_ffi);
        printf("  Main offset: %lld\n", main_offset);
    }

    return 0;
#undef READ_AND_INCR
}

int cc_load_bytecode(VirtualMachine *vm, const char *path) {
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

static int source_index_cmp(const void *a, const void *b) {
    const SourceIndex *sa = (const SourceIndex *)a;
    const SourceIndex *sb = (const SourceIndex *)b;
    if (sa->file != sb->file)
        return ((uintptr_t)sa->file < (uintptr_t)sb->file) ? -1 : 1;
    return (sa->line_no < sb->line_no) ? -1 : (sa->line_no > sb->line_no) ? 1 : 0;
}

void cc_compile(VirtualMachine *vm, Obj *prog) {
    if (!vm) {
        error("VM instance is NULL");
    }

    // Initialize VM memory if not already done
    if (!vm->text_seg) {
        // Reserve and commit all segments (base pointers will never move)
        vm_alloc_segments(vm);

        // Initialize codegen state
        vm->compiler.current_codegen_fn = NULL;

        // Initialize source map for debugger (if enabled)
        if (vm->flags & CCCC_ENABLE_DEBUGGER) {
            vm->dbg.source_map_capacity = 1024;
            vm->dbg.source_map = malloc(vm->dbg.source_map_capacity * sizeof(SourceMap));
            if (!vm->dbg.source_map) {
                error("could not malloc for source map");
            }
            vm->dbg.source_map_count = 0;
            vm->dbg.last_debug_file = NULL;
            vm->dbg.last_debug_line = -1;
            vm->dbg.source_index = NULL;
            vm->dbg.source_index_count = 0;
            vm->dbg.num_debug_symbols = 0;
            vm->dbg.num_watchpoints = 0;
        }
    }

    // Store the merged program for variable lookup during codegen
    vm->compiler.globals = prog;

    // Generate bytecode from AST using new register-based codegen
    gen(vm, prog);

    // Run optimizer if enabled at the global level, or if any function carries
    // a per-function [[cccc::optimize]] attribute (which may request a level
    // higher than the current global, e.g. O2 under a global O0 build).
    if (vm->compiler.opt_level > 0 || vm->compiler.fuse_ops ||
        vm->compiler.have_fn_opt_attrs) {
        cc_optimize(vm, vm->compiler.opt_level, vm->compiler.fuse_ops);
    }

    // Build source index for O(log n) line→PC lookups
    if (vm->flags & CCCC_ENABLE_DEBUGGER && vm->dbg.source_map_count > 0) {
        vm->dbg.source_index_count = 0;

        // First pass: count unique (file, line) pairs
        File *last_file = NULL;
        int last_line = -1;
        for (int i = 0; i < vm->dbg.source_map_count; i++) {
            SourceMap *m = &vm->dbg.source_map[i];
            if (m->file != last_file || m->line_no != last_line) {
                vm->dbg.source_index_count++;
                last_file = m->file;
                last_line = m->line_no;
            }
        }

        vm->dbg.source_index = malloc(vm->dbg.source_index_count * sizeof(SourceIndex));
        if (!vm->dbg.source_index) {
            error("could not malloc for source index");
        }

        // Fill with first PC for each unique (file, line)
        int idx = 0;
        last_file = NULL;
        last_line = -1;
        for (int i = 0; i < vm->dbg.source_map_count; i++) {
            SourceMap *m = &vm->dbg.source_map[i];
            if (m->file != last_file || m->line_no != last_line) {
                vm->dbg.source_index[idx].file = m->file;
                vm->dbg.source_index[idx].line_no = m->line_no;
                vm->dbg.source_index[idx].first_pc = (Pc)m->pc_offset;
                idx++;
                last_file = m->file;
                last_line = m->line_no;
            }
        }

        // Sort by (file, line_no) for binary search
        qsort(vm->dbg.source_index, vm->dbg.source_index_count, sizeof(SourceIndex),
              source_index_cmp);

        // Dedup: after sorting, identical (file, line) pairs are adjacent.
        // Keep only the entry with the lowest first_pc per pair.
        int dedup_count = 0;
        for (int i = 0; i < vm->dbg.source_index_count; i++) {
            SourceIndex *cur = &vm->dbg.source_index[i];
            if (dedup_count > 0) {
                SourceIndex *last = &vm->dbg.source_index[dedup_count - 1];
                if (last->file == cur->file && last->line_no == cur->line_no) {
                    if (cur->first_pc < last->first_pc)
                        last->first_pc = cur->first_pc;
                    continue;
                }
            }
            vm->dbg.source_index[dedup_count++] = *cur;
        }
        vm->dbg.source_index_count = dedup_count;
    }
}
