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

// Host-side check of the comptime capability classes: every natively
// registered function must be listed in src/comptime_policy.c, and the class
// parsing/grant logic must behave.

#include "internal.h"

#include <stdio.h>
#include <string.h>

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
    cc_init(&vm, 0);
    cc_load_stdlib(&vm);

    for (int i = 0; i < vm.compiler.ffi_count; i++) {
        const char *name = vm.compiler.ffi_table[i].name;
        CHECK(cccc_comptime_class_of(name) >= 0,
              "'%s' is registered but has no comptime capability class "
              "(add it to src/comptime_policy.c)",
              name);
    }

    for (int i = 0; i < vm.compiler.ffi_count; i++) {
        const char *name = vm.compiler.ffi_table[i].name;
        if (cccc_comptime_class_of(name) == CT_SAFE)
            CHECK(cccc_comptime_arg_spec(name) != NULL,
                  "safe '%s' has no pointer-argument spec "
                  "(add it to src/comptime_args.c)",
                  name);
    }
    CHECK(cccc_comptime_arg_spec("fopen") == NULL, "fopen is not spec'd");
    CHECK(strcmp(cccc_comptime_arg_spec("sqrtf"), "") == 0, "sqrtf spec");
    CHECK(strcmp(cccc_comptime_arg_spec("modff"), "- w=4") == 0, "modff spec");
    CHECK(strcmp(cccc_comptime_arg_spec("isalpha_l"), "") == 0, "isalpha_l");

    CHECK(cccc_comptime_class_of("strlen") == CT_SAFE, "strlen not safe");
    CHECK(cccc_comptime_class_of("sqrtf") == CT_SAFE, "sqrtf not safe");
    CHECK(cccc_comptime_class_of("isalpha_l") == CT_SAFE, "isalpha_l");
    CHECK(cccc_comptime_class_of("__builtin_gensym") == CT_SAFE, "builtin");
    CHECK(cccc_comptime_class_of("fopen") == CT_FS, "fopen not fs");
    CHECK(cccc_comptime_class_of("system") == CT_EXEC, "system not exec");
    CHECK(cccc_comptime_class_of("getenv") == CT_ENV, "getenv not env");
    CHECK(cccc_comptime_class_of("socket") == CT_NET, "socket not net");
    CHECK(cccc_comptime_class_of("dlopen") == CT_DLOPEN, "dlopen");
    CHECK(cccc_comptime_class_of("no_such_function") == -1, "unknown name");

    unsigned mask = 0;
    char     bad[32];
    CHECK(cccc_comptime_parse_classes("fs, exec", &mask, bad, sizeof(bad)),
          "parse fs,exec");
    CHECK(mask == ((1u << CT_FS) | (1u << CT_EXEC)), "mask = %x", mask);
    CHECK(!cccc_comptime_parse_classes("fs,bogus", &mask, bad, sizeof(bad)) &&
              strcmp(bad, "bogus") == 0,
          "bogus class accepted");
    CHECK(!cccc_comptime_parse_classes("safe", &mask, bad, sizeof(bad)),
          "'safe' is not grantable");

    CHECK(cccc_comptime_name_allowed(0, "strlen", NULL), "safe denied");
    CHECK(!cccc_comptime_name_allowed(0, "fopen", NULL), "fopen allowed");
    CHECK(cccc_comptime_name_allowed(1u << CT_FS, "fopen", NULL), "fs grant");
    CHECK(!cccc_comptime_name_allowed(1u << CT_FS, "system", NULL),
          "fs grants exec");
    CHECK(!cccc_comptime_name_allowed(1u << CT_FS, "no_such_function", NULL),
          "unclassified allowed without all");
    mask = 0;
    cccc_comptime_parse_classes("all", &mask, bad, sizeof(bad));
    CHECK(cccc_comptime_name_allowed(mask, "no_such_function", NULL),
          "all does not admit unclassified");

    cc_destroy(&vm);
    if (failed)
        return 1;
    printf("PASS: comptime policy\n");
    return 0;
}
