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


// Host-side test for the include graph --watch reloads by: cc_preprocess_ex
// records the files each root reads (also those another root already pulled
// in, and through a symlinked directory), and a re-preprocess can be rolled
// back.

#include "internal.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int  failed = 0;
static char dir[PATH_MAX];

#define CHECK(cond, ...)                                                       \
    do {                                                                       \
        if (!(cond)) {                                                         \
            fprintf(stderr, "FAIL (%s:%d): ", __FILE__, __LINE__);             \
            fprintf(stderr, __VA_ARGS__);                                      \
            fprintf(stderr, "\n");                                             \
            failed = 1;                                                        \
        }                                                                      \
    } while (0)

static char *path_of(const char *name) {
    static char paths[8][PATH_MAX + 64];
    static int  next;
    char       *p = paths[next++ % 8];
    snprintf(p, PATH_MAX + 64, "%s/%s", dir, name);
    return p;
}

static void write_file(const char *name, const char *text) {
    FILE *f = fopen(path_of(name), "w");
    fputs(text, f);
    fclose(f);
}

// Whether `root`'s dependency set holds `name`, a path under the real dir.
static bool depends(VirtualMachine *vm, const char *root, const char *name) {
    char rroot[PATH_MAX], rname[PATH_MAX];
    if (!realpath(path_of(root), rroot) || !realpath(path_of(name), rname))
        return false;
    HashMap *deps = hashmap_get(&vm->compiler.tu_deps, rroot);
    return deps && hashmap_get(deps, rname);
}

int main(void) {
    char tmpl[] = "/tmp/cccc_include_graph_XXXXXX";
    if (!mkdtemp(tmpl))
        return 2;
    realpath(tmpl, dir);

    mkdir(path_of("real"), 0755);
    symlink("real", path_of("inc"));
    write_file("real/shared.h", "#pragma once\n#define SHARED 1\n");
    write_file("a.h", "#define A 1\n");
    write_file("b.h", "#define B 1\n");
    write_file("a.c", "#include \"inc/shared.h\"\n#include \"a.h\"\n"
                      "int fa(void) { return A + SHARED; }\n");
    write_file("b.c", "#include \"inc/shared.h\"\n#include \"b.h\"\n"
                      "int fb(void) { return B + SHARED; }\n");

    VirtualMachine vm;
    cc_init(&vm, 0);
    cc_preprocess_ex(&vm, path_of("a.c"), false);
    cc_preprocess_ex(&vm, path_of("b.c"), false);

    CHECK(depends(&vm, "a.c", "a.c") && depends(&vm, "b.c", "b.c"),
          "a root depends on itself");
    CHECK(depends(&vm, "a.c", "a.h") && !depends(&vm, "a.c", "b.h"),
          "own header only");
    CHECK(depends(&vm, "b.c", "b.h") && !depends(&vm, "b.c", "a.h"),
          "own header only (b)");
    CHECK(depends(&vm, "a.c", "real/shared.h") &&
              depends(&vm, "b.c", "real/shared.h"),
          "a header another root pulled in first, through a symlink");

    // A re-preprocess replaces the set; rolling back restores it.
    write_file("a.c", "#include \"inc/shared.h\"\nint fa(void) { return 1; }\n");
    cc_preprocess_ex(&vm, path_of("a.c"), false);
    CHECK(!depends(&vm, "a.c", "a.h") && depends(&vm, "a.c", "real/shared.h"),
          "re-preprocess replaces the set");
    char rroot[PATH_MAX];
    realpath(path_of("a.c"), rroot);
    cc_deps_rollback(&vm, rroot);
    CHECK(depends(&vm, "a.c", "a.h"), "rollback restores the old set");

    cc_destroy(&vm);
    if (!failed)
        printf("PASS: include graph\n");
    return failed;
}
