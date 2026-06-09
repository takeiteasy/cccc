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

 Both analyses work on a CCCCInstrWord* text segment, which the caller
 has already loaded (via cc_load_bytecode for .jbc input or
 cc_compile for .c source). See the public functions below.
*/

#include "./internal.h"
#include "cccc.h"

#include <ctype.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//
// ======================= opcode n-gram mining =======================
//

#define NGRAM_INITIAL_CAP 1024
#define NGRAM_LOAD_FACTOR_NUM 3
#define NGRAM_LOAD_FACTOR_DEN 4

typedef struct {
    uint64_t key;
    uint64_t count;
} NGramEntry;

typedef struct {
    NGramEntry *entries;
    size_t capacity;
    size_t size;
} NGramMap;

typedef struct CcNgramState {
    CcAnalyzeNgramOptions opts;
    NGramMap agg;
    uint64_t agg_ngrams;
    uint64_t agg_opcodes;
} CcNgramState;

static const char *safe_opcode_name(int op) {
    const char *s = cc_opcode_name(op);
    return s ? s : "OP_?";
}

static void ngram_map_init(NGramMap *m) {
    m->capacity = NGRAM_INITIAL_CAP;
    m->size = 0;
    m->entries = (NGramEntry *)calloc(m->capacity, sizeof(NGramEntry));
    if (!m->entries) {
        fprintf(stderr, "cccc: out of memory\n");
        exit(1);
    }
}

static void ngram_map_free(NGramMap *m) {
    free(m->entries);
    m->entries = NULL;
    m->capacity = 0;
    m->size = 0;
}

static void ngram_map_grow(NGramMap *m) {
    size_t old_cap = m->capacity;
    NGramEntry *old = m->entries;
    m->capacity = old_cap * 2;
    m->entries = (NGramEntry *)calloc(m->capacity, sizeof(NGramEntry));
    if (!m->entries) {
        fprintf(stderr, "cccc: out of memory\n");
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

static void ngram_map_increment(NGramMap *m, uint64_t key) {
    if ((m->size + 1) * NGRAM_LOAD_FACTOR_DEN >
        m->capacity * NGRAM_LOAD_FACTOR_NUM) {
        ngram_map_grow(m);
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

static int ngram_entry_cmp_desc(const void *a, const void *b) {
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
        ops[i] = (int)(key & 0xFF);
        key >>= 8;
    }
}

static int ngram_extract_stream(const CCCCInstrWord *text, long long num_words,
                                int *out, int max_out) {
    int count = 0;
    long long pc = 1;  // text[0] is the entry point
    while (pc < num_words && count < max_out) {
        CCCCInstrWord op = text[pc];
        int words = cc_instr_words((int)op);
        if (words <= 0)
            break;
        out[count++] = (int)op;
        pc += words;
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
        if (len > w) w = len;
    }
    return w;
}

static void ngram_print_row(FILE *f, uint64_t count, uint64_t total,
                            const int *ops, int n) {
    int w = ngram_max_name_width(ops, n);
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
    size_t count = 0;
    NGramEntry *entries = ngram_map_collect_sorted(map, &count);
    int show = (int)count < top_n ? (int)count : top_n;
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

void cc_analyze_ngram_feed(CcNgramState *st, const CCCCInstrWord *text,
                           long long num_words, const char *label, FILE *out) {
    if (!st || !text || num_words <= 1)
        return;
    int max_out = (int)num_words;
    int *stream = (int *)malloc((size_t)max_out * sizeof(int));
    if (!stream) {
        fprintf(stderr, "cccc: out of memory\n");
        exit(1);
    }
    int len = ngram_extract_stream(text, num_words, stream, max_out);
    int n = st->opts.n;

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

//
// ======================= fusion candidate detection =======================
//

#define INVALID_REG (-1)

typedef struct {
    int8_t def_pos[3];
    int8_t use_pos[3];
    uint8_t n_defs;
    uint8_t n_uses;
} DefUseEntry;

// Symbolic positions within the first operand word:
//   P_RD  = byte 0 (rd)
//   P_RS1 = byte 1 (rs1)
//   P_RS2 = byte 2 (rs2)
//   -1    = unused slot
#define P_RD 0
#define P_RS1 1
#define P_RS2 2

static const DefUseEntry defuse_table[OP_COUNT] = {
    // Register arithmetic: rd = f(rs1, rs2)
    [ADD3] = {{P_RD}, {P_RS1, P_RS2}, 1, 2},
    [SUB3] = {{P_RD}, {P_RS1, P_RS2}, 1, 2},
    [MUL3] = {{P_RD}, {P_RS1, P_RS2}, 1, 2},
    [DIV3] = {{P_RD}, {P_RS1, P_RS2}, 1, 2},
    [ADDC] = {{P_RD}, {P_RS1, P_RS2}, 1, 2},
    [SUBC] = {{P_RD}, {P_RS1, P_RS2}, 1, 2},
    [MULC] = {{P_RD}, {P_RS1, P_RS2}, 1, 2},
    [DIVC] = {{P_RD}, {P_RS1, P_RS2}, 1, 2},
    [UDIV3] = {{P_RD}, {P_RS1, P_RS2}, 1, 2},
    [MOD3] = {{P_RD}, {P_RS1, P_RS2}, 1, 2},
    [UMOD3] = {{P_RD}, {P_RS1, P_RS2}, 1, 2},
    [AND3] = {{P_RD}, {P_RS1, P_RS2}, 1, 2},
    [OR3] = {{P_RD}, {P_RS1, P_RS2}, 1, 2},
    [XOR3] = {{P_RD}, {P_RS1, P_RS2}, 1, 2},
    [SHL3] = {{P_RD}, {P_RS1, P_RS2}, 1, 2},
    [SHR3] = {{P_RD}, {P_RS1, P_RS2}, 1, 2},
    [USHR3] = {{P_RD}, {P_RS1, P_RS2}, 1, 2},

    // Register compares: rd = (rs1 op rs2)
    [SEQ3] = {{P_RD}, {P_RS1, P_RS2}, 1, 2},
    [SNE3] = {{P_RD}, {P_RS1, P_RS2}, 1, 2},
    [SLT3] = {{P_RD}, {P_RS1, P_RS2}, 1, 2},
    [SGE3] = {{P_RD}, {P_RS1, P_RS2}, 1, 2},
    [SGT3] = {{P_RD}, {P_RS1, P_RS2}, 1, 2},
    [SLE3] = {{P_RD}, {P_RS1, P_RS2}, 1, 2},

    // Register ops: rd = ... (rs1 optional)
    [LI3] = {{P_RD}, {}, 1, 0},
    [LDA3] = {{P_RD}, {}, 1, 0},
    [LTA3] = {{P_RD}, {}, 1, 0},
    [MOV3] = {{P_RD}, {P_RS1}, 1, 1},
    [NEG3] = {{P_RD}, {P_RS1}, 1, 1},
    [NOT3] = {{P_RD}, {P_RS1}, 1, 1},
    [BNOT3] = {{P_RD}, {P_RS1}, 1, 1},
    [ADDI3] = {{P_RD}, {P_RS1}, 1, 1},
    [LEA3] = {{P_RD}, {}, 1, 0},

    // Sign/zero extend: rd = sext/zext(rs)
    [SX1] = {{P_RD}, {P_RS1}, 1, 1},
    [SX2] = {{P_RD}, {P_RS1}, 1, 1},
    [SX4] = {{P_RD}, {P_RS1}, 1, 1},
    [ZX1] = {{P_RD}, {P_RS1}, 1, 1},
    [ZX2] = {{P_RD}, {P_RS1}, 1, 1},
    [ZX4] = {{P_RD}, {P_RS1}, 1, 1},

    // Loads: rd = *(T*)regs[rs]
    [LDR_B] = {{P_RD}, {P_RS1}, 1, 1},
    [LDR_H] = {{P_RD}, {P_RS1}, 1, 1},
    [LDR_W] = {{P_RD}, {P_RS1}, 1, 1},
    [LDR_D] = {{P_RD}, {P_RS1}, 1, 1},

    // Stores: *(T*)regs[rs] = regs[rd] -- both rd and rs are uses
    [STR_B] = {{}, {P_RD, P_RS1}, 0, 2},
    [STR_H] = {{}, {P_RD, P_RS1}, 0, 2},
    [STR_W] = {{}, {P_RD, P_RS1}, 0, 2},
    [STR_D] = {{}, {P_RD, P_RS1}, 0, 2},

    // Float loads/stores
    [FLDR] = {{P_RD}, {P_RS1}, 1, 1},
    [FSTR] = {{}, {P_RD, P_RS1}, 0, 2},
    [FLDR_F32] = {{P_RD}, {P_RS1}, 1, 1},
    [FSTR_F32] = {{}, {P_RD, P_RS1}, 0, 2},

    // Float binary: fregs[rd] = fregs[rs1] (op) fregs[rs2]
    [FADD3] = {{P_RD}, {P_RS1, P_RS2}, 1, 2},
    [FSUB3] = {{P_RD}, {P_RS1, P_RS2}, 1, 2},
    [FMUL3] = {{P_RD}, {P_RS1, P_RS2}, 1, 2},
    [FDIV3] = {{P_RD}, {P_RS1, P_RS2}, 1, 2},
    [FMOV3] = {{P_RD}, {P_RS1}, 1, 1},
    [FNEG3] = {{P_RD}, {P_RS1}, 1, 1},
    [FADD3_F32] = {{P_RD}, {P_RS1, P_RS2}, 1, 2},
    [FSUB3_F32] = {{P_RD}, {P_RS1, P_RS2}, 1, 2},
    [FMUL3_F32] = {{P_RD}, {P_RS1, P_RS2}, 1, 2},
    [FDIV3_F32] = {{P_RD}, {P_RS1, P_RS2}, 1, 2},
    [FNEG3_F32] = {{P_RD}, {P_RS1}, 1, 1},

    // Float comparisons
    [FEQ3] = {{P_RD}, {P_RS1, P_RS2}, 1, 2},
    [FNE3] = {{P_RD}, {P_RS1, P_RS2}, 1, 2},
    [FLT3] = {{P_RD}, {P_RS1, P_RS2}, 1, 2},
    [FLE3] = {{P_RD}, {P_RS1, P_RS2}, 1, 2},
    [FGT3] = {{P_RD}, {P_RS1, P_RS2}, 1, 2},
    [FGE3] = {{P_RD}, {P_RS1, P_RS2}, 1, 2},
    [FEQ3_F32] = {{P_RD}, {P_RS1, P_RS2}, 1, 2},
    [FNE3_F32] = {{P_RD}, {P_RS1, P_RS2}, 1, 2},
    [FLT3_F32] = {{P_RD}, {P_RS1, P_RS2}, 1, 2},
    [FLE3_F32] = {{P_RD}, {P_RS1, P_RS2}, 1, 2},
    [FGT3_F32] = {{P_RD}, {P_RS1, P_RS2}, 1, 2},
    [FGE3_F32] = {{P_RD}, {P_RS1, P_RS2}, 1, 2},

    // Int<->float
    [I2F3] = {{P_RD}, {P_RS1}, 1, 1},
    [F2I3] = {{P_RD}, {P_RS1}, 1, 1},
    [I2F3_F32] = {{P_RD}, {P_RS1}, 1, 1},
    [F2I3_F32] = {{P_RD}, {P_RS1}, 1, 1},
    [FR2R] = {{P_RD}, {P_RS1}, 1, 1},
    [R2FR] = {{P_RD}, {P_RS1}, 1, 1},
    [FR2R_F32] = {{P_RD}, {P_RS1}, 1, 1},
    [R2FR_F32] = {{P_RD}, {P_RS1}, 1, 1},

    // Stack: PSH3 uses rs, POP3 defs rd
    [PSH3] = {{}, {P_RS1}, 0, 1},
    [POP3] = {{P_RD}, {}, 1, 0},

    // Conditional branches
    [JZ3] = {{}, {P_RS1}, 0, 1},
    [JNZ3] = {{}, {P_RS1}, 0, 1},
};

static inline bool fusion_is_caller_saved(int reg) {
    if (reg >= REG_T0 && reg <= REG_T4) return true;
    if (reg >= REG_T5 && reg <= REG_T10) return true;
    if (reg >= REG_A0 && reg <= REG_A7) return true;
    return false;
}

static int fusion_extract_reg(CCCCInstrWord operands_word, int byte_pos) {
    if (byte_pos < 0) return INVALID_REG;
    return (int)((operands_word >> (byte_pos * 8)) & 0xFF);
}

typedef struct {
    int pc;
    int def_op;
    int def_size;
    int use_count;
} DefState;

typedef struct {
    int def_op;
    int def_pc;
    int def_size;
    int use_op;
    int use_pc;
    int reg;
    int def_rd;
    int use_byte;
} Candidate;

typedef struct {
    Candidate *items;
    int count;
    int capacity;
} CandidateList;

typedef struct CcFusionState {
    CcAnalyzeFusionOptions opts;
    CandidateList list;
    int files_loaded;
} CcFusionState;

static bool fusion_is_killing_op(int op) {
    switch (op) {
    case JMP:
    case JZ3:
    case JNZ3:
    case JMPT:
    case JMPI:
    case CALL:
    case CALLT:
    case CALLI:
    case CALLN:
    case CALLF:
    case ENT3:
    case LEV3:
    case SETJMP:
    case LONGJMP:
        return true;
    default:
        return false;
    }
}

static void fusion_cand_push(CandidateList *list, Candidate c) {
    if (list->count == list->capacity) {
        int new_cap = list->capacity ? list->capacity * 2 : 64;
        Candidate *p =
            (Candidate *)realloc(list->items, (size_t)new_cap * sizeof(Candidate));
        if (!p) {
            fprintf(stderr, "cccc: out of memory\n");
            exit(1);
        }
        list->items = p;
        list->capacity = new_cap;
    }
    list->items[list->count++] = c;
}

static int fusion_cand_cmp(const void *a, const void *b) {
    const Candidate *ca = (const Candidate *)a;
    const Candidate *cb = (const Candidate *)b;
    if (ca->def_op != cb->def_op) return ca->def_op - cb->def_op;
    if (ca->use_op != cb->use_op) return ca->use_op - cb->use_op;
    if (ca->def_pc != cb->def_pc) return ca->def_pc - cb->def_pc;
    return ca->use_pc - cb->use_pc;
}

static void fusion_scan_text(CcFusionState *st, const CCCCInstrWord *text,
                              long long num_words) {
    DefState defs[NUM_REGS];
    for (int i = 0; i < NUM_REGS; i++)
        defs[i].pc = -1;

    long long pc = 1;  // text[0] is entry point
    while (pc < num_words) {
        int op = (int)text[pc];
        int size = cc_instr_words(op);
        if (size <= 0) break;

        if (fusion_is_killing_op(op)) {
            // Invalidate all def state. For CALL/CALLF also keep
            // callee-saved registers live (they survive the call).
            for (int i = 0; i < NUM_REGS; i++) {
                if ((op == CALL || op == CALLT || op == CALLI || op == CALLN || op == CALLF) &&
                    !fusion_is_caller_saved(i))
                    continue;
                defs[i].pc = -1;
            }
            DefUseEntry info = defuse_table[op];
            CCCCInstrWord op_word = (size > 1) ? text[pc + 1] : 0;
            for (int i = 0; i < info.n_defs; i++) {
                int r = fusion_extract_reg(op_word, info.def_pos[i]);
                if (r >= 0 && r < NUM_REGS)
                    defs[r].pc = -1;
            }
            pc += size;
            continue;
        }

        const DefUseEntry *info_p = &defuse_table[op];
        DefUseEntry info = *info_p;
        CCCCInstrWord op_word = (size > 1) ? text[pc + 1] : 0;

        // Phase 1: process uses -- detect adjacent def->use
        for (int i = 0; i < info.n_uses; i++) {
            int r = fusion_extract_reg(op_word, info.use_pos[i]);
            if (r <= 0 || r >= NUM_REGS) continue;  // skip REG_ZERO
            DefState *ds = &defs[r];
            if (ds->pc >= 0) {
                if (ds->use_count == 0 &&
                    ds->pc + ds->def_size == (int)pc) {
                    Candidate c = {
                        .def_op = ds->def_op,
                        .def_pc = ds->pc,
                        .def_size = ds->def_size,
                        .use_op = op,
                        .use_pc = (int)pc,
                        .reg = r,
                        .def_rd = r,
                        .use_byte = info.use_pos[i],
                    };
                    fusion_cand_push(&st->list, c);
                }
                ds->use_count++;
            }
        }

        // Phase 2: process defs -- kill prior def state
        for (int i = 0; i < info.n_defs; i++) {
            int r = fusion_extract_reg(op_word, info.def_pos[i]);
            if (r < 0 || r >= NUM_REGS) continue;
            defs[r].pc = (int)pc;
            defs[r].def_op = op;
            defs[r].def_size = size;
            defs[r].use_count = 0;
        }

        pc += size;
    }
}

static void fusion_print_text(FILE *f, const CandidateList *list, int show) {
    fprintf(f, "=== Fusion candidates: adjacent def->use pairs ===\n");
    fprintf(f, "  total:           %d\n", list->count);
    fprintf(f, "  showing:         %d\n", show);
    fprintf(f, "  top %d:\n", show);
    for (int i = 0; i < show; i++) {
        const Candidate *c = &list->items[i];
        fprintf(f, "  pc=%-5d  %-12s pc=%-5d  %-12s  reg=r%d  use_byte=%d\n",
                c->def_pc, safe_opcode_name(c->def_op),
                c->use_pc, safe_opcode_name(c->use_op),
                c->reg, c->use_byte);
    }
}

static void fusion_print_json(FILE *f, const CandidateList *list, int show) {
    fprintf(f, "{\n  \"tool\": \"cccc-fusion-candidates\",\n");
    fprintf(f, "  \"candidates\": [\n");
    for (int i = 0; i < show; i++) {
        const Candidate *c = &list->items[i];
        fprintf(f, "%s    {\"def_op\": \"%s\", \"def_pc\": %d, "
                   "\"use_op\": \"%s\", \"use_pc\": %d, "
                   "\"reg\": %d}",
                   i ? ",\n" : "",
                   safe_opcode_name(c->def_op), c->def_pc,
                   safe_opcode_name(c->use_op), c->use_pc,
                   c->reg);
    }
    fprintf(f, "%s\n  ],\n  \"total_candidates\": %d\n}\n",
            show ? "\n" : "", list->count);
}

CcFusionState *cc_analyze_fusion_begin(const CcAnalyzeFusionOptions *opts) {
    CcFusionState *st = (CcFusionState *)calloc(1, sizeof(CcFusionState));
    if (!st) {
        fprintf(stderr, "cccc: out of memory\n");
        exit(1);
    }
    st->opts = *opts;
    return st;
}

void cc_analyze_fusion_feed(CcFusionState *st, const CCCCInstrWord *text,
                            long long num_words, const char *label,
                            FILE *out) {
    (void)label;
    (void)out;
    if (!st || !text || num_words <= 1)
        return;
    fusion_scan_text(st, text, num_words);
    st->files_loaded++;
}

void cc_analyze_fusion_finish(CcFusionState *st, FILE *out) {
    if (!st)
        return;
    if (st->files_loaded == 0) {
        fprintf(stderr, "cccc: no input files loaded\n");
        free(st->list.items);
        free(st);
        return;
    }
    qsort(st->list.items, (size_t)st->list.count, sizeof(Candidate),
          fusion_cand_cmp);
    int show = st->list.count < st->opts.top_n ? st->list.count
                                                : st->opts.top_n;
    if (st->opts.json) {
        fusion_print_json(out, &st->list, show);
    } else {
        fusion_print_text(out, &st->list, show);
    }
    free(st->list.items);
    free(st);
}
