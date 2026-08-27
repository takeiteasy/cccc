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

 src/analyze.c - Static bytecode analysis passes for the cccc binary.

 Provides two in-process analyses that used to live in the standalone
 tools/bytecode_ngrams and tools/fusion_candidates executables:

   - n-gram mining (2-gram or 3-gram opcode sequences) -- used to
     surface common bytecode patterns that may be candidates for
     opcode fusion.
   - use-def fusion candidate detection -- walks the text segment,
     tracks register defs/uses per instruction, and reports adjacent
     def->use pairs where the defining instruction has a single
     reader. The strongest candidates for new fused opcodes.

 Both analyses work on a InstrWord* text segment, which the caller has
 already produced via cc_compile for .c source. See the public functions
 below.
*/

#include "./internal.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//
// ======================= opcode n-gram mining =======================
//

#define NGRAM_INITIAL_CAP     1024
#define NGRAM_LOAD_FACTOR_NUM 3
#define NGRAM_LOAD_FACTOR_DEN 4

typedef struct {
    uint64_t key;
    uint64_t count;
} NGramEntry;

typedef struct {
    NGramEntry *entries;
    size_t      capacity;
    size_t      size;
} NGramMap;

typedef struct CcNgramState {
    CcAnalyzeNgramOptions opts;
    NGramMap              agg;
    uint64_t              agg_ngrams;
    uint64_t              agg_opcodes;
} CcNgramState;

static const char *safe_opcode_name(int op) {
    const char *s = cc_opcode_name(op);
    return s ? s : "OP_?";
}

static void ngram_map_init(NGramMap *m) {
    m->capacity = NGRAM_INITIAL_CAP;
    m->size     = 0;
    m->entries  = (NGramEntry *)calloc(m->capacity, sizeof(NGramEntry));
    if (!m->entries) {
        fprintf(stderr, "cccc: out of memory\n");
        exit(1);
    }
}

static void ngram_map_free(NGramMap *m) {
    free(m->entries);
    m->entries  = NULL;
    m->capacity = 0;
    m->size     = 0;
}

static void ngram_map_grow(NGramMap *m) {
    size_t      old_cap = m->capacity;
    NGramEntry *old     = m->entries;
    m->capacity         = old_cap * 2;
    m->entries          = (NGramEntry *)calloc(m->capacity, sizeof(NGramEntry));
    if (!m->entries) {
        fprintf(stderr, "cccc: out of memory\n");
        exit(1);
    }
    m->size     = 0;
    size_t mask = m->capacity - 1;
    for (size_t i = 0; i < old_cap; i++) {
        if (old[i].count == 0)
            continue;
        size_t h = (size_t)(old[i].key * 0x9E3779B97F4A7C15ULL) & mask;
        while (m->entries[h].count != 0)
            h = (h + 1) & mask;
        m->entries[h] = old[i];
        m->size++;
    }
    free(old);
}

static void ngram_map_increment(NGramMap *m, uint64_t key) {
    if ((m->size + 1) * NGRAM_LOAD_FACTOR_DEN >
        m->capacity * NGRAM_LOAD_FACTOR_NUM) {
        ngram_map_grow(m);
    }
    size_t mask = m->capacity - 1;
    size_t h    = (size_t)(key * 0x9E3779B97F4A7C15ULL) & mask;
    while (1) {
        if (m->entries[h].count == 0) {
            m->entries[h].key   = key;
            m->entries[h].count = 1;
            m->size++;
            return;
        }
        if (m->entries[h].key == key) {
            m->entries[h].count++;
            return;
        }
        h = (h + 1) & mask;
    }
}

static int ngram_entry_cmp_desc(const void *a, const void *b) {
    uint64_t ca = ((const NGramEntry *)a)->count;
    uint64_t cb = ((const NGramEntry *)b)->count;
    if (ca < cb)
        return 1;
    if (ca > cb)
        return -1;
    uint64_t ka = ((const NGramEntry *)a)->key;
    uint64_t kb = ((const NGramEntry *)b)->key;
    if (ka < kb)
        return -1;
    if (ka > kb)
        return 1;
    return 0;
}

static NGramEntry *ngram_map_collect_sorted(const NGramMap *m, size_t *out_n) {
    NGramEntry *arr = (NGramEntry *)malloc(m->size * sizeof(NGramEntry));
    if (!arr) {
        fprintf(stderr, "cccc: out of memory\n");
        exit(1);
    }
    size_t n = 0;
    for (size_t i = 0; i < m->capacity; i++) {
        if (m->entries[i].count == 0)
            continue;
        arr[n++] = m->entries[i];
    }
    qsort(arr, n, sizeof(NGramEntry), ngram_entry_cmp_desc);
    *out_n = n;
    return arr;
}

static uint64_t ngram_pack(const int *ops, int n) {
    uint64_t key = 0;
    for (int i = 0; i < n; i++)
        key = (key << 8) | (uint64_t)(ops[i] & 0xFF);
    return key;
}

static void ngram_unpack(uint64_t key, int *ops, int n) {
    for (int i = n - 1; i >= 0; i--) {
        ops[i]   = (int)(key & 0xFF);
        key    >>= 8;
    }
}

static int ngram_extract_stream(const InstrWord *text, long long num_words,
                                int *out, int max_out) {
    int       count = 0;
    long long pc    = 1; // text[0] is the entry point
    while (pc < num_words && count < max_out) {
        InstrWord op    = text[pc];
        int       words = cc_instr_words((int)op);
        if (words <= 0)
            break;
        out[count++]  = (int)op;
        pc           += words;
    }
    return count;
}

static void ngram_count(const int *stream, int len, int n, NGramMap *map) {
    if (len < n)
        return;
    int window[3];
    for (int i = 0; i + n <= len; i++) {
        for (int j = 0; j < n; j++)
            window[j] = stream[i + j];
        ngram_map_increment(map, ngram_pack(window, n));
    }
}

static int ngram_max_name_width(const int *ops, int n) {
    int w = 0;
    for (int i = 0; i < n; i++) {
        int len = (int)strlen(safe_opcode_name(ops[i]));
        if (len > w)
            w = len;
    }
    return w;
}

static void ngram_print_row(FILE *f, uint64_t count, uint64_t total,
                            const int *ops, int n) {
    int    w   = ngram_max_name_width(ops, n);
    double pct = total > 0 ? 100.0 * (double)count / (double)total : 0.0;
    fprintf(f, "  %7" PRIu64 "  ", count);
    for (int i = 0; i < n; i++) {
        fprintf(f, "%-*s", w + 2, safe_opcode_name(ops[i]));
    }
    fprintf(f, " (%5.2f%%)\n", pct);
}

static void ngram_print_section(FILE *f, const char *title, int n,
                                const NGramMap *map, uint64_t total_ngrams,
                                int top_n, const char *source) {
    if (map->size == 0) {
        fprintf(f, "\n=== %s ===\n  (no sequences)\n", title);
        return;
    }
    size_t      count   = 0;
    NGramEntry *entries = ngram_map_collect_sorted(map, &count);
    int         show    = (int)count < top_n ? (int)count : top_n;
    fprintf(f, "\n=== %s ===\n", title);
    fprintf(f, "  source:        %s\n", source);
    fprintf(f, "  unique:        %zu\n", count);
    fprintf(f, "  total n-grams: %" PRIu64 "\n", total_ngrams);
    fprintf(f, "  top %d:\n", show);
    int ops[3];
    for (int i = 0; i < show; i++) {
        ngram_unpack(entries[i].key, ops, n);
        ngram_print_row(f, entries[i].count, total_ngrams, ops, n);
    }
    free(entries);
}

CcNgramState *cc_analyze_ngram_begin(const CcAnalyzeNgramOptions *opts) {
    CcNgramState *st = (CcNgramState *)calloc(1, sizeof(CcNgramState));
    if (!st) {
        fprintf(stderr, "cccc: out of memory\n");
        exit(1);
    }
    st->opts = *opts;
    ngram_map_init(&st->agg);
    return st;
}

void cc_analyze_ngram_feed(CcNgramState *st, const InstrWord *text,
                           long long num_words, const char *label, FILE *out) {
    if (!st || !text || num_words <= 1)
        return;
    int  max_out = (int)num_words;
    int *stream  = (int *)malloc((size_t)max_out * sizeof(int));
    if (!stream) {
        fprintf(stderr, "cccc: out of memory\n");
        exit(1);
    }
    int len = ngram_extract_stream(text, num_words, stream, max_out);
    int n   = st->opts.n;

    if (st->opts.per_file) {
        NGramMap m;
        ngram_map_init(&m);
        ngram_count(stream, len, n, &m);
        uint64_t total = 0;
        if (len >= n)
            total = (uint64_t)(len - n + 1);
        char title[64];
        snprintf(title, sizeof(title), "%d-grams: %s", n, label);
        ngram_print_section(out, title, n, &m, total, st->opts.top_n, label);
        ngram_map_free(&m);
    }

    ngram_count(stream, len, n, &st->agg);
    if (len >= n)
        st->agg_ngrams += (uint64_t)(len - n + 1);
    st->agg_opcodes += (uint64_t)len;

    free(stream);
}

void cc_analyze_ngram_finish(CcNgramState *st, FILE *out) {
    if (!st)
        return;
    char title[64];
    snprintf(title, sizeof(title), "%d-grams: aggregate", st->opts.n);
    ngram_print_section(out, title, st->opts.n, &st->agg, st->agg_ngrams,
                        st->opts.top_n, "<aggregate>");
    fprintf(out, "\n  total opcodes:  %" PRIu64 "\n", st->agg_opcodes);
    fprintf(out, "  total n-grams:  %" PRIu64 "\n", st->agg_ngrams);
    ngram_map_free(&st->agg);
    free(st);
}
