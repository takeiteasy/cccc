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

 tools/fusion_candidates.c - Use-def fusion candidate detector for .jbc files.

 Walks the text segment of one or more .jbc files, tracks register def/use
 per instruction, and reports adjacent def->use pairs where the defining
 instruction has a single reader. These are the strongest candidates for
 opcode fusion (see ticket #250).

 The tool is a *reporter*; it does not modify the bytecode. A future
 optimizer pass could use the same def/use machinery to perform the
 actual rewrites.

 Usage:
   tools/fusion_candidates [options] file.jbc [file2.jbc ...]

 Options:
   -t N     Show top N candidates (default 50)
   -j       Output JSON instead of text
   -h       Show this help

 Notes on conservativeness:
   - After CALL/CALLF, all caller-saved register defs are invalidated
     (T0-T10, A0-A7).
   - On control-flow joins (JMP, JZ3, JNZ3, JMPT, JMPI, LTA3, function
     entry) all def state is invalidated since we cannot statically
     know the predecessor.
   - The whole text segment is walked as a single linear block; real
     function boundaries are not modelled. This may slightly under-
     report candidates that cross CALL boundaries safely.
*/

#include "jcc.h"
#include "internal.h"

#include <getopt.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define INVALID_REG (-1)

typedef struct {
    // byte positions within the first operand word where the
    // defined/used register number lives; -1 in unused slots.
    // For RRR: bytes 0, 1, 2 = rd, rs1, rs2.
    // For RR:  bytes 0, 1    = rd, rs1.
    // For RI:  byte  0       = rd.
    int8_t def_pos[3];
    int8_t use_pos[3];
    uint8_t n_defs;
    uint8_t n_uses;
} DefUseEntry;

// Def/use table indexed by opcode. Opcodes not listed here are treated
// as having no def/use, which simply means they don't participate in
// fusion detection. This covers the RRR/RR/RI encodings that dominate
// the patterns from ticket #250.
//
// Conventions:
//   - 1 = byte 0 of first operand word
//   - 2 = byte 1 of first operand word
//   - 3 = byte 2 of first operand word
//
// (Symbolic constants for readability.)
#define P_RD 0
#define P_RS1 1
#define P_RS2 2

static const DefUseEntry defuse_table[OP_COUNT] = {
    // Register arithmetic: rd = f(rs1, rs2)
    [ADD3] = {{P_RD}, {P_RS1, P_RS2}, 1, 2},
    [SUB3] = {{P_RD}, {P_RS1, P_RS2}, 1, 2},
    [MUL3] = {{P_RD}, {P_RS1, P_RS2}, 1, 2},
    [DIV3] = {{P_RD}, {P_RS1, P_RS2}, 1, 2},
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

    // Float loads/stores: fregs[rd] = *(T*)regs[rs] (FLDR) and reverse (FSTR)
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

    // Float comparisons: int rd = fregs[rs1] (op) fregs[rs2]
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

    // Int<->float: rd and rs in int register file
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

    // Conditional branches: use rs
    [JZ3] = {{}, {P_RS1}, 0, 1},
    [JNZ3] = {{}, {P_RS1}, 0, 1},
};

// Caller-saved registers: T0-T4, T5-T10, A0-A7. After CALL/CALLF their
// def state is invalidated. (Callee-saved S0-S7 and the implicit BP are
// preserved across calls.)
static inline bool is_caller_saved(int reg) {
    if (reg >= REG_T0 && reg <= REG_T4) return true;
    if (reg >= REG_T5 && reg <= REG_T10) return true;
    if (reg >= REG_A0 && reg <= REG_A7) return true;
    return false;
}

static int extract_reg(JCCInstrWord operands_word, int byte_pos) {
    if (byte_pos < 0) return INVALID_REG;
    return (int)((operands_word >> (byte_pos * 8)) & 0xFF);
}

typedef struct {
    int pc;             // pc of the defining instruction
    int def_op;         // opcode of the definer
    int def_size;       // word size of the definer
    int use_count;      // number of reads since this def
} DefState;

typedef struct {
    int def_op;
    int def_pc;
    int def_size;
    int use_op;
    int use_pc;
    int reg;
    int def_rd;         // for reporting: which reg the def defined
    int use_byte;       // which operand position consumed the def
} Candidate;

typedef struct {
    Candidate *items;
    int count;
    int capacity;
} CandidateList;

static void cand_push(CandidateList *list, Candidate c) {
    if (list->count == list->capacity) {
        int new_cap = list->capacity ? list->capacity * 2 : 64;
        Candidate *p = realloc(list->items, (size_t)new_cap * sizeof(Candidate));
        if (!p) {
            fprintf(stderr, "fusion_candidates: out of memory\n");
            exit(1);
        }
        list->items = p;
        list->capacity = new_cap;
    }
    list->items[list->count++] = c;
}

static int cand_cmp(const void *a, const void *b) {
    const Candidate *ca = a, *cb = b;
    // Sort by sequence (def_op, use_op) then by pc
    if (ca->def_op != cb->def_op) return ca->def_op - cb->def_op;
    if (ca->use_op != cb->use_op) return ca->use_op - cb->use_op;
    if (ca->def_pc != cb->def_pc) return ca->def_pc - cb->def_pc;
    return ca->use_pc - cb->use_pc;
}

// Read the .jbc header and return the text segment.
// Identical layout to tools/bytecode_ngrams.c; kept separate so each tool
// is self-contained and can be reasoned about in isolation.
static int load_text_segment(const char *path, JCCInstrWord **out_text,
                             long long *out_num_words) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "fusion_candidates: cannot open %s: %s\n", path,
                strerror(errno));
        return -1;
    }
    char magic[4];
    if (fread(magic, 1, 4, f) != 4 || memcmp(magic, JCC_MAGIC, 4) != 0) {
        fprintf(stderr, "fusion_candidates: %s: not a JCC bytecode file\n",
                path);
        fclose(f);
        return -1;
    }
    int version = 0;
    if (fread(&version, sizeof(int), 1, f) != 1 ||
        version != JCC_VERSION) {
        fprintf(stderr, "fusion_candidates: %s: unsupported version %d\n",
                path, version);
        fclose(f);
        return -1;
    }
    uint32_t flags_unused = 0;
    if (fread(&flags_unused, sizeof(uint32_t), 1, f) != 1) {
        fclose(f);
        return -1;
    }
    long long text_size = 0;
    if (fread(&text_size, sizeof(long long), 1, f) != 1 ||
        text_size <= 0 || text_size % (long long)sizeof(JCCInstrWord) != 0) {
        fclose(f);
        return -1;
    }
    long long num_words = text_size / (long long)sizeof(JCCInstrWord);
    JCCInstrWord *text = malloc((size_t)text_size);
    if (!text) {
        fclose(f);
        return -1;
    }
    if (fread(text, 1, (size_t)text_size, f) != (size_t)text_size) {
        free(text);
        fclose(f);
        return -1;
    }
    fclose(f);
    *out_text = text;
    *out_num_words = num_words;
    return 0;
}

static const char *safe_name(int op) {
    const char *s = cc_opcode_name(op);
    return s ? s : "OP_?";
}

static bool is_killing_op(int op) {
    // Opcodes that invalidate def state because we can't track
    // predecessor state across them safely.
    switch (op) {
    case JMP:
    case JZ3:
    case JNZ3:
    case JMPT:
    case JMPI:
    case CALL:
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

static int scan_file(const char *path, CandidateList *out) {
    JCCInstrWord *text = NULL;
    long long num_words = 0;
    if (load_text_segment(path, &text, &num_words) != 0)
        return -1;

    DefState defs[NUM_REGS];
    for (int i = 0; i < NUM_REGS; i++)
        defs[i].pc = -1;

    long long pc = 1;  // text_seg[0] is entry point
    while (pc < num_words) {
        int op = (int)text[pc];
        int size = cc_instr_words(op);
        if (size <= 0) break;

        if (is_killing_op(op)) {
            // Invalidate all def state. For CALL/CALLF also keep
            // callee-saved registers live (they survive the call).
            for (int i = 0; i < NUM_REGS; i++) {
                if ((op == CALL || op == CALLI || op == CALLN || op == CALLF) &&
                    !is_caller_saved(i))
                    continue;
                defs[i].pc = -1;
            }
            // Self-def/use: e.g. ADJ has no def/use, but if defuse_table
            // has a def for this op (it doesn't for killing ops), process it.
            DefUseEntry info = defuse_table[op];
            JCCInstrWord op_word = (size > 1) ? text[pc + 1] : 0;
            for (int i = 0; i < info.n_defs; i++) {
                int r = extract_reg(op_word, info.def_pos[i]);
                if (r >= 0 && r < NUM_REGS)
                    defs[r].pc = -1;
            }
            pc += size;
            continue;
        }

        const DefUseEntry *info_p = &defuse_table[op];
        DefUseEntry info = *info_p;
        JCCInstrWord op_word = (size > 1) ? text[pc + 1] : 0;

        // Phase 1: process uses -- detect adjacent def->use
        for (int i = 0; i < info.n_uses; i++) {
            int r = extract_reg(op_word, info.use_pos[i]);
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
                    cand_push(out, c);
                }
                ds->use_count++;
            }
        }

        // Phase 2: process defs -- kill prior def state
        for (int i = 0; i < info.n_defs; i++) {
            int r = extract_reg(op_word, info.def_pos[i]);
            if (r < 0 || r >= NUM_REGS) continue;
            defs[r].pc = (int)pc;
            defs[r].def_op = op;
            defs[r].def_size = size;
            defs[r].use_count = 0;
        }

        pc += size;
    }

    free(text);
    return 0;
}

static void print_candidate(FILE *f, const Candidate *c) {
    fprintf(f, "  pc=%-5d  %-12s pc=%-5d  %-12s  reg=r%d  use_byte=%d\n",
            c->def_pc, safe_name(c->def_op),
            c->use_pc, safe_name(c->use_op),
            c->reg, c->use_byte);
}

static int count_key(const void *a, const void *b) {
    (void)a; (void)b;
    return 0;
}

static void usage(FILE *f) {
    fprintf(f,
            "Usage: fusion_candidates [options] file.jbc [file.jbc ...]\n"
            "\n"
            "Use-def fusion candidate detector for JCC .jbc files.\n"
            "Reports adjacent def->use pairs where the defining\n"
            "instruction has a single reader -- prime opcode fusion\n"
            "candidates (see ticket #250).\n"
            "\n"
            "Options:\n"
            "  -t N    Show top N candidates (default 50)\n"
            "  -j      Output JSON instead of text\n"
            "  -h      Show this help\n");
}

int main(int argc, char **argv) {
    const char *progname = argv[0];
    int top_n = 50;
    int json_mode = 0;
    int opt;
    while ((opt = getopt(argc, argv, "t:jh")) != -1) {
        switch (opt) {
        case 't':
            top_n = atoi(optarg);
            if (top_n <= 0) {
                fprintf(stderr, "%s: -t must be > 0\n", progname);
                return 1;
            }
            break;
        case 'j':
            json_mode = 1;
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

    CandidateList list = {0};
    int files_loaded = 0;
    for (int i = optind; i < argc; i++) {
        if (scan_file(argv[i], &list) == 0)
            files_loaded++;
    }
    if (files_loaded == 0) {
        fprintf(stderr, "%s: no input files loaded\n", progname);
        free(list.items);
        return 1;
    }

    qsort(list.items, (size_t)list.count, sizeof(Candidate), cand_cmp);

    int show = list.count < top_n ? list.count : top_n;
    if (json_mode) {
        printf("{\n  \"tool\": \"jcc-fusion-candidates\",\n");
        printf("  \"candidates\": [\n");
        for (int i = 0; i < show; i++) {
            const Candidate *c = &list.items[i];
            printf("%s    {\"def_op\": \"%s\", \"def_pc\": %d, "
                   "\"use_op\": \"%s\", \"use_pc\": %d, "
                   "\"reg\": %d}",
                   i ? ",\n" : "",
                   safe_name(c->def_op), c->def_pc,
                   safe_name(c->use_op), c->use_pc,
                   c->reg);
        }
        printf("%s\n  ],\n  \"total_candidates\": %d\n}\n",
               show ? "\n" : "", list.count);
    } else {
        printf("=== Fusion candidates: adjacent def->use pairs ===\n");
        printf("  files:           %d\n", files_loaded);
        printf("  total:           %d\n", list.count);
        printf("  showing:         %d\n", show);
        printf("  top %d:\n", show);
        for (int i = 0; i < show; i++)
            print_candidate(stdout, &list.items[i]);
    }

    free(list.items);
    return 0;
    (void)count_key;
}
