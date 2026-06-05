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

 tools/bytecode_ngrams.c - Static opcode n-gram miner for .jbc files.

 Walks the text segment of one or more .jbc files and ranks 2-grams and
 3-grams by occurrence. Used to discover common instruction sequences
 that are candidates for opcode fusion (see ticket #250).

 Usage:
   tools/bytecode_ngrams [options] file.jbc [file2.jbc ...]

 Options:
   -n N    N-gram size: 2 (default) or 3
   -t N    Show top N sequences (default 25)
   -p      Print per-file results in addition to the aggregate
   -q      Suppress per-file section, aggregate only (default)
   -h      Show this help

 The .jbc file format matches src/bytecode.c: the text segment is a
 sequence of 32-bit words, with text_seg[0] holding the entry point.
 The first word of each instruction is the opcode, followed by operand
 words (see OPS_X in src/jcc.h). The text segment length in bytes is
 at offset 12 in the file.
*/

#include "jcc.h"
#include "internal.h"

#include <ctype.h>
#include <getopt.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define INITIAL_CAP 1024
#define LOAD_FACTOR_NUM 3
#define LOAD_FACTOR_DEN 4

typedef struct {
    uint64_t key;
    uint64_t count;
} NGramEntry;

typedef struct {
    NGramEntry *entries;
    size_t capacity;
    size_t size;
} NGramMap;

static const char *progname = "bytecode_ngrams";

static void map_init(NGramMap *m) {
    m->capacity = INITIAL_CAP;
    m->size = 0;
    m->entries = (NGramEntry *)calloc(m->capacity, sizeof(NGramEntry));
    if (!m->entries) {
        fprintf(stderr, "%s: out of memory\n", progname);
        exit(1);
    }
}

static void map_free(NGramMap *m) {
    free(m->entries);
    m->entries = NULL;
    m->capacity = 0;
    m->size = 0;
}

static void map_grow(NGramMap *m) {
    size_t old_cap = m->capacity;
    NGramEntry *old = m->entries;
    m->capacity = old_cap * 2;
    m->entries = (NGramEntry *)calloc(m->capacity, sizeof(NGramEntry));
    if (!m->entries) {
        fprintf(stderr, "%s: out of memory\n", progname);
        exit(1);
    }
    m->size = 0;
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

static void map_increment(NGramMap *m, uint64_t key) {
    if ((m->size + 1) * LOAD_FACTOR_DEN >
        m->capacity * LOAD_FACTOR_NUM) {
        map_grow(m);
    }
    size_t mask = m->capacity - 1;
    size_t h = (size_t)(key * 0x9E3779B97F4A7C15ULL) & mask;
    while (1) {
        if (m->entries[h].count == 0) {
            m->entries[h].key = key;
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

static int entry_cmp_desc(const void *a, const void *b) {
    uint64_t ca = ((const NGramEntry *)a)->count;
    uint64_t cb = ((const NGramEntry *)b)->count;
    if (ca < cb) return 1;
    if (ca > cb) return -1;
    uint64_t ka = ((const NGramEntry *)a)->key;
    uint64_t kb = ((const NGramEntry *)b)->key;
    if (ka < kb) return -1;
    if (ka > kb) return 1;
    return 0;
}

static NGramEntry *map_collect_sorted(const NGramMap *m, size_t *out_n) {
    NGramEntry *arr = (NGramEntry *)malloc(m->size * sizeof(NGramEntry));
    if (!arr) {
        fprintf(stderr, "%s: out of memory\n", progname);
        exit(1);
    }
    size_t n = 0;
    for (size_t i = 0; i < m->capacity; i++) {
        if (m->entries[i].count == 0)
            continue;
        arr[n++] = m->entries[i];
    }
    qsort(arr, n, sizeof(NGramEntry), entry_cmp_desc);
    *out_n = n;
    return arr;
}

static uint64_t pack_n(const int *ops, int n) {
    uint64_t key = 0;
    for (int i = 0; i < n; i++)
        key = (key << 8) | (uint64_t)(ops[i] & 0xFF);
    return key;
}

static void unpack_n(uint64_t key, int *ops, int n) {
    for (int i = n - 1; i >= 0; i--) {
        ops[i] = (int)(key & 0xFF);
        key >>= 8;
    }
}

static const char *safe_name(int op) {
    const char *s = cc_opcode_name(op);
    return s ? s : "OP_?";
}

// Walk a single text segment, count n-grams.
// stream: opcode array extracted from text_seg
// n: 2 or 3
// map: counter (incremented in place)
static void count_ngrams(const int *stream, int len, int n, NGramMap *map) {
    if (len < n)
        return;
    int window[3];
    for (int i = 0; i + n <= len; i++) {
        for (int j = 0; j < n; j++)
            window[j] = stream[i + j];
        map_increment(map, pack_n(window, n));
    }
}

// Extract opcode stream from text_seg, starting at PC=1 (skip entry point).
// Returns the number of opcodes written to `out` (capped at max_out).
static int extract_opcode_stream(const JCCInstrWord *text_seg,
                                 long long num_words, int *out, int max_out) {
    int count = 0;
    long long pc = 1;
    while (pc < num_words && count < max_out) {
        JCCInstrWord op = text_seg[pc];
        int words = cc_instr_words((int)op);
        if (words <= 0)
            break;
        out[count++] = (int)op;
        pc += words;
    }
    return count;
}

// Read the .jbc file format and return the text segment.
// .jbc header (V5/V6/V7):
//   4 bytes  magic "JCC\0"
//   4 bytes  version
//   4 bytes  flags
//   8 bytes  text_size
//   8 bytes  data_size
//   8 bytes  main_offset
//   8 bytes  data_reloc_count
//   text_size bytes text segment
//   data_size bytes data segment
// Returns 0 on success, -1 on failure.
static int load_text_segment(const char *path, JCCInstrWord **out_text,
                             long long *out_num_words) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "%s: cannot open %s: %s\n", progname, path,
                strerror(errno));
        return -1;
    }

    char magic[4];
    if (fread(magic, 1, 4, f) != 4 || memcmp(magic, JCC_MAGIC, 4) != 0) {
        fprintf(stderr, "%s: %s: not a JCC bytecode file (bad magic)\n",
                progname, path);
        fclose(f);
        return -1;
    }

    int version = 0;
    if (fread(&version, sizeof(int), 1, f) != 1) {
        fprintf(stderr, "%s: %s: truncated version field\n", progname, path);
        fclose(f);
        return -1;
    }
    if (version != JCC_VERSION) {
        fprintf(stderr, "%s: %s: unsupported bytecode version %d (expected %d)\n",
                progname, path, version, JCC_VERSION);
        fclose(f);
        return -1;
    }

    uint32_t flags_unused = 0;
    if (fread(&flags_unused, sizeof(uint32_t), 1, f) != 1) {
        fprintf(stderr, "%s: %s: truncated flags field\n", progname, path);
        fclose(f);
        return -1;
    }

    long long text_size = 0;
    if (fread(&text_size, sizeof(long long), 1, f) != 1) {
        fprintf(stderr, "%s: %s: truncated text_size field\n", progname, path);
        fclose(f);
        return -1;
    }
    if (text_size <= 0 || text_size % (long long)sizeof(JCCInstrWord) != 0) {
        fprintf(stderr, "%s: %s: invalid text_size %lld\n", progname, path,
                text_size);
        fclose(f);
        return -1;
    }

    long long num_words = text_size / (long long)sizeof(JCCInstrWord);
    JCCInstrWord *text =
        (JCCInstrWord *)malloc((size_t)text_size);
    if (!text) {
        fprintf(stderr, "%s: out of memory\n", progname);
        fclose(f);
        return -1;
    }
    if (fread(text, 1, (size_t)text_size, f) != (size_t)text_size) {
        fprintf(stderr, "%s: %s: truncated text segment\n", progname, path);
        free(text);
        fclose(f);
        return -1;
    }
    fclose(f);

    *out_text = text;
    *out_num_words = num_words;
    return 0;
}

static int max_name_width(const int *ops, int n) {
    int w = 0;
    for (int i = 0; i < n; i++) {
        int len = (int)strlen(safe_name(ops[i]));
        if (len > w) w = len;
    }
    return w;
}

static void print_row(FILE *f, uint64_t count, uint64_t total,
                      const int *ops, int n) {
    int w = max_name_width(ops, n);
    double pct = total > 0 ? 100.0 * (double)count / (double)total : 0.0;
    fprintf(f, "  %7" PRIu64 "  ", count);
    for (int i = 0; i < n; i++) {
        fprintf(f, "%-*s", w + 2, safe_name(ops[i]));
    }
    fprintf(f, " (%5.2f%%)\n", pct);
}

static void print_section(FILE *f, const char *title, int n, const NGramMap *map,
                          uint64_t total_ngrams, int top_n,
                          const char *source) {
    if (map->size == 0) {
        fprintf(f, "\n=== %s ===\n  (no sequences)\n", title);
        return;
    }
    size_t count = 0;
    NGramEntry *entries = map_collect_sorted(map, &count);
    int show = (int)count < top_n ? (int)count : top_n;
    fprintf(f, "\n=== %s ===\n", title);
    fprintf(f, "  source:        %s\n", source);
    fprintf(f, "  unique:        %zu\n", count);
    fprintf(f, "  total n-grams: %" PRIu64 "\n", total_ngrams);
    fprintf(f, "  top %d:\n", show);
    int ops[3];
    for (int i = 0; i < show; i++) {
        unpack_n(entries[i].key, ops, n);
        print_row(f, entries[i].count, total_ngrams, ops, n);
    }
    free(entries);
}

static void usage(FILE *f) {
    fprintf(f,
            "Usage: %s [options] file.jbc [file.jbc ...]\n"
            "\n"
            "Static opcode n-gram miner for JCC .jbc files.\n"
            "Ranks 2-grams and 3-grams by occurrence to surface common\n"
            "instruction sequences that may be candidates for fusion.\n"
            "\n"
            "Options:\n"
            "  -n N    N-gram size: 2 (default) or 3\n"
            "  -t N    Show top N sequences per section (default 25)\n"
            "  -p      Also print a per-file section for each input\n"
            "  -h      Show this help\n",
            progname);
}

int main(int argc, char **argv) {
    progname = argv[0];
    int n = 2;
    int top_n = 25;
    int per_file = 0;
    int opt;
    while ((opt = getopt(argc, argv, "n:t:ph")) != -1) {
        switch (opt) {
        case 'n':
            n = atoi(optarg);
            if (n != 2 && n != 3) {
                fprintf(stderr, "%s: -n must be 2 or 3\n", progname);
                return 1;
            }
            break;
        case 't':
            top_n = atoi(optarg);
            if (top_n <= 0) {
                fprintf(stderr, "%s: -t must be > 0\n", progname);
                return 1;
            }
            break;
        case 'p':
            per_file = 1;
            break;
        case 'h':
            usage(stdout);
            return 0;
        default:
            usage(stderr);
            return 1;
        }
    }
    if (optind >= argc) {
        usage(stderr);
        return 1;
    }

    int *stream = NULL;
    int stream_cap = 0;
    NGramMap agg;
    map_init(&agg);
    uint64_t agg_ngrams = 0;
    uint64_t agg_opcodes = 0;

    for (int i = optind; i < argc; i++) {
        const char *path = argv[i];
        JCCInstrWord *text = NULL;
        long long num_words = 0;
        if (load_text_segment(path, &text, &num_words) != 0)
            continue;

        int needed = (int)num_words;
        if (needed > stream_cap) {
            free(stream);
            stream_cap = needed;
            stream = (int *)malloc((size_t)stream_cap * sizeof(int));
            if (!stream) {
                fprintf(stderr, "%s: out of memory\n", progname);
                free(text);
                return 1;
            }
        }
        int len = extract_opcode_stream(text, num_words, stream, stream_cap);

        if (per_file) {
            NGramMap m;
            map_init(&m);
            count_ngrams(stream, len, n, &m);
            uint64_t total = 0;
            if (len >= n)
                total = (uint64_t)(len - n + 1);
            char title[64];
            snprintf(title, sizeof(title), "%d-grams: %s", n, path);
            print_section(stdout, title, n, &m, total, top_n, path);
            map_free(&m);
        }

        count_ngrams(stream, len, n, &agg);
        if (len >= n)
            agg_ngrams += (uint64_t)(len - n + 1);
        agg_opcodes += (uint64_t)len;

        free(text);
    }

    free(stream);

    char agg_title[64];
    snprintf(agg_title, sizeof(agg_title), "%d-grams: aggregate", n);
    print_section(stdout, agg_title, n, &agg, agg_ngrams, top_n,
                  argc - optind == 1 ? argv[optind] : "<multiple files>");
    fprintf(stdout, "\n  total opcodes:  %" PRIu64 "\n", agg_opcodes);
    fprintf(stdout, "  total n-grams:  %" PRIu64 "\n", agg_ngrams);

    map_free(&agg);
    return 0;
}
