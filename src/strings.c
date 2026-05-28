/*
 JCC: JIT C Compiler

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

 This file was original part of chibicc by Rui Ueyama (MIT) https://github.com/rui314/chibicc
*/

#include "jcc.h"
#include "./internal.h"

void strarray_push(StringArray *arr, char *s) {
    if (!arr->data) {
        arr->data = calloc(8, sizeof(char *));
        arr->capacity = 8;
    }

    if (arr->capacity == arr->len) {
        arr->data = realloc(arr->data, sizeof(char *) * arr->capacity * 2);
        arr->capacity *= 2;
        for (int i = arr->len; i < arr->capacity; i++)
            arr->data[i] = NULL;
    }

    arr->data[arr->len++] = s;
}

void arena_strarray_push(JCC *vm, StringArray *arr, char *s) {
    if (!arr->data) {
        arr->data = arena_alloc(&vm->compiler.parser_arena, 8 * sizeof(char *));
        memset(arr->data, 0, 8 * sizeof(char *));
        arr->capacity = 8;
    }

    if (arr->capacity == arr->len) {
        int old_capacity = arr->capacity;
        int new_capacity = arr->capacity * 2;
        char **data =
            arena_alloc(&vm->compiler.parser_arena, new_capacity * sizeof(char *));
        memset(data, 0, new_capacity * sizeof(char *));
        memcpy(data, arr->data, old_capacity * sizeof(char *));
        arr->data = data;
        arr->capacity = new_capacity;
    }

    arr->data[arr->len++] = s;
}

// Takes a printf-style format string and returns a formatted string.
char *format(char *fmt, ...) {
    char *buf;
    size_t buflen;
    FILE *out = open_memstream(&buf, &buflen);

    va_list ap;
    va_start(ap, fmt);
    vfprintf(out, fmt, ap);
    va_end(ap);
    fclose(out);
    return buf;
}

char *arena_strdup(JCC *vm, const char *str) {
    if (!str)
        return NULL;
    return arena_strndup(vm, str, strlen(str));
}

char *arena_strndup(JCC *vm, const char *str, int len) {
    char *buf = arena_alloc(&vm->compiler.parser_arena, len + 1);
    memcpy(buf, str, len);
    buf[len] = '\0';
    return buf;
}

char *arena_format(JCC *vm, char *fmt, ...) {
    char *heap_buf;
    size_t buflen;
    FILE *out = open_memstream(&heap_buf, &buflen);

    va_list ap;
    va_start(ap, fmt);
    vfprintf(out, fmt, ap);
    va_end(ap);
    fclose(out);

    char *arena_buf = arena_strndup(vm, heap_buf, buflen);
    free(heap_buf);
    return arena_buf;
}
