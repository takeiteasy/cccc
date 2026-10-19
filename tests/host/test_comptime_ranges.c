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

// Host-side check of the comptime host-range registry against a brute-force
// model: overlap eviction, drop by owner, adjacent-range merging, tree nodes.
// The addresses are never dereferenced.

#include "internal.h"

#include <stdio.h>
#include <string.h>

#define BASE   0x100000u
#define SLOTS  256
#define OWNERS 12
#define MAXM   4096
#define STEPS  40000

typedef struct {
    uintptr_t base;
    size_t    len;
    uintptr_t owner;
    bool      writable, tree;
} Model;

static Model model[MAXM];
static int   model_n;
static int   failed;

#define CHECK(cond, ...)                                                       \
    do {                                                                       \
        if (!(cond)) {                                                         \
            fprintf(stderr, "FAIL (%s:%d): ", __FILE__, __LINE__);             \
            fprintf(stderr, __VA_ARGS__);                                      \
            fprintf(stderr, "\n");                                             \
            failed = 1;                                                        \
        }                                                                      \
    } while (0)

static uint64_t rng = 0x9e3779b97f4a7c15ull;

static unsigned rnd(unsigned n) {
    rng ^= rng << 13;
    rng ^= rng >> 7;
    rng ^= rng << 17;
    return (unsigned)(rng % n);
}

static void model_remove(int i) {
    model[i] = model[--model_n];
}

static void model_add(uintptr_t base, size_t len, uintptr_t owner, bool w,
                      bool tree) {
    for (int i = 0; i < model_n;) {
        if (model[i].base < base + len && base < model[i].base + model[i].len)
            model_remove(i);
        else
            i++;
    }
    model[model_n++] = (Model){base, len, owner, w, tree};
}

static int model_find(uintptr_t a) {
    for (int i = 0; i < model_n; i++)
        if (model[i].base <= a && a < model[i].base + model[i].len)
            return i;
    return -1;
}

static size_t model_left(uintptr_t a, bool write) {
    int i = model_find(a);
    if (i < 0 || (write && !model[i].writable))
        return 0;
    uintptr_t end  = model[i].base + model[i].len;
    size_t    left = end - a;
    for (;;) {
        int j = -1;
        for (int k = 0; k < model_n; k++)
            if (model[k].base == end)
                j = k;
        if (j < 0 || (write && !model[j].writable))
            return left;
        end  += model[j].len;
        left += model[j].len;
    }
}

static bool model_node_known(uintptr_t a, uintptr_t tree) {
    for (int i = 0; i < model_n; i++)
        if (model[i].base == a && model[i].tree &&
            (!tree || model[i].owner == tree))
            return true;
    return false;
}

static void verify(void) {
    for (int q = 0; q < 24; q++) {
        uintptr_t a = BASE + rnd(SLOTS * 8 + 64);
        for (int w = 0; w < 2; w++) {
            size_t want = model_left(a, w);
            size_t got  = cccc_comptime_host_range_left((void *)a, w);
            CHECK(want == got, "left(%#lx, w=%d): want %zu got %zu",
                  (unsigned long)a, w, want, got);
        }
        uintptr_t tree = rnd(3) ? 1 + rnd(OWNERS) : 0;
        bool      want = model_node_known(a, tree);
        bool      got  = cccc_comptime_host_node_known((void *)a, (void *)tree);
        CHECK(want == got, "node_known(%#lx, %lu): want %d got %d",
              (unsigned long)a, (unsigned long)tree, want, got);
    }
}

int main(void) {
    for (int step = 0; step < STEPS && !failed; step++) {
        unsigned op = rnd(100);
        if (op < 60) {
            uintptr_t base  = BASE + rnd(SLOTS) * 8;
            size_t    len   = rnd(4) ? 8 * (1 + rnd(4)) : 1 + rnd(40);
            uintptr_t owner = 1 + rnd(OWNERS);
            bool      w     = rnd(2);
            cccc_comptime_host_range_add((void *)base, len, (void *)owner, w);
            model_add(base, len, owner, w, false);
        } else if (op < 75) {
            uintptr_t base  = BASE + rnd(SLOTS) * 8;
            uintptr_t owner = 1 + rnd(OWNERS);
            cccc_comptime_host_node_add((void *)base, (void *)owner);
            model_add(base, sizeof(void *), owner, false, true);
        } else if (op < 90) {
            uintptr_t owner = 1 + rnd(OWNERS);
            cccc_comptime_host_range_drop((void *)owner);
            for (int i = 0; i < model_n;) {
                if (model[i].owner == owner)
                    model_remove(i);
                else
                    i++;
            }
        }
        verify();
    }

    for (uintptr_t owner = 1; owner <= OWNERS; owner++)
        cccc_comptime_host_range_drop((void *)owner);
    for (unsigned s = 0; s < SLOTS * 8 + 64; s++)
        CHECK(cccc_comptime_host_range_left((void *)(BASE + s), false) == 0,
              "range left after dropping every owner at +%u", s);

    if (failed)
        return 1;
    printf("PASS: comptime host ranges\n");
    return 0;
}
