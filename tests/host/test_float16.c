/*
 CCCC: Comprehiensive C Compensation Compiler

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

// The binary16 conversions in src/float16.h: every half pattern round-trips,
// the rounding boundaries are exact, and (where the host has `_Float16`) the
// conversion agrees with the host's on a large pseudo-random sample.

#include "float16.h"

#include <math.h>
#include <stdio.h>

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

static void expect(const char *what, double d, unsigned want) {
    unsigned got = cccc_f16_from_double(d);
    CHECK(got == want, "%s: got 0x%04x, want 0x%04x", what, got, want);
}

static void test_boundaries(void) {
    expect("zero", 0.0, 0x0000);
    expect("negative zero", -0.0, 0x8000);
    expect("one", 1.0, 0x3c00);
    expect("max", 65504.0, 0x7bff);
    expect("below the overflow tie", 65519.99, 0x7bff);
    expect("overflow tie rounds to even (inf)", 65520.0, 0x7c00);
    expect("inf", INFINITY, 0x7c00);
    expect("-inf", -INFINITY, 0xfc00);
    expect("smallest subnormal", ldexp(1.0, -24), 0x0001);
    expect("below half the smallest subnormal", ldexp(1.0, -26), 0x0000);
    expect("tie below the smallest subnormal rounds to 0", ldexp(1.0, -25),
           0x0000);
    expect("just above that tie", ldexp(1.0, -25) * (1 + 1e-12), 0x0001);
    expect("smallest normal", ldexp(1.0, -14), 0x0400);
    expect("largest subnormal", ldexp(1023.0, -24), 0x03ff);
    expect("tie to even keeps 1.0", 1.0 + ldexp(1.0, -11), 0x3c00);
    expect("tie to even rounds up", 1.0 + 3 * ldexp(1.0, -11), 0x3c02);
    expect("subnormal carry into the smallest normal",
           ldexp(1023.5, -24) * (1 + 1e-12), 0x0400);
    unsigned nan = cccc_f16_from_double(NAN);
    CHECK((nan & 0x7c00) == 0x7c00 && (nan & 0x3ff), "NaN stays a NaN");
}

static void test_round_trip(void) {
    for (unsigned h = 0; h < 0x10000; h++) {
        double d = cccc_f16_to_double((uint16_t)h);
        if ((h & 0x7c00) == 0x7c00 && (h & 0x3ff)) {
            CHECK(isnan(d), "0x%04x should widen to NaN", h);
            continue;
        }
        unsigned back = cccc_f16_from_double(d);
        CHECK(back == h, "0x%04x round-tripped to 0x%04x", h, back);
    }
}

#ifdef __FLT16_MAX__
static void test_against_host(void) {
    unsigned long long x = 0x9e3779b97f4a7c15ULL;
    for (int i = 0; i < 2000000; i++) {
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        double d;
        if (i & 1) {
            memcpy(&d, &x, sizeof d);
        } else {
            d = ldexp((double)(x >> 11) / 9007199254740992.0,
                      (int)(x % 50) - 28);
            if (x & 1)
                d = -d;
        }
        if (isnan(d))
            continue;
        _Float16 host = (_Float16)d;
        uint16_t want;
        memcpy(&want, &host, sizeof want);
        unsigned got = cccc_f16_from_double(d);
        CHECK(got == want, "%a: got 0x%04x, host 0x%04x", d, got, want);
        if (failed)
            return;
    }
    for (unsigned h = 0; h < 0x10000; h++) {
        uint16_t hh = (uint16_t)h;
        _Float16 host;
        memcpy(&host, &hh, sizeof host);
        double want = (double)host, got = cccc_f16_to_double(hh);
        CHECK((isnan(want) && isnan(got)) || want == got,
              "0x%04x widened to %a, host %a", h, got, want);
    }
}
#endif

int main(void) {
    test_boundaries();
    test_round_trip();
#ifdef __FLT16_MAX__
    test_against_host();
#endif
    if (!failed)
        printf("PASS: float16\n");
    return failed;
}
