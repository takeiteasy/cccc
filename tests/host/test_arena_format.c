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

// arena_format (src/strings.c) formats through a stack buffer and falls back to
// an arena allocation when the result does not fit: short results, the exact
// buffer boundary and long results all come back whole and NUL-terminated.

#include "internal.h"

#include <stdio.h>
#include <string.h>

static int failed = 0;

static void expect_length(VirtualMachine *vm, size_t len) {
    char *src = malloc(len + 1);
    memset(src, 'x', len);
    src[len]  = '\0';
    char *got = arena_format(vm, "%s", src);
    if (strlen(got) != len || strcmp(got, src)) {
        fprintf(stderr, "FAIL: length %zu came back as %zu\n", len,
                strlen(got));
        failed = 1;
    }
    free(src);
}

int main(void) {
    VirtualMachine vm;
    cc_init(&vm, 0);

    char *s = arena_format(&vm, "%s-%d", "a", 42);
    if (strcmp(s, "a-42")) {
        fprintf(stderr, "FAIL: got %s, want a-42\n", s);
        failed = 1;
    }
    if (*arena_format(&vm, "%s", "")) {
        fprintf(stderr, "FAIL: empty format is not empty\n");
        failed = 1;
    }

    size_t lens[] = {1, 254, 255, 256, 257, 511, 4096, 100000};
    for (size_t i = 0; i < sizeof lens / sizeof *lens; i++)
        expect_length(&vm, lens[i]);

    char *tail = arena_format(&vm, "%.*s|%d", 300, "ab", 7);
    if (strcmp(tail, "ab|7")) {
        fprintf(stderr, "FAIL: got %s, want ab|7\n", tail);
        failed = 1;
    }

    cc_destroy(&vm);
    if (!failed)
        printf("PASS: arena_format\n");
    return failed;
}
