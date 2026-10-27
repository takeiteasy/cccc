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

// print_escaped_string (src/json.c) emits valid JSON strings: named escapes,
// \u00XX for every other control byte, and everything else verbatim.

#include "internal.h"

#include <stdio.h>
#include <string.h>

static int failed = 0;

static void expect(const char *in, const char *want) {
    char  buf[512] = {0};
    FILE *f        = fmemopen(buf, sizeof buf - 1, "w");
    print_escaped_string(f, in);
    fclose(f);
    if (strcmp(buf, want)) {
        fprintf(stderr, "FAIL: got %s, want %s\n", buf, want);
        failed = 1;
    }
}

int main(void) {
    expect(NULL, "null");
    expect("plain", "\"plain\"");
    expect("a\"b\\c", "\"a\\\"b\\\\c\"");
    expect("\n\r\t\b\f", "\"\\n\\r\\t\\b\\f\"");
    expect("\x01\x1f", "\"\\u0001\\u001f\"");
    expect("\x7f \xc3\xa9", "\"\x7f \xc3\xa9\"");

    char all[0x20];
    for (int i = 1; i < 0x20; i++)
        all[i - 1] = (char)i;
    all[0x1f] = '\0';
    char  buf[1024] = {0};
    FILE *f         = fmemopen(buf, sizeof buf - 1, "w");
    print_escaped_string(f, all);
    fclose(f);
    for (const char *p = buf; *p; p++)
        if ((unsigned char)*p < 0x20) {
            fprintf(stderr, "FAIL: raw control byte 0x%02x in output\n", *p);
            failed = 1;
        }

    if (!failed)
        printf("PASS: json_escape\n");
    return failed;
}
