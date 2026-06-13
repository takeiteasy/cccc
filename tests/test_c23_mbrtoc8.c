// CCCC_FLAGS: --std=c23
// Test C23 mbrtoc8/c8rtomb (ticket #399): incremental UTF-8 <-> char8_t
// conversion, including the (size_t)-3 "queued byte" convention, input
// split across calls, and invalid sequences.
#include <locale.h>
#include <string.h>
#include <uchar.h>

int main(void) {
    if (!setlocale(LC_ALL, "en_US.UTF-8") && !setlocale(LC_ALL, "C.UTF-8"))
        return 1;

    mbstate_t st;
    char8_t c8;
    size_t rc;

    // ASCII 'A' - single byte, consumed immediately.
    memset(&st, 0, sizeof(st));
    rc = mbrtoc8(&c8, "A", 1, &st);
    if (rc != 1) return 2;
    if (c8 != 'A') return 3;

    // U+00E9 (e acute) -> UTF-8 0xC3 0xA9, all input available at once.
    // First call consumes both input bytes and emits the first char8_t;
    // the second byte is queued and drained via (size_t)-3.
    memset(&st, 0, sizeof(st));
    rc = mbrtoc8(&c8, "\xC3\xA9", 2, &st);
    if (rc != 2) return 4;
    if (c8 != 0xC3) return 5;
    rc = mbrtoc8(&c8, "", 0, &st);
    if (rc != (size_t)-3) return 6;
    if (c8 != 0xA9) return 7;

    // Same character, but input split across calls: first call only sees
    // the lead byte (incomplete sequence -> (size_t)-2), second call
    // completes it. Verifies the queued-byte state doesn't collide with
    // mbrtowc's own incomplete-sequence state in *ps.
    memset(&st, 0, sizeof(st));
    rc = mbrtoc8(&c8, "\xC3", 1, &st);
    if (rc != (size_t)-2) return 8;
    rc = mbrtoc8(&c8, "\xA9", 1, &st);
    if (rc == (size_t)-1 || rc == (size_t)-2) return 9;
    if (c8 != 0xC3) return 10;
    rc = mbrtoc8(&c8, "", 0, &st);
    if (rc != (size_t)-3) return 11;
    if (c8 != 0xA9) return 12;

    // U+20AC (EURO SIGN) -> UTF-8 0xE2 0x82 0xAC (3 bytes).
    memset(&st, 0, sizeof(st));
    rc = mbrtoc8(&c8, "\xE2\x82\xAC", 3, &st);
    if (rc == (size_t)-1 || rc == (size_t)-2) return 13;
    if (c8 != 0xE2) return 14;
    rc = mbrtoc8(&c8, "", 0, &st);
    if (rc != (size_t)-3 || c8 != 0x82) return 15;
    rc = mbrtoc8(&c8, "", 0, &st);
    if (rc != (size_t)-3 || c8 != 0xAC) return 16;

    // U+1F600 (GRINNING FACE) -> UTF-8 0xF0 0x9F 0x98 0x80 (4 bytes).
    memset(&st, 0, sizeof(st));
    rc = mbrtoc8(&c8, "\xF0\x9F\x98\x80", 4, &st);
    if (rc == (size_t)-1 || rc == (size_t)-2) return 17;
    if (c8 != 0xF0) return 18;
    rc = mbrtoc8(&c8, "", 0, &st);
    if (rc != (size_t)-3 || c8 != 0x9F) return 19;
    rc = mbrtoc8(&c8, "", 0, &st);
    if (rc != (size_t)-3 || c8 != 0x98) return 20;
    rc = mbrtoc8(&c8, "", 0, &st);
    if (rc != (size_t)-3 || c8 != 0x80) return 21;

    // mbrtoc8 with an invalid lead byte should report EILSEQ.
    memset(&st, 0, sizeof(st));
    rc = mbrtoc8(&c8, "\x80", 1, &st);
    if (rc != (size_t)-1) return 22;

    // mbrtoc8 with a null input character.
    memset(&st, 0, sizeof(st));
    rc = mbrtoc8(&c8, "", 1, &st);
    if (rc != 0 || c8 != 0) return 23;

    // c8rtomb: feed the EURO SIGN's three char8_t units one at a time;
    // should return 0 while incomplete, then the encoded byte count.
    char buf[8];
    memset(&st, 0, sizeof(st));
    if (c8rtomb(buf, 0xE2, &st) != 0) return 24;
    if (c8rtomb(buf, 0x82, &st) != 0) return 25;
    rc = c8rtomb(buf, 0xAC, &st);
    if (rc == 0 || rc == (size_t)-1) return 26;
    if (memcmp(buf, "\xE2\x82\xAC", rc) != 0) return 27;

    // c8rtomb round trip for the 4-byte GRINNING FACE sequence.
    memset(&st, 0, sizeof(st));
    if (c8rtomb(buf, 0xF0, &st) != 0) return 28;
    if (c8rtomb(buf, 0x9F, &st) != 0) return 29;
    if (c8rtomb(buf, 0x98, &st) != 0) return 30;
    rc = c8rtomb(buf, 0x80, &st);
    if (rc == 0 || rc == (size_t)-1) return 31;
    if (memcmp(buf, "\xF0\x9F\x98\x80", rc) != 0) return 32;

    // c8rtomb with an invalid lead byte should report EILSEQ.
    memset(&st, 0, sizeof(st));
    if (c8rtomb(buf, 0xFF, &st) != (size_t)-1) return 33;

    // c8rtomb null terminator.
    memset(&st, 0, sizeof(st));
    rc = c8rtomb(buf, 0, &st);
    if (rc != 1 || buf[0] != '\0') return 34;

    return 42;
}
