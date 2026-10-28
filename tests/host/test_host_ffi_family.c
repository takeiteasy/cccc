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

// Every native in src/host_ffi_family.h has a canonical registration and a
// host-numbered `__cccc_host_` passthrough.

#include "host_ffi_family.h"
#include "internal.h"

#include <stdio.h>
#include <string.h>

int find_ffi_function(VirtualMachine *vm, const char *name);

static int failed = 0;

static void check_row(VirtualMachine *vm, const char *name) {
    char host_name[64];
    snprintf(host_name, sizeof host_name, CCCC_HOST_FFI_PREFIX "%s", name);
    if (find_ffi_function(vm, host_name) < 0) {
        fprintf(stderr, "FAIL: '%s' is not registered\n", host_name);
        failed = 1;
    }
}

int main(void) {
    VirtualMachine vm;
    cc_init(&vm, 0);
    cc_load_stdlib(&vm);

#define ROW(n) check_row(&vm, #n);
    CCCC_HOST_FFI_FAMILY(ROW)
    CCCC_HOST_FFI_FAMILY_LINUX(ROW)
#undef ROW

    if (!failed)
        printf("PASS: host ffi family\n");
    return failed ? 1 : 0;
}
