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

// Host-side test: an extern global the program declares but never defines
// binds to storage registered with cc_register_cdata[_owned]. A definition in
// the program wins, owned slots reset at each run start, and the FFI policy
// applies to the load.

#include "internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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

static char *write_source(const char *src) {
    static char path[] = "/tmp/cccc_host_data_XXXXXX.c";
    strcpy(path, "/tmp/cccc_host_data_XXXXXX.c");
    int fd = mkstemps(path, 2);
    if (fd < 0 || write(fd, src, strlen(src)) < 0) {
        perror("write_source");
        exit(1);
    }
    close(fd);
    return path;
}

static void build(VirtualMachine *vm) {
    cc_init(vm, 0);
    vm->compiler.compiler_family = CCCC_COMPILER_FAMILY_GCC;
    init_mode_macros(vm);
    register_stdlib_functions(vm);
}

static void compile(VirtualMachine *vm, const char *src) {
    char  *path   = write_source(src);
    Token *tok    = cc_preprocess(vm, path);
    Obj   *prog   = cc_parse(vm, tok);
    Obj   *merged = cc_link_progs(vm, &prog, 1);
    cc_expand_macros(vm, merged);
    cc_compile(vm, merged);
    unlink(path);
}

static int host_counter = 3;

static void test_bound_host_int(void) {
    VirtualMachine vm;
    build(&vm);
    host_counter = 3;
    CHECK(cc_register_cdata(&vm, "host_counter", &host_counter,
                            sizeof host_counter) == 0,
          "register");
    CHECK(cc_cdata_addr(&vm, "host_counter") == &host_counter, "cdata_addr");
    compile(&vm,
            "extern int host_counter;\n"
            "int main(void) { host_counter += 5; return host_counter; }\n");
    int rc = cc_run(&vm, 0, NULL);
    CHECK(rc == 8, "guest saw %d, expected 8", rc);
    CHECK(host_counter == 8, "host saw %d, expected 8", host_counter);
    cc_destroy(&vm);
}

static void test_owned_slot_resets(void) {
    VirtualMachine vm;
    build(&vm);
    int  init = 10;
    int *slot = cc_register_cdata_owned(&vm, "host_owned", sizeof init, &init);
    CHECK(slot && *slot == 10, "owned slot not initialised");
    compile(&vm, "extern int host_owned;\n"
                 "int main(void) { host_owned += 7; return host_owned; }\n");
    int first = cc_run(&vm, 0, NULL);
    CHECK(first == 17, "first run %d, expected 17", first);
    CHECK(*slot == 17, "host slot %d, expected 17", *slot);
    int second = cc_run(&vm, 0, NULL);
    CHECK(second == 17, "second run %d, expected 17 (slot reset)", second);
    cc_destroy(&vm);
}

static void test_definition_wins(const char *src, int expect) {
    VirtualMachine vm;
    build(&vm);
    host_counter = 3;
    cc_register_cdata(&vm, "host_counter", &host_counter, sizeof host_counter);
    compile(&vm, src);
    int rc = cc_run(&vm, 0, NULL);
    CHECK(rc == expect, "program saw %d, expected %d", rc, expect);
    CHECK(host_counter == 3, "host object was written: %d", host_counter);
    cc_destroy(&vm);
}

static void test_static_initializer(void) {
    VirtualMachine vm;
    build(&vm);
    host_counter = 3;
    cc_register_cdata(&vm, "host_counter", &host_counter, sizeof host_counter);
    compile(&vm, "extern int host_counter;\n"
                 "static int *p = &host_counter;\n"
                 "int main(void) { *p = 9; return host_counter; }\n");
    int rc = cc_run(&vm, 0, NULL);
    CHECK(rc == 9 && host_counter == 9, "rc %d host %d, expected 9", rc,
          host_counter);
    cc_destroy(&vm);
}

static void test_disable_ffi_traps(void) {
    VirtualMachine vm;
    build(&vm);
    host_counter = 3;
    cc_register_cdata(&vm, "host_counter", &host_counter, sizeof host_counter);
    vm.disable_all_ffi = 1;
    compile(&vm, "extern int host_counter;\n"
                 "int main(void) { host_counter = 99; return 42; }\n");
    int rc = cc_run(&vm, 0, NULL);
    CHECK(rc != 42, "run continued after the FFI denial");
    CHECK(host_counter == 3, "host object was written: %d", host_counter);
    cc_destroy(&vm);
}

static void test_vms_are_separate(void) {
    VirtualMachine a, b;
    build(&a);
    build(&b);
    int  one = 1, two = 2;
    int *sa = cc_register_cdata_owned(&a, "slot", sizeof one, &one);
    int *sb = cc_register_cdata_owned(&b, "slot", sizeof two, &two);
    CHECK(sa != sb && *sa == 1 && *sb == 2, "slots shared between VMs");
    CHECK(cc_cdata_addr(&a, "slot") == sa && cc_cdata_addr(&b, "slot") == sb,
          "lookup crossed VMs");
    cc_destroy(&a);
    cc_destroy(&b);
}

int main(void) {
    test_bound_host_int();
    test_owned_slot_resets();
    test_definition_wins("extern int host_counter;\n"
                         "int host_counter = 100;\n"
                         "int main(void) { return host_counter; }\n",
                         100);
    test_definition_wins("extern int host_counter;\n"
                         "int host_counter;\n"
                         "int main(void) { host_counter = 5; return "
                         "host_counter; }\n",
                         5);
    test_static_initializer();
    test_disable_ffi_traps();
    test_vms_are_separate();
    if (failed)
        return 1;
    printf("ok\n");
    return 0;
}
