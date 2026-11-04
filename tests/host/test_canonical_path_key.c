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

// cc_canonical_path_key() memoizes successful realpath() results per cwd:
// a relative path re-resolves after cc_cwd_changed(), and a path that failed
// to resolve is looked up again once the file exists.

#include "internal.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
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

static void touch(const char *path) {
    FILE *f = fopen(path, "w");
    if (f)
        fclose(f);
}

int main(void) {
    char tmpl[] = "/tmp/cccc_path_key_XXXXXX";
    if (!mkdtemp(tmpl))
        return 2;
    char root[PATH_MAX], a[PATH_MAX], b[PATH_MAX], file[PATH_MAX];
    realpath(tmpl, root);
    snprintf(a, sizeof a, "%s/a", root);
    snprintf(b, sizeof b, "%s/b", root);
    mkdir(a, 0755);
    mkdir(b, 0755);
    snprintf(file, sizeof file, "%s/x.h", a);
    touch(file);
    snprintf(file, sizeof file, "%s/x.h", b);
    touch(file);

    VirtualMachine vm;
    cc_init(&vm, 0);

    char expect_a[PATH_MAX], expect_b[PATH_MAX];
    snprintf(expect_a, sizeof expect_a, "%s/x.h", a);
    snprintf(expect_b, sizeof expect_b, "%s/x.h", b);

    CHECK(chdir(a) == 0, "chdir a");
    cc_cwd_changed();
    const char *k1 = cc_canonical_path_key(&vm, "./x.h");
    CHECK(!strcmp(k1, expect_a), "first key %s, expected %s", k1, expect_a);
    CHECK(cc_canonical_path_key(&vm, "./x.h") == k1, "repeat hits the memo");

    CHECK(chdir(b) == 0, "chdir b");
    cc_cwd_changed();
    const char *k2 = cc_canonical_path_key(&vm, "./x.h");
    CHECK(!strcmp(k2, expect_b), "after chdir key %s, expected %s", k2,
          expect_b);

    const char *missing = "./later.h";
    CHECK(!strcmp(cc_canonical_path_key(&vm, missing), missing),
          "an unresolvable path is returned as spelled");
    touch("later.h");
    const char *k3 = cc_canonical_path_key(&vm, missing);
    char        expect_c[PATH_MAX];
    snprintf(expect_c, sizeof expect_c, "%s/later.h", b);
    CHECK(!strcmp(k3, expect_c), "failure was cached: got %s, expected %s", k3,
          expect_c);

    cc_destroy(&vm);
    if (chdir("/") != 0)
        failed = 1;
    if (!failed)
        printf("PASS: canonical path key\n");
    return failed;
}
