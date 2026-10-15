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

// Host-side regression test: cccc_unwind_point_restore undoes what a longjmp
// out of cc_run_at / cccc_call_guest_callback (a failed Assert or timeout in a
// test) leaves stale -- the callback clears CCCC_ENABLE_DEBUGGER and the
// debugger step state, and the run leaves cc_running_vm and the FFI vm set.

#include "internal.h"

#include <stdio.h>

static int failed = 0;

#define CHECK(cond, ...)                                                       \
    do {                                                                       \
        if (!(cond)) {                                                         \
            fprintf(stderr, "FAIL (%s:%d): ", __FILE__, __LINE__);             \
            fprintf(stderr, __VA_ARGS__);                                      \
            fprintf(stderr, "\n");                                             \
            failed = 1;                                                        \
        }                                                                      \
    } while (0)

int main(void) {
    VirtualMachine vm;
    cc_init(&vm, CCCC_ENABLE_DEBUGGER);
    vm.dbg.single_step       = 1;
    vm.dbg.debugger_attached = 1;

    CcUnwindPoint p;
    cccc_unwind_point_save(&vm, &p);

    // What cccc_call_guest_callback_ex leaves behind when it is skipped.
    vm.flags                 &= ~CCCC_ENABLE_DEBUGGER;
    vm.dbg.single_step        = 0;
    vm.dbg.step_over          = 1;
    vm.dbg.debugger_attached  = 0;
    cc_running_vm             = &vm;

    cccc_gil_acquire(&vm);
    cccc_gil_acquire(&vm);
    CHECK(vm.gil_depth == 2, "gil_depth = %d, expected 2", vm.gil_depth);

    cccc_unwind_point_restore(&vm, &p);

    CHECK(vm.flags & CCCC_ENABLE_DEBUGGER, "debugger flag not restored");
    CHECK(vm.dbg.single_step == 1, "single_step not restored");
    CHECK(vm.dbg.step_over == 0, "step_over not restored");
    CHECK(vm.dbg.debugger_attached == 1, "debugger_attached not restored");
    CHECK(vm.gil_depth == 0, "gil_depth = %d, expected 0", vm.gil_depth);
    CHECK(cc_running_vm == NULL, "cc_running_vm still set");
    CHECK(cccc_current_ffi_vm() == NULL, "ffi vm still set");

    cc_destroy(&vm);
    return failed ? 1 : 0;
}
