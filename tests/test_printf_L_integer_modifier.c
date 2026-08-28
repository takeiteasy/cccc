// CCCC_NATIVE_SKIP: %Ld/%Li/%Lu/%Lo/%Lx/%Ln treating `L` as `ll` on an
// integer conversion is a glibc extension (the C standard leaves `L` on `d`
// etc. undefined); BSD/Apple libc reads/writes a 32-bit slot instead, so the
// -c=native round-trip output is host-libc-dependent -- see man/COVERAGE.md
// "Serialized-output divergences" and #1170. The VM formatter deliberately
// follows glibc here.
/*
 * #1228: the VM's formatter used to treat the `L` length modifier as
 * float-only, so `%Ld` read a 32-bit argument slot (and scanf `%Ld` stored
 * only 4 bytes), silently truncating and -- worse -- desyncing every later
 * conversion in the same call. `L` on d/i/u/o/x/X is now the same
 * wide-integer flag as `ll`, in both the printf and scanf directions,
 * matching glibc.
 *
 * #1230: the printf `%n` conversion had the same float-only blind spot, so
 * `%Ln` stored a 4-byte int. It now writes a `long long`, matching `%lln`
 * and glibc. (scanf `%Ln` already stored 8 bytes via store_int since #1228.)
 */

#include "stdio.h"
#include "string.h"

int main(void) {
    char      buf[64];
    long long big = 0x1FFFFFFFFLL; // 8589934591, needs more than 32 bits

    snprintf(buf, sizeof buf, "%Ld", big);
    if (strcmp(buf, "8589934591") != 0)
        return 1;

    snprintf(buf, sizeof buf, "%Li", -big);
    if (strcmp(buf, "-8589934591") != 0)
        return 2;

    snprintf(buf, sizeof buf, "%Lu", big);
    if (strcmp(buf, "8589934591") != 0)
        return 3;

    snprintf(buf, sizeof buf, "%Lx", big);
    if (strcmp(buf, "1ffffffff") != 0)
        return 4;

    snprintf(buf, sizeof buf, "%Lo", big);
    if (strcmp(buf, "77777777777") != 0)
        return 5;

    // Slot-desync guard: the conversion must consume exactly one argument.
    snprintf(buf, sizeof buf, "[%Ld][%d]", big, 7);
    if (strcmp(buf, "[8589934591][7]") != 0)
        return 6;

    // scanf side: %Ld must store a full 64-bit value.
    long long got = 0;
    if (sscanf("8589934591", "%Ld", &got) != 1)
        return 7;
    if (got != big)
        return 8;

    // printf `%Ln`: `L` == `ll`, so it writes a full 8-byte `long long`.
    // Pre-poison the high half and confirm it landed as zero.
    long long nll = (long long)0xAAAAAAAAAAAAAAAALL;
    snprintf(buf, sizeof buf, "abcd%Ln", &nll);
    if (nll != 4)
        return 9;

    // scanf `%Ln` likewise stores 8 bytes (store_int, LEN_L == LEN_ll).
    long long nsc = (long long)0xAAAAAAAAAAAAAAAALL;
    if (sscanf("abcdef", "abcd%Ln", &nsc) != 0)
        return 10;
    if (nsc != 4)
        return 11;

    return 42;
}
