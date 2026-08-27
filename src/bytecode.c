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

    // Build source index for O(log n) line→PC lookups
    if (vm->flags & CCCC_ENABLE_DEBUGGER && vm->dbg.source_map_count > 0) {
        vm->dbg.source_index_count = 0;

        // First pass: count unique (file, line) pairs
        File *last_file = NULL;
        int   last_line = -1;
        for (int i = 0; i < vm->dbg.source_map_count; i++) {
            SourceMap *m = &vm->dbg.source_map[i];
            if (m->file != last_file || m->line_no != last_line) {
                vm->dbg.source_index_count++;
                last_file = m->file;
                last_line = m->line_no;
            }
        }

        vm->dbg.source_index =
            malloc(vm->dbg.source_index_count * sizeof(SourceIndex));
        if (!vm->dbg.source_index) {
            error("could not malloc for source index");
        }

        // Fill with first PC for each unique (file, line)
        int idx   = 0;
        last_file = NULL;
        last_line = -1;
        for (int i = 0; i < vm->dbg.source_map_count; i++) {
            SourceMap *m = &vm->dbg.source_map[i];
            if (m->file != last_file || m->line_no != last_line) {
                vm->dbg.source_index[idx].file     = m->file;
                vm->dbg.source_index[idx].line_no  = m->line_no;
                vm->dbg.source_index[idx].first_pc = (Pc)m->pc_offset;
                idx++;
                last_file = m->file;
                last_line = m->line_no;
            }
        }

        // Sort by (file, line_no) for binary search
        qsort(vm->dbg.source_index, vm->dbg.source_index_count,
              sizeof(SourceIndex), source_index_cmp);

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
