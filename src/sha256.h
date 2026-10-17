#ifndef CCCC_SHA256_H
#define CCCC_SHA256_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t      state[8];
    uint32_t      count[2]; // bits processed, high word first
    unsigned char buf[64];
} CcccSha256;

void cccc_sha256_init(CcccSha256 *ctx);
void cccc_sha256_update(CcccSha256 *ctx, const void *in, size_t len);
// Writes the digest and clears `ctx`.
void cccc_sha256_final(CcccSha256 *ctx, unsigned char digest[32]);

// One-shot digest of `data` as 64 lowercase hex digits plus a NUL.
void cccc_sha256_hex(const void *data, size_t len, char out[65]);

#endif
