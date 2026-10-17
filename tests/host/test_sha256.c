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

// SHA-256 (src/sha256.c) against the FIPS 180-2 vectors, the padding
// boundaries, and an incremental update that splits the input unevenly.

#include "sha256.h"

#include <stdio.h>
#include <stdlib.h>
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

static void expect(const char *what, const void *data, size_t len,
                   const char *want) {
    char got[65];
    cccc_sha256_hex(data, len, got);
    CHECK(!strcmp(got, want), "%s: got %s, want %s", what, got, want);
}

static void test_vectors(void) {
    expect("empty", "", 0,
           "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    expect("abc", "abc", 3,
           "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    const char *two_block =
        "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    expect("448-bit", two_block, strlen(two_block),
           "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    const char *four_block =
        "abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmn"
        "hijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu";
    expect("896-bit", four_block, strlen(four_block),
           "cf5b16a778af8380036ce59e7b0492370b249b11e8f07a51afac45037afee9d1");
}

// Lengths either side of the 56-byte padding threshold and the block size.
static void test_boundaries(void) {
    static const struct {
        size_t      n;
        const char *want;
    } cases[] = {
        {55,
         "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318"},
        {56,
         "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a"},
        {64,
         "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb"},
    };
    char buf[64];
    memset(buf, 'a', sizeof buf);
    for (size_t i = 0; i < sizeof cases / sizeof *cases; i++) {
        char what[32];
        snprintf(what, sizeof what, "%zu x 'a'", cases[i].n);
        expect(what, buf, cases[i].n, cases[i].want);
    }
}

// One million 'a', fed in chunks of awkward sizes.
static void test_million_a_chunked(void) {
    static const size_t chunks[] = {1, 7, 63, 64, 65, 1000, 4097};
    CcccSha256          ctx;
    unsigned char       digest[32];
    char                hex[65];
    char               *buf = malloc(4097);
    memset(buf, 'a', 4097);
    cccc_sha256_init(&ctx);
    size_t left = 1000000;
    for (size_t i = 0; left; i++) {
        size_t n = chunks[i % (sizeof chunks / sizeof *chunks)];
        if (n > left)
            n = left;
        cccc_sha256_update(&ctx, buf, n);
        left -= n;
    }
    cccc_sha256_final(&ctx, digest);
    free(buf);
    for (int i = 0; i < 32; i++)
        snprintf(hex + i * 2, 3, "%02x", digest[i]);
    CHECK(
        !strcmp(
            hex,
            "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"),
        "million 'a': got %s", hex);
}

int main(void) {
    test_vectors();
    test_boundaries();
    test_million_a_chunked();
    if (!failed)
        printf("PASS: sha256\n");
    return failed;
}
