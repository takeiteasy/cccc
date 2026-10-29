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

#include "./internal.h"

static int source_index_cmp(const void *a, const void *b) {
    const SourceIndex *sa = (const SourceIndex *)a;
    const SourceIndex *sb = (const SourceIndex *)b;
    if (sa->file != sb->file)
        return ((uintptr_t)sa->file < (uintptr_t)sb->file) ? -1 : 1;
    return (sa->line_no < sb->line_no)   ? -1
           : (sa->line_no > sb->line_no) ? 1
                                         : 0;
}

// True if `pc` lies in a function body that a later definition replaced
// (hot reload): nothing reaches it any more except frames already running it.
bool cc_pc_in_replaced_body(VirtualMachine *vm, Pc pc) {
    for (Obj *fn = vm->compiler.globals; fn; fn = fn->next) {
        Obj *old = obj_fn(fn)->reload_of;
        if (old && (long long)pc >= obj_fn(old)->code_addr &&
            (long long)pc < obj_fn(old)->code_end_addr)
            return true;
    }
    return false;
}

// Rebuilds the (file, line) -> first pc index from the source map, leaving out
// replaced bodies. Cheap to call repeatedly: it skips when nothing was added.
void cc_build_source_index(VirtualMachine *vm) {
    if (!(vm->flags & CCCC_ENABLE_DEBUGGER) || vm->dbg.source_map_count == 0)
        return;
    if (vm->dbg.source_index &&
        vm->dbg.source_index_built_for == vm->dbg.source_map_count)
        return;

    free(vm->dbg.source_index);
    vm->dbg.source_index           = NULL;
    vm->dbg.source_index_count     = 0;
    vm->dbg.source_index_built_for = vm->dbg.source_map_count;

    SourceIndex *index =
        malloc((size_t)vm->dbg.source_map_count * sizeof(SourceIndex));
    if (!index)
        error("could not malloc for source index");

    // First pc of each run of one (file, line); entries inside a replaced
    // body are skipped.
    int   count     = 0;
    File *last_file = NULL;
    int   last_line = -1;
    for (int i = 0; i < vm->dbg.source_map_count; i++) {
        SourceMap *m = &vm->dbg.source_map[i];
        if (cc_pc_in_replaced_body(vm, (Pc)m->pc_offset))
            continue;
        if (m->file != last_file || m->line_no != last_line) {
            index[count].file     = m->file;
            index[count].line_no  = m->line_no;
            index[count].first_pc = (Pc)m->pc_offset;
            count++;
            last_file = m->file;
            last_line = m->line_no;
        }
    }

    qsort(index, (size_t)count, sizeof(SourceIndex), source_index_cmp);

    // After sorting, identical (file, line) pairs are adjacent; keep the
    // lowest first_pc of each.
    int dedup_count = 0;
    for (int i = 0; i < count; i++) {
        SourceIndex *cur = &index[i];
        if (dedup_count > 0) {
            SourceIndex *last = &index[dedup_count - 1];
            if (last->file == cur->file && last->line_no == cur->line_no) {
                if (cur->first_pc < last->first_pc)
                    last->first_pc = cur->first_pc;
                continue;
            }
        }
        index[dedup_count++] = *cur;
    }
    vm->dbg.source_index       = index;
    vm->dbg.source_index_count = dedup_count;
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
            vm->dbg.source_map =
                malloc(vm->dbg.source_map_capacity * sizeof(SourceMap));
            if (!vm->dbg.source_map) {
                error("could not malloc for source map");
            }
            vm->dbg.source_map_count   = 0;
            vm->dbg.last_debug_file    = NULL;
            vm->dbg.last_debug_line    = -1;
            vm->dbg.source_index       = NULL;
            vm->dbg.source_index_count = 0;
            vm->dbg.num_debug_symbols  = 0;
            vm->dbg.num_watchpoints    = 0;
        }
    }

    // Store the merged program for variable lookup during codegen
    vm->compiler.globals = prog;

    // Generate bytecode from AST using new register-based codegen. The VM has
    // no bytecode optimiser: -O<n> only affects -c=native, where it is
    // forwarded to the host cc (#1159).
    gen(vm, prog);

    cc_build_source_index(vm);
}
