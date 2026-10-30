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

// End-to-end test of `-c=object`: a main-less file compiles to an object that
// the host cc links into a program. The binary is $CCCC_BIN, else build/cccc,
// else ./cccc; the test is skipped when none exists.

#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static char cccc[PATH_MAX];

static bool find_cccc(void) {
    const char *candidates[] = {getenv("CCCC_BIN"), "build/cccc", "./cccc"};
    for (size_t i = 0; i < sizeof candidates / sizeof *candidates; i++)
        if (candidates[i] && realpath(candidates[i], cccc) &&
            access(cccc, X_OK) == 0)
            return true;
    return false;
}

static int run(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

static int run(const char *fmt, ...) {
    char    cmd[4 * PATH_MAX];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(cmd, sizeof cmd, fmt, ap);
    va_end(ap);
    int st = system(cmd);
    return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}

static void write_file(const char *path, const char *text) {
    FILE *f = fopen(path, "w");
    if (f) {
        fputs(text, f);
        fclose(f);
    }
}

int main(void) {
    alarm(120);
    if (!find_cccc()) {
        printf("SKIP: native object (no cccc binary; set CCCC_BIN)\n");
        return 0;
    }
    char root[] = "/tmp/cccc_object_XXXXXX";
    if (!mkdtemp(root))
        return 2;

    char path[PATH_MAX];
    snprintf(path, sizeof path, "%s/helper.c", root);
    write_file(path, "int helper(void) { return 42; }\n");
    snprintf(path, sizeof path, "%s/main.c", root);
    write_file(path,
               "int helper(void);\nint main(void) { return helper(); }\n");

    int failed = 0;
    if (run("%s -c=object %s/helper.c -o %s/named.o >/dev/null 2>&1", cccc,
            root, root) != 0) {
        fprintf(stderr, "FAIL: -c=object with -o failed\n");
        failed = 1;
    } else if (run("cc %s/main.c %s/named.o -o %s/app >/dev/null 2>&1", root,
                   root, root) != 0 ||
               run("%s/app", root) != 42) {
        fprintf(stderr, "FAIL: object did not link into a program returning "
                        "42\n");
        failed = 1;
    }

    // No -o: the object is named after the input, in the current directory.
    if (!failed && (run("cd %s && %s -c=object helper.c >/dev/null 2>&1", root,
                        cccc) != 0 ||
                    run("test -s %s/helper.o", root) != 0)) {
        fprintf(stderr, "FAIL: default output name was not helper.o\n");
        failed = 1;
    }

    // Several inputs need an explicit -o.
    if (!failed && run("%s -c=object %s/helper.c %s/main.c >/dev/null 2>&1",
                       cccc, root, root) == 0) {
        fprintf(stderr, "FAIL: several inputs without -o were accepted\n");
        failed = 1;
    }

    // A static table of an extern function's address resolves at link time.
    if (!failed) {
        snprintf(path, sizeof path, "%s/table.c", root);
        write_file(path, "int helper(void);\n"
                         "static int (*const tbl[])(void) = {helper};\n"
                         "int call(void) { return tbl[0](); }\n");
        snprintf(path, sizeof path, "%s/main2.c", root);
        write_file(path,
                   "int call(void);\nint main(void) { return call(); }\n");
        if (run("%s -c=object %s/table.c -o %s/table.o >/dev/null 2>&1", cccc,
                root, root) != 0 ||
            run("cc %s/main2.c %s/table.o %s/named.o -o %s/app2 >/dev/null "
                "2>&1",
                root, root, root, root) != 0 ||
            run("%s/app2", root) != 42) {
            fprintf(stderr, "FAIL: static table of an extern function\n");
            failed = 1;
        }
    }

    const char *files[] = {"helper.c", "helper.o", "named.o", "main.c",
                           "app",      "table.c",  "table.o", "main2.c",
                           "app2"};
    for (size_t i = 0; i < sizeof files / sizeof *files; i++) {
        snprintf(path, sizeof path, "%s/%s", root, files[i]);
        unlink(path);
    }
    rmdir(root);
    if (!failed)
        printf("PASS: native object\n");
    return failed;
}
