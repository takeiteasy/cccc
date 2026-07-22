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

// Host-side regression test (#707): links directly against the compiler
// sources, like src/fuzzing.c does, instead of going through the guest
// .c/exit-code-42 protocol used by tools/tests.py.
//
// Regression test for #181: file_no / embedded_file_no used to be
// function-local statics in tokenize.c, so they outlived any single
// VirtualMachine and kept indexing into the *next* VM's freshly-zeroed
// vm->compiler.input_files array -- leaving the low indices as
// uninitialized heap garbage. This constructs two VirtualMachine
// instances sequentially in one process and asserts the second VM's
// state is well-formed and independent of the first.

#include "internal.h"

#include <stdio.h>
#include <string.h>

static int failed = 0;

#define CHECK(cond, ...)                                                     \
    do {                                                                     \
        if (!(cond)) {                                                      \
            fprintf(stderr, "FAIL (%s:%d): ", __FILE__, __LINE__);          \
            fprintf(stderr, __VA_ARGS__);                                   \
            fprintf(stderr, "\n");                                         \
            failed = 1;                                                    \
        }                                                                    \
    } while (0)

static void write_file(const char *path, const char *contents) {
    FILE *f = fopen(path, "w");
    fputs(contents, f);
    fclose(f);
}

int main(void) {
    const char *file_a = "/tmp/cccc_host_test_multi_vm_a.c";
    const char *file_b = "/tmp/cccc_host_test_multi_vm_b.c";
    const char *file_c = "/tmp/cccc_host_test_multi_vm_c.c";
    write_file(file_a, "int a;\n");
    write_file(file_b, "int b;\n");
    write_file(file_c, "int c;\n");

    // First VM: tokenize three files, advancing file_no to 3.
    VirtualMachine vm1;
    cc_init(&vm1, 0);
    tokenize_file(&vm1, (char *)file_a);
    tokenize_file(&vm1, (char *)file_b);
    tokenize_file(&vm1, (char *)file_c);
    CHECK(vm1.compiler.file_no == 3, "vm1.compiler.file_no = %d, expected 3",
          vm1.compiler.file_no);
    cc_destroy(&vm1);

    // Second, independent VM in the same process: file_no and input_files
    // must start fresh, not continue from vm1's counters.
    VirtualMachine vm2;
    cc_init(&vm2, 0);
    tokenize_file(&vm2, (char *)file_a);

    CHECK(vm2.compiler.file_no == 1,
          "vm2.compiler.file_no = %d, expected 1 (counter leaked across VMs)",
          vm2.compiler.file_no);
    CHECK(vm2.compiler.input_files != NULL && vm2.compiler.input_files[0] != NULL,
          "vm2.compiler.input_files[0] missing");
    if (vm2.compiler.input_files && vm2.compiler.input_files[0]) {
        CHECK(strcmp(vm2.compiler.input_files[0]->name, file_a) == 0,
              "vm2.compiler.input_files[0]->name = %s, expected %s",
              vm2.compiler.input_files[0]->name, file_a);
    }
    CHECK(vm2.compiler.input_files != NULL && vm2.compiler.input_files[1] == NULL,
          "vm2.compiler.input_files[1] should be the NULL sentinel");
    cc_destroy(&vm2);

    // Third VM: embedded_file_no must also start fresh.
    VirtualMachine vm3;
    cc_init(&vm3, 0);
    tokenize_string(&vm3, "<embed1>", "int x;\n");
    CHECK(vm3.compiler.embedded_file_no == 1,
          "vm3.compiler.embedded_file_no = %d, expected 1", vm3.compiler.embedded_file_no);
    cc_destroy(&vm3);

    if (!failed)
        printf("PASS: file_no/embedded_file_no reset per-VM; input_files well-formed across instances\n");

    return failed;
}
