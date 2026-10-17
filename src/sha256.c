/*-
 * Extracted from libscrypt (https://github.com/technion/libscrypt), sha256.c.
 *
 * Copyright 2005,2007,2009 Colin Percival
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "sha256.h"

#include <string.h>

static uint32_t be32dec(const unsigned char *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static void be32enc(unsigned char *p, uint32_t x) {
    p[0] = (unsigned char)(x >> 24);
    p[1] = (unsigned char)(x >> 16);
    p[2] = (unsigned char)(x >> 8);
    p[3] = (unsigned char)x;
}

#define Ch(x, y, z)  ((x & (y ^ z)) ^ z)
#define Maj(x, y, z) ((x & (y | z)) | (y & z))
#define SHR(x, n)    (x >> n)
#define ROTR(x, n)   ((x >> n) | (x << (32 - n)))
#define S0(x)        (ROTR(x, 2) ^ ROTR(x, 13) ^ ROTR(x, 22))
#define S1(x)        (ROTR(x, 6) ^ ROTR(x, 11) ^ ROTR(x, 25))
#define s0(x)        (ROTR(x, 7) ^ ROTR(x, 18) ^ SHR(x, 3))
#define s1(x)        (ROTR(x, 17) ^ ROTR(x, 19) ^ SHR(x, 10))

// One round; the state words rotate through the argument positions.
#define RND(a, b, c, d, e, f, g, h, k)                                         \
    t0  = h + S1(e) + Ch(e, f, g) + k;                                         \
    t1  = S0(a) + Maj(a, b, c);                                                \
    d  += t0;                                                                  \
    h   = t0 + t1;

#define RNDr(S, W, i, k)                                                       \
    RND(S[(64 - i) % 8], S[(65 - i) % 8], S[(66 - i) % 8], S[(67 - i) % 8],    \
        S[(68 - i) % 8], S[(69 - i) % 8], S[(70 - i) % 8], S[(71 - i) % 8],    \
        W[i] + k)

static void transform(uint32_t *state, const unsigned char block[64]) {
    uint32_t W[64];
    uint32_t S[8];
    uint32_t t0, t1;

    for (int i = 0; i < 16; i++)
        W[i] = be32dec(block + i * 4);
    for (int i = 16; i < 64; i++)
        W[i] = s1(W[i - 2]) + W[i - 7] + s0(W[i - 15]) + W[i - 16];

    memcpy(S, state, 32);

    RNDr(S, W, 0, 0x428a2f98);
    RNDr(S, W, 1, 0x71374491);
    RNDr(S, W, 2, 0xb5c0fbcf);
    RNDr(S, W, 3, 0xe9b5dba5);
    RNDr(S, W, 4, 0x3956c25b);
    RNDr(S, W, 5, 0x59f111f1);
    RNDr(S, W, 6, 0x923f82a4);
    RNDr(S, W, 7, 0xab1c5ed5);
    RNDr(S, W, 8, 0xd807aa98);
    RNDr(S, W, 9, 0x12835b01);
    RNDr(S, W, 10, 0x243185be);
    RNDr(S, W, 11, 0x550c7dc3);
    RNDr(S, W, 12, 0x72be5d74);
    RNDr(S, W, 13, 0x80deb1fe);
    RNDr(S, W, 14, 0x9bdc06a7);
    RNDr(S, W, 15, 0xc19bf174);
    RNDr(S, W, 16, 0xe49b69c1);
    RNDr(S, W, 17, 0xefbe4786);
    RNDr(S, W, 18, 0x0fc19dc6);
    RNDr(S, W, 19, 0x240ca1cc);
    RNDr(S, W, 20, 0x2de92c6f);
    RNDr(S, W, 21, 0x4a7484aa);
    RNDr(S, W, 22, 0x5cb0a9dc);
    RNDr(S, W, 23, 0x76f988da);
    RNDr(S, W, 24, 0x983e5152);
    RNDr(S, W, 25, 0xa831c66d);
    RNDr(S, W, 26, 0xb00327c8);
    RNDr(S, W, 27, 0xbf597fc7);
    RNDr(S, W, 28, 0xc6e00bf3);
    RNDr(S, W, 29, 0xd5a79147);
    RNDr(S, W, 30, 0x06ca6351);
    RNDr(S, W, 31, 0x14292967);
    RNDr(S, W, 32, 0x27b70a85);
    RNDr(S, W, 33, 0x2e1b2138);
    RNDr(S, W, 34, 0x4d2c6dfc);
    RNDr(S, W, 35, 0x53380d13);
    RNDr(S, W, 36, 0x650a7354);
    RNDr(S, W, 37, 0x766a0abb);
    RNDr(S, W, 38, 0x81c2c92e);
    RNDr(S, W, 39, 0x92722c85);
    RNDr(S, W, 40, 0xa2bfe8a1);
    RNDr(S, W, 41, 0xa81a664b);
    RNDr(S, W, 42, 0xc24b8b70);
    RNDr(S, W, 43, 0xc76c51a3);
    RNDr(S, W, 44, 0xd192e819);
    RNDr(S, W, 45, 0xd6990624);
    RNDr(S, W, 46, 0xf40e3585);
    RNDr(S, W, 47, 0x106aa070);
    RNDr(S, W, 48, 0x19a4c116);
    RNDr(S, W, 49, 0x1e376c08);
    RNDr(S, W, 50, 0x2748774c);
    RNDr(S, W, 51, 0x34b0bcb5);
    RNDr(S, W, 52, 0x391c0cb3);
    RNDr(S, W, 53, 0x4ed8aa4a);
    RNDr(S, W, 54, 0x5b9cca4f);
    RNDr(S, W, 55, 0x682e6ff3);
    RNDr(S, W, 56, 0x748f82ee);
    RNDr(S, W, 57, 0x78a5636f);
    RNDr(S, W, 58, 0x84c87814);
    RNDr(S, W, 59, 0x8cc70208);
    RNDr(S, W, 60, 0x90befffa);
    RNDr(S, W, 61, 0xa4506ceb);
    RNDr(S, W, 62, 0xbef9a3f7);
    RNDr(S, W, 63, 0xc67178f2);

    for (int i = 0; i < 8; i++)
        state[i] += S[i];
}

#undef Ch
#undef Maj
#undef SHR
#undef ROTR
#undef S0
#undef S1
#undef s0
#undef s1
#undef RND
#undef RNDr

void cccc_sha256_init(CcccSha256 *ctx) {
    ctx->count[0] = ctx->count[1] = 0;
    ctx->state[0]                 = 0x6A09E667;
    ctx->state[1]                 = 0xBB67AE85;
    ctx->state[2]                 = 0x3C6EF372;
    ctx->state[3]                 = 0xA54FF53A;
    ctx->state[4]                 = 0x510E527F;
    ctx->state[5]                 = 0x9B05688C;
    ctx->state[6]                 = 0x1F83D9AB;
    ctx->state[7]                 = 0x5BE0CD19;
}

void cccc_sha256_update(CcccSha256 *ctx, const void *in, size_t len) {
    const unsigned char *src = in;

    // Bytes already buffered from earlier updates.
    uint32_t r         = (ctx->count[1] >> 3) & 0x3f;

    uint32_t bitlen_lo = (uint32_t)len << 3;
    uint32_t bitlen_hi = (uint32_t)(len >> 29);
    if ((ctx->count[1] += bitlen_lo) < bitlen_lo)
        ctx->count[0]++;
    ctx->count[0] += bitlen_hi;

    if (len < 64 - r) {
        memcpy(&ctx->buf[r], src, len);
        return;
    }

    memcpy(&ctx->buf[r], src, 64 - r);
    transform(ctx->state, ctx->buf);
    src += 64 - r;
    len -= 64 - r;

    while (len >= 64) {
        transform(ctx->state, src);
        src += 64;
        len -= 64;
    }
    memcpy(ctx->buf, src, len);
}

static const unsigned char PAD[64] = {0x80};

void cccc_sha256_final(CcccSha256 *ctx, unsigned char digest[32]) {
    // Capture the bit count before padding changes it.
    unsigned char len[8];
    be32enc(len, ctx->count[0]);
    be32enc(len + 4, ctx->count[1]);

    // Pad to 56 mod 64, then append the 64-bit length.
    uint32_t r    = (ctx->count[1] >> 3) & 0x3f;
    uint32_t plen = (r < 56) ? (56 - r) : (120 - r);
    cccc_sha256_update(ctx, PAD, plen);
    cccc_sha256_update(ctx, len, 8);

    for (int i = 0; i < 8; i++)
        be32enc(digest + i * 4, ctx->state[i]);
    memset(ctx, 0, sizeof *ctx);
}

void cccc_sha256_hex(const void *data, size_t len, char out[65]) {
    static const char digits[] = "0123456789abcdef";
    CcccSha256        ctx;
    unsigned char     digest[32];
    cccc_sha256_init(&ctx);
    cccc_sha256_update(&ctx, data, len);
    cccc_sha256_final(&ctx, digest);
    for (int i = 0; i < 32; i++) {
        out[i * 2]     = digits[digest[i] >> 4];
        out[i * 2 + 1] = digits[digest[i] & 15];
    }
    out[64] = '\0';
}
