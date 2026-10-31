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

#include "./codegen_internal.h"

// Per-function bytecode clean-up, run on a function's text window right after
// its last instruction is emitted: copy propagation, dead definitions,
// retargeting `OP t; MOV x,t`, redundant sign/zero extensions and jumps to the
// next instruction. Only opcodes listed in op_spec() are understood; any other
// opcode is opaque: it reads every register, ends every fact about register
// contents and is never removed, so a new opcode is never mis-optimised.
//
// Extension facts and value intervals flow across the block graph (meet =
// intersection / hull over predecessors, widened on a backward edge); copy
// facts and the latest compare stay inside a block. A conditional branch on a
// compare narrows the operands' intervals along each of its edges. A call keeps
// what is known about promoted registers: the callee restores them. A block
// that can be entered from outside the graph (the function entry, an
// address-taken label) starts from the promoted-register seed alone. A switch
// table's targets are successors of its JMPT; every label of a function with a
// computed goto is a successor of its JMPI.
//
// An operation whose operands all hold a single known value is replaced by an
// instruction of the same width: a 4-word one (ADDI3, the shifts and AND with
// an immediate, the compares against an immediate, ...) by `LI3 rd, result`, a
// 2-word one only when the result is 0
// (`MOV3 rd, r0`). An operand that is the identity (`x + 0`, `x * 1`, `x & -1`)
// makes it a copy of the other operand, and a `MULADDI3` with a known index an
// `ADDI3`. A register loaded with 0 is read as the zero register afterwards.
//
// TODO: an extension after arithmetic is kept when it needs a relation between
// two registers rather than an interval
// (https://todo.sr.ht/~takeiteasy/cccc/1892), or follows a call result
// (https://todo.sr.ht/~takeiteasy/cccc/1893). A 2-word operation with a
// non-zero constant result is kept, and so is its LI3 operand
// (https://todo.sr.ht/~takeiteasy/cccc/1894).

enum { R_NO, R_IU, R_ID, R_FU, R_FD };
enum { SP_PURE = 1, SP_MOVE = 2 };

typedef struct {
    uint8_t role[4];
    uint8_t flags;
} OpSpec;

#define SPEC(r0, r1, r2, r3, fl)                                               \
    (OpSpec) {                                                                 \
        {R_##r0, R_##r1, R_##r2, R_##r3}, (fl)                                 \
    }

// What each byte of an instruction's first operand word means.
// PURE: the instruction has no effect beyond its register definitions.
static bool op_spec(int op, OpSpec *s) {
    switch (op) {
        case ADD3:
        case SUB3:
        case MUL3:
        case AND3:
        case OR3:
        case XOR3:
        case SHL3:
        case SHR3:
        case USHR3:
        case SEQ3:
        case SNE3:
        case SLT3:
        case SGE3:
        case SGT3:
        case SLE3:
        case ULT3:
        case ULE3:
            *s = SPEC(ID, IU, IU, NO, SP_PURE);
            return true;
        case DIV3:
        case MOD3:
        case UDIV3:
        case UMOD3:
            *s = SPEC(ID, IU, IU, NO, 0);
            return true;
        case MOV3:
            *s = SPEC(ID, IU, NO, NO, SP_PURE | SP_MOVE);
            return true;
        case NEG3:
        case NOT3:
        case BNOT3:
        case SX1:
        case SX2:
        case SX4:
        case ZX1:
        case ZX2:
        case ZX4:
        case ADDI3:
        case MULI3:
        case SHLI3:
        case SHRI3:
        case USHRI3:
        case ANDI3:
        case SEQI3:
        case SNEI3:
        case SLTI3:
        case SLEI3:
        case SGTI3:
        case SGEI3:
        case ULTI3:
        case ULEI3:
        case UGTI3:
        case UGEI3:
            *s = SPEC(ID, IU, NO, NO, SP_PURE);
            return true;
        case MULADD3:
            *s = SPEC(ID, IU, IU, IU, SP_PURE);
            return true;
        case MULADDI3:
            *s = SPEC(ID, IU, IU, NO, SP_PURE);
            return true;
        case LI3:
        case LDA3:
            *s = SPEC(ID, NO, NO, NO, SP_PURE);
            return true;
        case LEA3:
        case LDR_LOCAL_B:
        case LDR_LOCAL_H:
        case LDR_LOCAL_W:
        case LDR_LOCAL_D:
            *s = SPEC(ID, NO, NO, NO, 0);
            return true;
        case STR_LOCAL_B:
        case STR_LOCAL_H:
        case STR_LOCAL_W:
        case STR_LOCAL_D:
            *s = SPEC(IU, NO, NO, NO, 0);
            return true;
        case LDR_B:
        case LDR_W:
        case LDR_D:
            *s = SPEC(ID, IU, NO, NO, 0);
            return true;
        case STR_B:
        case STR_H:
        case STR_W:
        case STR_D:
            *s = SPEC(IU, IU, NO, NO, 0);
            return true;
        case LDR_INDEX_B:
        case LDR_INDEX_H:
        case LDR_INDEX_W:
        case LDR_INDEX_D:
            *s = SPEC(ID, IU, IU, NO, 0);
            return true;
        case STR_INDEX_B:
        case STR_INDEX_H:
        case STR_INDEX_W:
        case STR_INDEX_D:
            *s = SPEC(IU, IU, IU, NO, 0);
            return true;
        case FMOV3:
            *s = SPEC(FD, FU, NO, NO, SP_PURE | SP_MOVE);
            return true;
        case FNEG3:
        case FNEG3_F32:
            *s = SPEC(FD, FU, NO, NO, SP_PURE);
            return true;
        case FADD3:
        case FSUB3:
        case FMUL3:
        case FDIV3:
        case FADD3_F32:
        case FSUB3_F32:
        case FMUL3_F32:
        case FDIV3_F32:
            *s = SPEC(FD, FU, FU, NO, 0);
            return true;
        case FMADD3:
        case FMADD3_F32:
        case FMSUB3:
        case FMSUB3_F32:
        case FNMSUB3:
        case FNMSUB3_F32:
            *s = SPEC(FD, FU, FU, FU, 0);
            return true;
        case FEQ3:
        case FNE3:
        case FLT3:
        case FLE3:
        case FGT3:
        case FGE3:
        case FEQ3_F32:
        case FNE3_F32:
        case FLT3_F32:
        case FLE3_F32:
        case FGT3_F32:
        case FGE3_F32:
            *s = SPEC(ID, FU, FU, NO, 0);
            return true;
        case I2F3:
        case I2F3_F32:
            *s = SPEC(FD, IU, NO, NO, 0);
            return true;
        case F2I3:
        case F2I3_F32:
        case FNZ3:
        case FNZ3_F32:
            *s = SPEC(ID, FU, NO, NO, 0);
            return true;
        case FLDR:
        case FLDR_F32:
            *s = SPEC(FD, IU, NO, NO, 0);
            return true;
        case FSTR:
        case FSTR_F32:
            *s = SPEC(FU, IU, NO, NO, 0);
            return true;
        case FLDR_LOCAL:
        case FLDR_LOCAL_F32:
            *s = SPEC(FD, NO, NO, NO, 0);
            return true;
        case FSTR_LOCAL:
        case FSTR_LOCAL_F32:
            *s = SPEC(FU, NO, NO, NO, 0);
            return true;
        case FLDR_INDEX:
        case FLDR_INDEX_F32:
            *s = SPEC(FD, IU, IU, NO, 0);
            return true;
        case FSTR_INDEX:
        case FSTR_INDEX_F32:
            *s = SPEC(FU, IU, IU, NO, 0);
            return true;
        default:
            return false;
    }
}

#define ALL_REGS 0xFFFFFFFFu
// A0-A7 and S0-S7: what a return or tail call hands on. The temporaries are
// the caller-saved ones.
#define EXIT_LIVE 0x03FFFC00u
#define BIT(r)    ((uint32_t)1 << (r))

typedef struct {
    uint32_t iuse, idef, fuse, fdef;
} Eff;

// A compare whose result register has not changed since, nor its operands:
// `dst = a OP b` (`b < 0`: `a OP imm`).
typedef struct {
    int       op, dst, a, b;
    long long imm;
    bool      valid;
} Cmp;

// What is known about the registers at one point: icopy[r] / fcopy[r] is a
// register r currently equals, ext[r] the extensions r already satisfies,
// [lo[r], hi[r]] an interval holding it (ext[r] and the interval imply each
// other), cmp the latest compare.
typedef struct {
    int       icopy[NUM_REGS];
    int       fcopy[NUM_REGS];
    uint8_t   ext[NUM_REGS];
    long long lo[NUM_REGS], hi[NUM_REGS];
    Cmp       cmp;
} Facts;

typedef struct {
    Pc      pc;
    int     op;
    int     size;
    bool    dead;
    bool    leader;
    bool    addr_target; // a label whose address may be jumped to
    bool    known;   // op_spec() applies and every register byte is in range
    uint8_t src_ext; // an extension's source facts, set by the forward pass
    OpSpec  spec;
} Insn;

typedef struct {
    int      first, last; // instruction indices, [first, last)
    int      eoff, nsucc; // successors are Peep.edges[eoff, eoff + nsucc)
    bool     ext_entry;   // enterable from outside the block graph
    bool     reached;
    uint32_t iin, fin, iout, fout;
    Facts    in;
} Block;

typedef struct {
    VirtualMachine *vm;
    Obj            *fn;
    Pc              start, end;
    Insn           *ins;
    int             n;
    int            *at;     // word offset - start -> instruction index, or -1
    Block          *blocks;
    int            *blk_of; // instruction index -> block index
    int             nblocks;
    int            *edges;  // successor block indices, grouped by block
    int             nedges, edge_cap;
    int            *labels; // blocks starting at an address-taken label
    int             nlabels;
    bool            changed;
    uint8_t         seed[NUM_REGS]; // extension facts that hold throughout
    uint32_t        promoted;       // registers holding promoted locals
} Peep;

enum { C_NONE, C_JMP, C_COND, C_EXIT };

static int ctrl_kind(const Insn *in) {
    if (in->dead)
        return C_NONE;
    switch (in->op) {
        case JMP:
            return C_JMP;
        case JZ3:
        case JNZ3:
            return C_COND;
        case JMPT:
        case LEV3:
        case CALLT:
        case JMPI:
        case LONGJMP:
            return C_EXIT;
        default:
            return C_NONE;
    }
}

static InstrWord operand(Peep *p, const Insn *in) {
    return p->vm->text_seg[in->pc + 1];
}

static void effects(Peep *p, const Insn *in, Eff *e) {
    *e = (Eff){0};
    if (in->op == JMP)
        return;
    InstrWord w = operand(p, in);
    if (in->op == JZ3 || in->op == JNZ3) {
        int r = w & 0xFF;
        if (r > REG_ZERO && r < NUM_REGS)
            e->iuse = BIT(r);
        else if (r >= NUM_REGS)
            e->iuse = ALL_REGS;
        return;
    }
    if (in->op == LEV3 || in->op == CALLT) {
        e->iuse = e->fuse = EXIT_LIVE;
        return;
    }
    if (in->op == JMPT) {
        e->iuse = BIT(REG_A0);
        return;
    }
    if (in->op == JMPI) {
        int r = w & 0xFF;
        if (r >= NUM_REGS)
            e->iuse = ALL_REGS;
        else if (r != REG_ZERO)
            e->iuse = BIT(r);
        return;
    }
    if (!in->known) {
        e->iuse = e->fuse = ALL_REGS;
        return;
    }
    for (int k = 0; k < 4; k++) {
        int r = (w >> (8 * k)) & 0xFF;
        switch (in->spec.role[k]) {
            case R_IU:
                if (r != REG_ZERO)
                    e->iuse |= BIT(r);
                break;
            case R_ID:
                if (r != REG_ZERO)
                    e->idef |= BIT(r);
                break;
            case R_FU:
                e->fuse |= BIT(r);
                break;
            case R_FD:
                e->fdef |= BIT(r);
                break;
        }
    }
}

// ---- Decoding ----

static bool in_window(const Peep *p, Pc t) {
    return t >= p->start && t < p->end;
}

// Records `t` as a branch target. False when it points inside an instruction.
static bool mark_target(Peep *p, Pc t) {
    if (!in_window(p, t))
        return true;
    int i = p->at[t - p->start];
    if (i < 0)
        return false;
    p->ins[i].leader = true;
    return true;
}

static bool decode_window(Peep *p) {
    VirtualMachine *vm  = p->vm;
    size_t          cap = (size_t)(p->end - p->start) + 1;
    p->ins              = calloc(cap, sizeof(Insn));
    p->at               = malloc(cap * sizeof(int));
    if (!p->ins || !p->at)
        error("out of memory");
    for (size_t i = 0; i < cap; i++)
        p->at[i] = -1;

    Pc pc = p->start;
    while (pc < p->end) {
        int op   = (int)vm->text_seg[pc];
        int size = cc_instr_words(op);
        if (size <= 0)
            return false;
        if (op == JMPT) {
            if (pc + 3 >= p->end || vm->text_seg[pc + 1] != pc + 4)
                return false;
            size += (int)vm->text_seg[pc + 2];
        }
        if (pc + (Pc)size > p->end)
            return false;
        Insn *in = &p->ins[p->n];
        in->pc   = pc;
        in->op   = op;
        in->size = size;
        in->known =
            op != JMP && op != JZ3 && op != JNZ3 && op_spec(op, &in->spec);
        p->at[pc - p->start]  = p->n++;
        pc                   += (Pc)size;
    }
    if (pc != p->end)
        return false;

    for (int i = 0; i < p->n; i++) {
        Insn     *in = &p->ins[i];
        InstrWord w  = vm->text_seg[in->pc + 1];
        if (in->known) {
            for (int k = 0; k < 4; k++)
                if (in->spec.role[k] != R_NO &&
                    ((w >> (8 * k)) & 0xFF) >= NUM_REGS)
                    in->known = false;
        }
    }
    return true;
}

// Marks `t` as a label whose address may be jumped to.
static bool mark_address_target(Peep *p, Pc t) {
    if (!mark_target(p, t))
        return false;
    if (in_window(p, t))
        p->ins[p->at[t - p->start]].addr_target = true;
    return true;
}

// Marks every branch target as a leader. False if one is not an instruction.
// With a computed goto, every label of the function is a possible target.
static bool mark_leaders(Peep *p, PeepMark m) {
    VirtualMachine *vm       = p->vm;
    bool            has_jmpi = false;
    for (int i = 0; i < p->n; i++) {
        Insn *in = &p->ins[i];
        bool  ok = true;
        switch (in->op) {
            case JMPI:
                has_jmpi = true;
                break;
            case JMP:
                ok = mark_target(p, vm->text_seg[in->pc + 1]);
                break;
            case JZ3:
            case JNZ3:
                ok = mark_target(p, vm->text_seg[in->pc + 2]);
                break;
            case JMPT: {
                Pc        table = vm->text_seg[in->pc + 1];
                InstrWord count = vm->text_seg[in->pc + 2];
                ok              = mark_target(p, vm->text_seg[in->pc + 3]);
                for (InstrWord k = 0; ok && k < count; k++)
                    ok = mark_target(p, vm->text_seg[table + (Pc)k]);
                break;
            }
            case LTA3:
                ok = mark_address_target(
                    p, cc_byte_offset_to_pc(cc_read_i64_at(vm, in->pc + 2)));
                break;
        }
        if (!ok)
            return false;
    }
    if (has_jmpi)
        for (int i = m.labels; i < num_global_labels; i++)
            if (!mark_address_target(p, global_label_map[i].offset))
                return false;
    return true;
}

// ---- Control-flow graph and liveness ----

static int count_blocks(const Peep *p) {
    int  count = 0;
    bool open  = false;
    for (int i = 0; i < p->n; i++) {
        if (!open || p->ins[i].leader)
            count++;
        open = ctrl_kind(&p->ins[i]) == C_NONE;
    }
    return count;
}

static void build_blocks(Peep *p) {
    int cap   = count_blocks(p);
    p->blocks = calloc((size_t)cap + 1, sizeof(Block));
    p->labels = malloc(((size_t)cap + 1) * sizeof(int));
    int *bi = p->blk_of = malloc(((size_t)p->n + 1) * sizeof(int));
    if (!p->blocks || !p->labels || !bi)
        error("out of memory");
    p->nblocks = 0;
    bool open  = false;
    for (int i = 0; i < p->n; i++) {
        if (!open || p->ins[i].leader) {
            if (open)
                p->blocks[p->nblocks - 1].last = i;
            Block *blk     = &p->blocks[p->nblocks];
            blk->first     = i;
            blk->ext_entry = p->nblocks == 0 || p->ins[i].addr_target;
            p->nblocks++;
            open = true;
        }
        bi[i] = p->nblocks - 1;
        if (ctrl_kind(&p->ins[i]) != C_NONE) {
            p->blocks[p->nblocks - 1].last = i + 1;
            open                           = false;
        }
    }
    if (open)
        p->blocks[p->nblocks - 1].last = p->n;
}

static void add_edge(Peep *p, int b) {
    if (p->nedges == p->edge_cap) {
        p->edge_cap = p->edge_cap ? p->edge_cap * 2 : 64;
        p->edges    = realloc(p->edges, (size_t)p->edge_cap * sizeof(int));
        if (!p->edges)
            error("out of memory");
    }
    p->edges[p->nedges++] = b;
}

static void add_target(Peep *p, Pc t) {
    if (in_window(p, t))
        add_edge(p, p->blk_of[p->at[t - p->start]]);
}

static void compute_succs(Peep *p) {
    VirtualMachine *vm = p->vm;
    p->nedges          = 0;
    p->nlabels         = 0;
    for (int b = 0; b < p->nblocks; b++)
        if (p->ins[p->blocks[b].first].addr_target)
            p->labels[p->nlabels++] = b;
    for (int b = 0; b < p->nblocks; b++) {
        Block *blk       = &p->blocks[b];
        blk->eoff        = p->nedges;
        const Insn *last = &p->ins[blk->last - 1];
        int         kind = ctrl_kind(last);
        if ((kind == C_NONE || kind == C_COND) && b + 1 < p->nblocks)
            add_edge(p, b + 1);
        if (kind == C_JMP || kind == C_COND)
            add_target(p, vm->text_seg[last->pc + (kind == C_JMP ? 1 : 2)]);
        else if (last->op == JMPT && !last->dead) {
            add_target(p, vm->text_seg[last->pc + 3]);
            InstrWord count = vm->text_seg[last->pc + 2];
            for (InstrWord k = 0; k < count; k++)
                add_target(p, vm->text_seg[last->pc + 4 + (Pc)k]);
        } else if (last->op == JMPI && !last->dead) {
            for (int k = 0; k < p->nlabels; k++)
                add_edge(p, p->labels[k]);
        }
        blk->nsucc = p->nedges - blk->eoff;
    }
}

static void step_back(Peep *p, const Insn *in, uint32_t *il, uint32_t *fl) {
    Eff e;
    effects(p, in, &e);
    *il = (*il & ~e.idef) | e.iuse;
    *fl = (*fl & ~e.fdef) | e.fuse;
}

static void compute_liveness(Peep *p) {
    for (int b = 0; b < p->nblocks; b++)
        p->blocks[b].iin = p->blocks[b].fin = 0;
    bool again = true;
    while (again) {
        again = false;
        for (int b = p->nblocks - 1; b >= 0; b--) {
            Block   *blk = &p->blocks[b];
            uint32_t il = 0, fl = 0;
            for (int s = 0; s < blk->nsucc; s++) {
                const Block *sb  = &p->blocks[p->edges[blk->eoff + s]];
                il              |= sb->iin;
                fl              |= sb->fin;
            }
            blk->iout = il;
            blk->fout = fl;
            for (int i = blk->last - 1; i >= blk->first; i--)
                if (!p->ins[i].dead)
                    step_back(p, &p->ins[i], &il, &fl);
            if (il != blk->iin || fl != blk->fin) {
                blk->iin = il;
                blk->fin = fl;
                again    = true;
            }
        }
    }
}

// ---- Forward pass: copy propagation and redundant extensions ----

enum {
    X_SX1 = 1,
    X_SX2 = 2,
    X_SX4 = 4,
    X_ZX1 = 8,
    X_ZX2 = 16,
    X_ZX4 = 32,
    X_ALL = 63,
};

static const struct {
    int       bit;
    long long lo, hi;
} EXT_LIMITS[] = {
    {X_SX1, -128, 127}, {X_SX2, -32768, 32767}, {X_SX4, INT_MIN, INT_MAX},
    {X_ZX1, 0, 255},    {X_ZX2, 0, 65535},      {X_ZX4, 0, 4294967295LL},
};
#define N_EXT_LIMITS (int)(sizeof EXT_LIMITS / sizeof EXT_LIMITS[0])

// Extension facts a value in [lo, hi] satisfies.
static int ext_of_range(long long lo, long long hi) {
    int x = 0;
    for (int i = 0; i < N_EXT_LIMITS; i++)
        if (lo >= EXT_LIMITS[i].lo && hi <= EXT_LIMITS[i].hi)
            x |= EXT_LIMITS[i].bit;
    return x;
}

// The tightest interval the extension facts `x` confine a value to.
static void range_of_ext(int x, long long *lo, long long *hi) {
    *lo = LLONG_MIN;
    *hi = LLONG_MAX;
    for (int i = 0; i < N_EXT_LIMITS; i++) {
        if (!(x & EXT_LIMITS[i].bit))
            continue;
        if (EXT_LIMITS[i].lo > *lo)
            *lo = EXT_LIMITS[i].lo;
        if (EXT_LIMITS[i].hi < *hi)
            *hi = EXT_LIMITS[i].hi;
    }
}

static int ext_needed(int op) {
    switch (op) {
        case SX1:
            return X_SX1;
        case SX2:
            return X_SX2;
        case SX4:
            return X_SX4;
        case ZX1:
            return X_ZX1;
        case ZX2:
            return X_ZX2;
        case ZX4:
            return X_ZX4;
        default:
            return 0;
    }
}

static int ext_result(int op) {
    switch (op) {
        case SX1:
            return ext_of_range(-128, 127);
        case SX2:
            return ext_of_range(-32768, 32767);
        case SX4:
            return ext_of_range(INT_MIN, INT_MAX);
        case ZX1:
            return ext_of_range(0, 255);
        case ZX2:
            return ext_of_range(0, 65535);
        case ZX4:
            return ext_of_range(0, 4294967295LL);
        case LDR_B:
        case LDR_INDEX_B:
        case LDR_LOCAL_B:
            return ext_of_range(-128, 127);
        case LDR_W:
        case LDR_INDEX_W:
        case LDR_LOCAL_W:
            return ext_of_range(INT_MIN, INT_MAX);
        case SEQ3:
        case SNE3:
        case SLT3:
        case SGE3:
        case SGT3:
        case SLE3:
        case ULT3:
        case ULE3:
        case SEQI3:
        case SNEI3:
        case SLTI3:
        case SLEI3:
        case SGTI3:
        case SGEI3:
        case ULTI3:
        case ULEI3:
        case UGTI3:
        case UGEI3:
        case FEQ3:
        case FNE3:
        case FLT3:
        case FLE3:
        case FGT3:
        case FGE3:
        case FNZ3:
        case FEQ3_F32:
        case FNE3_F32:
        case FNZ3_F32:
        case FLT3_F32:
        case FLE3_F32:
        case FGT3_F32:
        case FGE3_F32:
            return ext_of_range(0, 1);
        default:
            return 0;
    }
}

// Makes r's extension facts and interval imply each other.
static void fit_reg(Facts *f, int r) {
    long long lo, hi;
    range_of_ext(f->ext[r], &lo, &hi);
    if (f->lo[r] < lo)
        f->lo[r] = lo;
    if (f->hi[r] > hi)
        f->hi[r] = hi;
    if (f->lo[r] > f->hi[r]) {
        f->lo[r] = lo;
        f->hi[r] = hi;
    }
    f->ext[r] |= ext_of_range(f->lo[r], f->hi[r]);
}

// A promoted local's register is written only through emit_promoted_write,
// which normalises it to the variable's type, and a callee restores it, so it
// holds an in-range value at every point of the body.
static void facts_reset(Facts *f, const uint8_t *seed) {
    for (int r = 0; r < NUM_REGS; r++) {
        f->icopy[r] = f->fcopy[r] = -1;
        f->ext[r]                 = seed[r];
        range_of_ext(seed[r], &f->lo[r], &f->hi[r]);
    }
    f->ext[REG_ZERO] = X_ALL;
    f->lo[REG_ZERO] = f->hi[REG_ZERO] = 0;
    f->cmp.valid                      = false;
}

// A call ends what is known except about promoted registers, which the callee
// saves and restores (emit_save_promoted_registers).
static void facts_after_call(const Peep *p, Facts *f) {
    Facts before = *f;
    facts_reset(f, p->seed);
    for (int r = 1; r < NUM_REGS; r++) {
        if (!(p->promoted & BIT(r)))
            continue;
        f->ext[r] = before.ext[r];
        f->lo[r]  = before.lo[r];
        f->hi[r]  = before.hi[r];
    }
}

static bool is_call(int op) {
    return op == CALL || op == CALLI || op == CALLN || op == CALLF;
}

static void kill_int(Facts *f, int d) {
    f->icopy[d] = -1;
    for (int r = 0; r < NUM_REGS; r++)
        if (f->icopy[r] == d)
            f->icopy[r] = -1;
    if (f->cmp.valid && (f->cmp.dst == d || f->cmp.a == d || f->cmp.b == d))
        f->cmp.valid = false;
}

static void kill_float(Facts *f, int d) {
    f->fcopy[d] = -1;
    for (int r = 0; r < NUM_REGS; r++)
        if (f->fcopy[r] == d)
            f->fcopy[r] = -1;
}

static void set_byte(Peep *p, const Insn *in, int k, int reg) {
    InstrWord *w = &p->vm->text_seg[in->pc + 1];
    *w = (*w & ~((InstrWord)0xFF << (8 * k))) | ((InstrWord)reg << (8 * k));
}

static int get_byte(Peep *p, const Insn *in, int k) {
    return (int)((operand(p, in) >> (8 * k)) & 0xFF);
}

// Extensions the result of a bitwise operation satisfies. An AND's result is
// a subset of the bits of a non-negative operand, so it keeps that operand's
// facts.
static int ext_bitwise(int op, int a, int b) {
    int nonneg = X_ZX1 | X_ZX2 | X_ZX4;
    int x      = a & b;
    if (op == AND3) {
        if (a & nonneg)
            x |= a;
        if (b & nonneg)
            x |= b;
    }
    return x;
}

enum { REL_LT, REL_LE, REL_EQ, REL_NE };

// `a REL b` after swapping the operands if `swap`; `uns`: unsigned order;
// `imm`: b is an immediate.
typedef struct {
    int  rel;
    bool swap, uns, imm;
} CmpForm;

static bool cmp_form(int op, CmpForm *c) {
    switch (op) {
        case SLT3:
            *c = (CmpForm){REL_LT, false, false, false};
            return true;
        case SLE3:
            *c = (CmpForm){REL_LE, false, false, false};
            return true;
        case SGT3:
            *c = (CmpForm){REL_LT, true, false, false};
            return true;
        case SGE3:
            *c = (CmpForm){REL_LE, true, false, false};
            return true;
        case SEQ3:
            *c = (CmpForm){REL_EQ, false, false, false};
            return true;
        case SNE3:
            *c = (CmpForm){REL_NE, false, false, false};
            return true;
        case ULT3:
            *c = (CmpForm){REL_LT, false, true, false};
            return true;
        case ULE3:
            *c = (CmpForm){REL_LE, false, true, false};
            return true;
        case SLTI3:
            *c = (CmpForm){REL_LT, false, false, true};
            return true;
        case SLEI3:
            *c = (CmpForm){REL_LE, false, false, true};
            return true;
        case SGTI3:
            *c = (CmpForm){REL_LT, true, false, true};
            return true;
        case SGEI3:
            *c = (CmpForm){REL_LE, true, false, true};
            return true;
        case SEQI3:
            *c = (CmpForm){REL_EQ, false, false, true};
            return true;
        case SNEI3:
            *c = (CmpForm){REL_NE, false, false, true};
            return true;
        case ULTI3:
            *c = (CmpForm){REL_LT, false, true, true};
            return true;
        case ULEI3:
            *c = (CmpForm){REL_LE, false, true, true};
            return true;
        case UGTI3:
            *c = (CmpForm){REL_LT, true, true, true};
            return true;
        case UGEI3:
            *c = (CmpForm){REL_LE, true, true, true};
            return true;
        default:
            return false;
    }
}

static bool point_of(const Facts *f, int r, long long *v) {
    *v = f->lo[r];
    return f->lo[r] == f->hi[r];
}

// The value of the pure integer op `op` when every register it reads holds a
// single known value. Arithmetic wraps and shifts by 0-63 only, as in ops.c.
static bool fold_point(Peep *p, const Insn *in, int op, const Facts *f,
                       long long *out) {
    int       rs1 = get_byte(p, in, 1), rs2 = get_byte(p, in, 2);
    int       rs3 = get_byte(p, in, 3);
    long long a, b = 0, c = 0, imm = 0;
    if (!in->known || !(in->spec.flags & SP_PURE) || in->spec.role[1] != R_IU ||
        !point_of(f, rs1, &a))
        return false;
    unsigned long long ua = (unsigned long long)a;
    switch (op) {
        case NEG3:
            *out = (long long)(0ULL - ua);
            return true;
        case NOT3:
            *out = !a;
            return true;
        case BNOT3:
            *out = ~a;
            return true;
        case SX1:
            *out = (signed char)a;
            return true;
        case SX2:
            *out = (short)a;
            return true;
        case SX4:
            *out = (int)a;
            return true;
        case ZX1:
            *out = (unsigned char)a;
            return true;
        case ZX2:
            *out = (unsigned short)a;
            return true;
        case ZX4:
            *out = (unsigned int)a;
            return true;
        case ADDI3:
            imm  = cc_read_i64_at(p->vm, in->pc + 2);
            *out = (long long)(ua + (unsigned long long)imm);
            return true;
        case MULI3:
            imm  = cc_read_i64_at(p->vm, in->pc + 2);
            *out = (long long)(ua * (unsigned long long)imm);
            return true;
        case SHLI3:
        case SHRI3:
        case USHRI3:
            imm = cc_read_i64_at(p->vm, in->pc + 2);
            if (imm < 0 || imm > 63)
                return false;
            *out = op == SHLI3   ? (long long)(ua << imm)
                   : op == SHRI3 ? a >> imm
                                 : (long long)(ua >> imm);
            return true;
        case ANDI3:
            *out = a & cc_read_i64_at(p->vm, in->pc + 2);
            return true;
        case SEQI3:
        case SNEI3:
        case SLTI3:
        case SLEI3:
        case SGTI3:
        case SGEI3:
        case ULTI3:
        case ULEI3:
        case UGTI3:
        case UGEI3: {
            CmpForm cf;
            imm = cc_read_i64_at(p->vm, in->pc + 2);
            cmp_form(op, &cf);
            long long x = cf.swap ? imm : a, y = cf.swap ? a : imm;
            bool      lt =
                cf.uns ? (unsigned long long)x < (unsigned long long)y : x < y;
            bool eq = x == y;
            *out    = cf.rel == REL_LT   ? lt
                      : cf.rel == REL_LE ? lt || eq
                      : cf.rel == REL_EQ ? eq
                                         : !eq;
            return true;
        }
        case MULADDI3:
            imm = cc_read_i64_at(p->vm, in->pc + 2);
            if (!point_of(f, rs2, &b))
                return false;
            *out = (long long)(ua +
                               (unsigned long long)b * (unsigned long long)imm);
            return true;
        case MULADD3:
            if (!point_of(f, rs2, &b) || !point_of(f, rs3, &c))
                return false;
            *out =
                (long long)(ua + (unsigned long long)b * (unsigned long long)c);
            return true;
        default:
            break;
    }
    if (in->spec.role[2] != R_IU || !point_of(f, rs2, &b))
        return false;
    unsigned long long ub = (unsigned long long)b;
    switch (op) {
        case ADD3:
            *out = (long long)(ua + ub);
            return true;
        case SUB3:
            *out = (long long)(ua - ub);
            return true;
        case MUL3:
            *out = (long long)(ua * ub);
            return true;
        case AND3:
            *out = a & b;
            return true;
        case OR3:
            *out = a | b;
            return true;
        case XOR3:
            *out = a ^ b;
            return true;
        case SHL3:
        case SHR3:
        case USHR3:
            if (b < 0 || b > 63)
                return false;
            *out = op == SHL3   ? (long long)(ua << b)
                   : op == SHR3 ? a >> b
                                : (long long)(ua >> b);
            return true;
        case SEQ3:
            *out = a == b;
            return true;
        case SNE3:
            *out = a != b;
            return true;
        case SLT3:
            *out = a < b;
            return true;
        case SGE3:
            *out = a >= b;
            return true;
        case SGT3:
            *out = a > b;
            return true;
        case SLE3:
            *out = a <= b;
            return true;
        case ULT3:
            *out = ua < ub;
            return true;
        case ULE3:
            *out = ua <= ub;
            return true;
        default:
            return false;
    }
}

// The interval `op` leaves in its destination, when it is tighter than what the
// extension facts give. A sum or difference that may wrap in 64 bits gives up
// unless both operands are single values.
static bool result_range(Peep *p, const Insn *in, int op, const Facts *f,
                         long long *lo, long long *hi) {
    int rs1 = get_byte(p, in, 1), rs2 = get_byte(p, in, 2);
    if (op != LI3 && op != MOV3 && fold_point(p, in, op, f, lo)) {
        *hi = *lo;
        return true;
    }
    switch (op) {
        case MOV3:
            *lo = f->lo[rs1];
            *hi = f->hi[rs1];
            return true;
        case LI3:
            *lo = *hi = cc_read_i64_at(p->vm, in->pc + 2);
            return true;
        case ADDI3: {
            long long imm = cc_read_i64_at(p->vm, in->pc + 2);
            return !__builtin_add_overflow(f->lo[rs1], imm, lo) &&
                   !__builtin_add_overflow(f->hi[rs1], imm, hi);
        }
        case ANDI3: {
            long long mask = cc_read_i64_at(p->vm, in->pc + 2);
            if (mask < 0)
                return false;
            *lo = 0;
            *hi = mask;
            return true;
        }
        case SHRI3: {
            long long k = cc_read_i64_at(p->vm, in->pc + 2);
            if (k < 0 || k > 63)
                return false;
            *lo = f->lo[rs1] >> k;
            *hi = f->hi[rs1] >> k;
            return true;
        }
        case USHRI3: {
            long long k = cc_read_i64_at(p->vm, in->pc + 2);
            if (k < 1 || k > 63)
                return false;
            *lo = 0;
            *hi = (long long)(ULLONG_MAX >> k);
            return true;
        }
        case ADD3:
            return !__builtin_add_overflow(f->lo[rs1], f->lo[rs2], lo) &&
                   !__builtin_add_overflow(f->hi[rs1], f->hi[rs2], hi);
        case SUB3:
            return !__builtin_sub_overflow(f->lo[rs1], f->hi[rs2], lo) &&
                   !__builtin_sub_overflow(f->hi[rs1], f->lo[rs2], hi);
        default:
            return false;
    }
}

// ---- Compares: what a branch on one tells about its operands ----

static void record_cmp(Peep *p, const Insn *in, Facts *f) {
    CmpForm cf;
    if (!cmp_form(in->op, &cf))
        return;
    int rd = get_byte(p, in, 0), a = get_byte(p, in, 1);
    int b = cf.imm ? -1 : get_byte(p, in, 2);
    if (rd == REG_ZERO || rd == a || rd == b)
        return;
    f->cmp = (Cmp){
        in->op, rd, a, b, cf.imm ? cc_read_i64_at(p->vm, in->pc + 2) : 0, true};
}

typedef struct {
    long long *lo, *hi;
} Interval;

// Narrows x and y given x < y (`strict`) or x <= y. An edge the intervals show
// cannot be taken is left alone.
static void clip_le(Interval x, Interval y, bool strict) {
    long long x_hi = *y.hi, y_lo = *x.lo;
    if (strict) {
        if (*y.hi == LLONG_MIN || *x.lo == LLONG_MAX)
            return;
        x_hi = *y.hi - 1;
        y_lo = *x.lo + 1;
    }
    if (x_hi >= *x.lo && x_hi < *x.hi)
        *x.hi = x_hi;
    if (y_lo <= *y.hi && y_lo > *y.lo)
        *y.lo = y_lo;
}

static void clip_eq(Interval x, Interval y) {
    long long lo = *x.lo > *y.lo ? *x.lo : *y.lo;
    long long hi = *x.hi < *y.hi ? *x.hi : *y.hi;
    if (lo > hi)
        return;
    *x.lo = *y.lo = lo;
    *x.hi = *y.hi = hi;
}

static void exclude_point(Interval v, long long k) {
    if (*v.lo == k && *v.hi > k)
        ++*v.lo;
    else if (*v.hi == k && *v.lo < k)
        --*v.hi;
}

static void clip_ne(Interval x, Interval y) {
    if (*y.lo == *y.hi)
        exclude_point(x, *y.lo);
    if (*x.lo == *x.hi)
        exclude_point(y, *x.lo);
}

// Narrows the operands of `c` in `f` for the edge on which the compare is
// `truth`. An unsigned order is the signed one when both sides are known
// non-negative.
static void refine_cmp(Facts *f, const Cmp *c, bool truth) {
    CmpForm cf;
    if (!cmp_form(c->op, &cf) || c->a == c->b)
        return;
    long long imm_lo = c->imm, imm_hi = c->imm;
    Interval  x = {&f->lo[c->a], &f->hi[c->a]};
    Interval  y = c->b >= 0 ? (Interval){&f->lo[c->b], &f->hi[c->b]}
                            : (Interval){&imm_lo, &imm_hi};
    if (cf.uns && (*x.lo < 0 || *y.lo < 0))
        return;
    bool swap = cf.swap;
    int  rel  = cf.rel;
    if (!truth) {
        switch (rel) {
            case REL_LT:
                rel  = REL_LE;
                swap = !swap;
                break;
            case REL_LE:
                rel  = REL_LT;
                swap = !swap;
                break;
            case REL_EQ:
                rel = REL_NE;
                break;
            default:
                rel = REL_EQ;
                break;
        }
    }
    if (swap) {
        Interval t = x;
        x          = y;
        y          = t;
    }
    switch (rel) {
        case REL_LT:
            clip_le(x, y, true);
            break;
        case REL_LE:
            clip_le(x, y, false);
            break;
        case REL_EQ:
            clip_eq(x, y);
            break;
        default:
            clip_ne(x, y);
            break;
    }
    fit_reg(f, c->a);
    if (c->b >= 0)
        fit_reg(f, c->b);
}

// Given two copies of the facts at the end of a block that ends in JZ3/JNZ3,
// narrows `taken` to what holds on the edge the branch jumps along and `fall`
// to what holds on the other.
static void branch_facts(Peep *p, const Insn *br, Facts *taken, Facts *fall) {
    int r = get_byte(p, br, 0);
    if (!fall->cmp.valid || r <= REG_ZERO || r >= NUM_REGS)
        return;
    if (fall->cmp.dst != r && fall->icopy[r] != fall->cmp.dst)
        return;
    bool jumps_when_true = br->op == JNZ3;
    refine_cmp(taken, &fall->cmp, jumps_when_true);
    refine_cmp(fall, &taken->cmp, !jumps_when_true);
}

static void make_mov(Peep *p, Insn *in, int src) {
    p->vm->text_seg[in->pc] = MOV3;
    in->op                  = MOV3;
    op_spec(MOV3, &in->spec);
    set_byte(p, in, 1, src);
    set_byte(p, in, 2, 0);
}

static void make_imm_op(Peep *p, Insn *in, int op, long long imm) {
    p->vm->text_seg[in->pc] = op;
    in->op                  = op;
    op_spec(op, &in->spec);
    cc_write_i64_at(p->vm, in->pc + 2, imm);
}

// Rewrites `in` to an instruction of the same width that gives the same
// result: the constant itself, or a copy of one operand (or of the zero
// register). Instructions never change size, so nothing needs remapping.
static bool fold_in_place(Peep *p, Insn *in, const Facts *f) {
    int op = in->op;
    int rd = get_byte(p, in, 0);
    if (!(in->spec.flags & SP_PURE) || in->spec.role[0] != R_ID ||
        in->spec.role[1] != R_IU || rd == REG_ZERO || op == LI3 || op == MOV3)
        return false;
    int       rs1 = get_byte(p, in, 1), rs2 = get_byte(p, in, 2);
    long long v, a = 0, b = 0;
    if (fold_point(p, in, op, f, &v)) {
        if (in->size == 4) {
            make_imm_op(p, in, LI3, v);
            p->vm->text_seg[in->pc + 1] = (InstrWord)rd;
        } else if (v == 0) {
            make_mov(p, in, REG_ZERO);
        } else {
            return false;
        }
        p->changed = true;
        return true;
    }
    if (op == MULADDI3 && point_of(f, rs2, &b)) {
        long long imm = cc_read_i64_at(p->vm, in->pc + 2);
        make_imm_op(
            p, in, ADDI3,
            (long long)((unsigned long long)b * (unsigned long long)imm));
        set_byte(p, in, 2, 0);
        p->changed = true;
        return true;
    }
    if (in->size != 2 || in->spec.role[2] != R_IU || in->spec.role[3] != R_NO)
        return false;
    bool pa = point_of(f, rs1, &a), pb = point_of(f, rs2, &b);
    int  src = -1;
    switch (op) {
        case ADD3:
        case OR3:
        case XOR3:
            src = pb && b == 0 ? rs1 : pa && a == 0 ? rs2 : -1;
            break;
        case SUB3:
        case SHL3:
        case SHR3:
        case USHR3:
            src = pb && b == 0 ? rs1 : -1;
            break;
        case MUL3:
            src = pb && b == 1                       ? rs1
                  : pa && a == 1                     ? rs2
                  : (pa && a == 0) || (pb && b == 0) ? REG_ZERO
                                                     : -1;
            break;
        case AND3:
            src = (pa && a == 0) || (pb && b == 0) ? REG_ZERO
                  : pb && b == -1                  ? rs1
                  : pa && a == -1                  ? rs2
                                                   : -1;
            break;
    }
    if (src < 0)
        return false;
    make_mov(p, in, src);
    p->changed = true;
    return true;
}

// Walks `blk` from the facts in `*f`, leaving the facts at its end. With
// `rewrite` it also edits the code; without it, it only models what the edits
// would do, so the facts it yields do not depend on them.
static void forward_block(Peep *p, Block *blk, Facts *f, bool rewrite) {
    VirtualMachine *vm = p->vm;
    for (int i = blk->first; i < blk->last; i++) {
        Insn *in = &p->ins[i];
        if (in->dead)
            continue;
        int kind = ctrl_kind(in);
        if ((in->op == MOV3 || in->op == FMOV3) &&
            get_byte(p, in, 0) == get_byte(p, in, 1)) {
            if (rewrite) {
                in->dead   = true;
                p->changed = true;
            }
            continue;
        }
        if (!in->known) {
            if (in->op == JZ3 || in->op == JNZ3) {
                int r = get_byte(p, in, 0);
                if (rewrite && r > REG_ZERO && r < NUM_REGS &&
                    f->icopy[r] >= 0) {
                    set_byte(p, in, 0, f->icopy[r]);
                    p->changed = true;
                }
            } else if (kind == C_NONE) {
                if (is_call(in->op))
                    facts_after_call(p, f);
                else
                    facts_reset(f, p->seed);
            }
            continue;
        }

        // A sign/zero extension of a value already known to fit is a copy.
        int op   = in->op;
        int need = ext_needed(op);
        if (need) {
            int rd = get_byte(p, in, 0), rs = get_byte(p, in, 1);
            if (f->ext[rs] & need) {
                if (rewrite)
                    p->changed = true;
                if (rd == rs) {
                    if (rewrite)
                        in->dead = true;
                    continue;
                }
                if (rewrite) {
                    vm->text_seg[in->pc] = MOV3;
                    in->op               = MOV3;
                    op_spec(MOV3, &in->spec);
                }
                op = MOV3;
            } else if (rewrite) {
                in->src_ext = f->ext[rs];
            }
        }

        if (rewrite) {
            for (int k = 0; k < 4; k++) {
                int role = in->spec.role[k];
                int r    = get_byte(p, in, k);
                if (role == R_IU && r > REG_ZERO && f->icopy[r] >= 0) {
                    set_byte(p, in, k, f->icopy[r]);
                    p->changed = true;
                } else if (role == R_FU && f->fcopy[r] >= 0) {
                    set_byte(p, in, k, f->fcopy[r]);
                    p->changed = true;
                }
            }
        }

        if (rewrite && fold_in_place(p, in, f))
            op = in->op;

        Eff e;
        effects(p, in, &e);
        int ext = ext_result(op);
        if (op == MOV3)
            ext = f->ext[get_byte(p, in, 1)];
        else if (op == AND3 || op == OR3 || op == XOR3)
            ext = ext_bitwise(op, f->ext[get_byte(p, in, 1)],
                              f->ext[get_byte(p, in, 2)]);
        else if (op == ANDI3)
            ext = ext_bitwise(AND3, f->ext[get_byte(p, in, 1)],
                              ext_of_range(cc_read_i64_at(vm, in->pc + 2),
                                           cc_read_i64_at(vm, in->pc + 2)));
        else if (op == LI3)
            ext = ext_of_range(cc_read_i64_at(vm, in->pc + 2),
                               cc_read_i64_at(vm, in->pc + 2));
        long long lo, hi;
        if (!result_range(p, in, op, f, &lo, &hi))
            range_of_ext(ext, &lo, &hi);
        for (int r = 1; r < NUM_REGS; r++) {
            if (e.idef & BIT(r)) {
                kill_int(f, r);
                f->ext[r] = (uint8_t)ext;
                f->lo[r]  = lo;
                f->hi[r]  = hi;
                fit_reg(f, r);
            }
            if (e.fdef & BIT(r))
                kill_float(f, r);
        }
        record_cmp(p, in, f);
        if (op == MOV3) {
            int rd = get_byte(p, in, 0), rs = get_byte(p, in, 1);
            if (rd != rs && rd != REG_ZERO)
                f->icopy[rd] = rs;
        } else if (op == LI3 && lo == 0 && hi == 0) {
            int rd = get_byte(p, in, 0);
            if (rd != REG_ZERO)
                f->icopy[rd] = REG_ZERO;
        } else if (op == FMOV3) {
            int rd = get_byte(p, in, 0), rs = get_byte(p, in, 1);
            if (rd != rs)
                f->fcopy[rd] = rs;
        }
    }
}

// The bounds an interval that keeps growing is pushed out to, so a loop that
// counts settles after a few passes instead of one per step.
static const long long WIDEN_STEPS[] = {
    LLONG_MIN, INT_MIN, -32768,  -128,         0,        127, 255,
    32767,     65535,   INT_MAX, 4294967295LL, LLONG_MAX};
#define N_WIDEN_STEPS (int)(sizeof WIDEN_STEPS / sizeof WIDEN_STEPS[0])

static long long widen_down(long long x) {
    long long w = LLONG_MIN;
    for (int i = 0; i < N_WIDEN_STEPS; i++)
        if (WIDEN_STEPS[i] <= x)
            w = WIDEN_STEPS[i];
    return w;
}

static long long widen_up(long long x) {
    for (int i = 0; i < N_WIDEN_STEPS; i++)
        if (WIDEN_STEPS[i] >= x)
            return WIDEN_STEPS[i];
    return LLONG_MAX;
}

// Merges the extension facts and intervals of `src` into `dst` (intersection
// and hull); with `widen`, a bound that grows jumps to the next step. True if
// `dst` changed.
static bool meet_facts(Facts *dst, const Facts *src, bool widen) {
    bool changed = false;
    for (int r = 0; r < NUM_REGS; r++) {
        uint8_t   ext = dst->ext[r] & src->ext[r];
        long long lo = dst->lo[r], hi = dst->hi[r];
        if (src->lo[r] < lo)
            lo = widen ? widen_down(src->lo[r]) : src->lo[r];
        if (src->hi[r] > hi)
            hi = widen ? widen_up(src->hi[r]) : src->hi[r];
        if (ext == dst->ext[r] && lo == dst->lo[r] && hi == dst->hi[r])
            continue;
        uint8_t   old_ext = dst->ext[r];
        long long old_lo = dst->lo[r], old_hi = dst->hi[r];
        dst->ext[r] = ext;
        dst->lo[r]  = lo;
        dst->hi[r]  = hi;
        fit_reg(dst, r);
        if (dst->ext[r] != old_ext || dst->lo[r] != old_lo ||
            dst->hi[r] != old_hi)
            changed = true;
    }
    return changed;
}

// Drops what does not cross a block boundary: a copy forwarded across one
// keeps the source register live and defeats retargeting.
static void forget_copies(Facts *f) {
    for (int r = 0; r < NUM_REGS; r++)
        f->icopy[r] = f->fcopy[r] = -1;
    f->cmp.valid = false;
}

// Facts at the start of every block: the function entry and any block that
// can be entered from outside the graph start from the seed, the rest from the
// merge over the blocks that reach them. A block nothing reaches is given the
// seed as well. A conditional branch on a compare narrows the facts along each
// of its edges; an edge back to an earlier block widens the intervals.
// TODO: each pass models every block again; a worklist would visit only those
// whose entry changed if compile time on very large functions shows it.
static void compute_entry_facts(Peep *p) {
    for (int b = 0; b < p->nblocks; b++) {
        Block *blk   = &p->blocks[b];
        blk->reached = blk->ext_entry;
        facts_reset(&blk->in, p->seed);
    }
    bool again = true;
    for (int pass = 0; again; pass++) {
        // Widening makes this settle in a few passes; if it ever does not, the
        // seed holds everywhere.
        if (pass == 256) {
            for (int b = 0; b < p->nblocks; b++)
                facts_reset(&p->blocks[b].in, p->seed);
            return;
        }
        again = false;
        for (int b = 0; b < p->nblocks; b++) {
            Block *blk = &p->blocks[b];
            if (!blk->reached)
                continue;
            Facts out = blk->in;
            forward_block(p, blk, &out, false);
            const Insn *last     = &p->ins[blk->last - 1];
            bool        cond     = ctrl_kind(last) == C_COND;
            bool        has_fall = cond && b + 1 < p->nblocks;
            Facts       jump     = out;
            if (cond)
                branch_facts(p, last, &jump, &out);
            forget_copies(&out);
            forget_copies(&jump);
            for (int s = 0; s < blk->nsucc; s++) {
                int    si = p->edges[blk->eoff + s];
                Block *sb = &p->blocks[si];
                if (sb->ext_entry)
                    continue;
                const Facts *edge =
                    cond && !(has_fall && s == 0) ? &jump : &out;
                if (!sb->reached) {
                    sb->in      = *edge;
                    sb->reached = true;
                    again       = true;
                } else if (meet_facts(&sb->in, edge, si <= b)) {
                    again = true;
                }
            }
        }
    }
    for (int b = 0; b < p->nblocks; b++)
        if (!p->blocks[b].reached)
            facts_reset(&p->blocks[b].in, p->seed);
}

// ---- Backward pass: dead definitions and retargeting ----

// `OP t, ...; ...; MOV x, t` with t dead afterwards becomes `OP x, ...`.
static bool try_retarget(Peep *p, Block *blk, int mov, uint32_t il,
                         uint32_t fl) {
    Insn *m  = &p->ins[mov];
    bool  fp = m->op == FMOV3;
    int   x  = get_byte(p, m, 0);
    int   t  = get_byte(p, m, 1);
    if (x == t || (!fp && (x == REG_ZERO || t == REG_ZERO)))
        return false;
    if ((fp ? fl : il) & BIT(t))
        return false;
    for (int i = mov - 1; i >= blk->first && i >= mov - 8; i--) {
        Insn *d = &p->ins[i];
        if (d->dead)
            continue;
        if (!d->known)
            return false;
        Eff e;
        effects(p, d, &e);
        uint32_t defs = fp ? e.fdef : e.idef;
        uint32_t uses = fp ? e.fuse : e.iuse;
        if (defs & BIT(t)) {
            if (defs != BIT(t) || d->spec.role[0] != (fp ? R_FD : R_ID) ||
                get_byte(p, d, 0) != t)
                return false;
            set_byte(p, d, 0, x);
            m->dead    = true;
            p->changed = true;
            return true;
        }
        if ((defs | uses) & (BIT(t) | BIT(x)))
            return false;
    }
    return false;
}

// The facts under which `ext` (SXk / ZXk) keeps a value's zero-ness.
static int ext_zero_facts(int op) {
    switch (op) {
        case SX1:
        case ZX1:
            return X_SX1 | X_ZX1;
        case SX2:
        case ZX2:
            return X_SX2 | X_ZX2;
        default:
            return X_SX4 | X_ZX4;
    }
}

// `SXk/ZXk t, s; JZ3/JNZ3 t` with t dead after the branch tests s directly when
// s already fits k bytes: it is zero exactly when its low k bytes are.
static bool drop_branch_ext(Peep *p, Insn *ext, Insn *br, uint32_t after_br) {
    int rd = get_byte(p, ext, 0), rs = get_byte(p, ext, 1);
    if (rd == REG_ZERO || get_byte(p, br, 0) != rd || (after_br & BIT(rd)) ||
        !(ext->src_ext & ext_zero_facts(ext->op)))
        return false;
    set_byte(p, br, 0, rs);
    ext->dead  = true;
    p->changed = true;
    return true;
}

static void backward_block(Peep *p, Block *blk) {
    uint32_t il = blk->iout, fl = blk->fout;
    int      next     = -1;
    uint32_t after_br = 0;
    for (int i = blk->last - 1; i >= blk->first; i--) {
        Insn *in = &p->ins[i];
        if (in->dead)
            continue;
        if (in->known) {
            Eff e;
            effects(p, in, &e);
            if ((in->spec.flags & SP_PURE) && !(e.idef & il) &&
                !(e.fdef & fl)) {
                in->dead   = true;
                p->changed = true;
                continue;
            }
            if ((in->spec.flags & SP_MOVE) && try_retarget(p, blk, i, il, fl))
                continue;
            if (ext_needed(in->op) && next >= 0 &&
                (p->ins[next].op == JZ3 || p->ins[next].op == JNZ3) &&
                drop_branch_ext(p, in, &p->ins[next], after_br)) {
                int rs = get_byte(p, in, 1);
                il     = after_br | (rs > REG_ZERO ? BIT(rs) : 0);
                continue;
            }
        }
        if (in->op == JZ3 || in->op == JNZ3)
            after_br = il;
        step_back(p, in, &il, &fl);
        next = i;
    }
}

// A jump to the next live instruction goes nowhere.
static void drop_jump_to_next(Peep *p) {
    VirtualMachine *vm = p->vm;
    for (int i = 0; i < p->n; i++) {
        Insn *in = &p->ins[i];
        if (in->dead || in->op != JMP)
            continue;
        Pc  target = vm->text_seg[in->pc + 1];
        int j      = i + 1;
        while (j < p->n && p->ins[j].dead)
            j++;
        Pc next = j < p->n ? p->ins[j].pc : p->end;
        if (target > in->pc && target <= next) {
            in->dead   = true;
            p->changed = true;
        }
    }
}

// ---- Compaction ----

static Pc remap(const Peep *p, const Pc *map, Pc t) {
    if (t < p->start || t > p->end)
        return t;
    return map[t - p->start];
}

static void compact(Peep *p, PeepMark m) {
    VirtualMachine *vm  = p->vm;
    size_t          cap = (size_t)(p->end - p->start) + 1;
    Pc             *map = malloc(cap * sizeof(Pc));
    if (!map)
        error("out of memory");
    Pc out = p->start;
    for (int i = 0; i < p->n; i++) {
        Insn *in = &p->ins[i];
        for (int w = 0; w < in->size; w++)
            map[in->pc - p->start + (Pc)w] = in->dead ? out : out + (Pc)w;
        if (!in->dead)
            out += (Pc)in->size;
    }
    map[p->end - p->start] = out;

    for (int i = 0; i < p->n; i++) {
        Insn *in = &p->ins[i];
        if (in->dead)
            continue;
        Pc np = map[in->pc - p->start];
        if (np != in->pc)
            memmove(&vm->text_seg[np], &vm->text_seg[in->pc],
                    (size_t)in->size * sizeof(InstrWord));
        switch (in->op) {
            case JMP:
                vm->text_seg[np + 1] = remap(p, map, vm->text_seg[np + 1]);
                break;
            case JZ3:
            case JNZ3:
                vm->text_seg[np + 2] = remap(p, map, vm->text_seg[np + 2]);
                break;
            case JMPT: {
                InstrWord count      = vm->text_seg[np + 2];
                vm->text_seg[np + 1] = np + 4;
                vm->text_seg[np + 3] = remap(p, map, vm->text_seg[np + 3]);
                for (InstrWord k = 0; k < count; k++)
                    vm->text_seg[np + 4 + k] =
                        remap(p, map, vm->text_seg[np + 4 + k]);
                break;
            }
            case LTA3: {
                Pc old = cc_byte_offset_to_pc(cc_read_i64_at(vm, np + 2));
                cc_write_i64_at(vm, np + 2,
                                cc_pc_to_byte_offset(remap(p, map, old)));
                break;
            }
        }
    }

    for (int i = m.calls; i < vm->compiler.num_call_patches; i++)
        vm->compiler.call_patches[i].location =
            remap(p, map, vm->compiler.call_patches[i].location);
    for (int i = m.faddrs; i < vm->compiler.num_func_addr_patches; i++)
        vm->compiler.func_addr_patches[i].location =
            remap(p, map, vm->compiler.func_addr_patches[i].location);
    if (vm->dbg.source_map)
        for (int i = m.srcs; i < vm->dbg.source_map_count; i++)
            vm->dbg.source_map[i].pc_offset =
                remap(p, map, (Pc)vm->dbg.source_map[i].pc_offset);
    for (int i = m.labels; i < num_global_labels; i++)
        global_label_map[i].offset = remap(p, map, global_label_map[i].offset);
    for (CleanupRange *r = obj_attr(p->fn)->cleanup_ranges; r; r = r->next) {
        r->start = remap(p, map, (Pc)r->start);
        if (r->end >= 0)
            r->end = remap(p, map, (Pc)r->end);
    }

    vm->text_ptr = out - 1;
    free(map);
}

// ---- Entry points ----

PeepMark cg_peep_mark(VirtualMachine *vm) {
    return (PeepMark){
        vm->compiler.num_call_patches, vm->compiler.num_func_addr_patches,
        vm->dbg.source_map ? vm->dbg.source_map_count : 0, num_global_labels};
}

// Whether every cleanup range still starts and ends on an instruction.
static bool cleanup_ranges_aligned(const Peep *p) {
    for (const CleanupRange *r = obj_attr(p->fn)->cleanup_ranges; r; r = r->next) {
        for (int k = 0; k < 2; k++) {
            long long t = k ? r->end : r->start;
            if (t >= (long long)p->start && t < (long long)p->end &&
                p->at[t - p->start] < 0)
                return false;
        }
    }
    return true;
}

// An instruction holding a not-yet-resolved text address is left alone.
static void pin_location(Peep *p, Pc loc) {
    if (!in_window(p, loc))
        return;
    for (Pc w = loc; w >= p->start && loc - w < 8; w--) {
        int k = p->at[w - p->start];
        if (k >= 0) {
            p->ins[k].known = false;
            return;
        }
    }
}

static void pin_patched(Peep *p, PeepMark m) {
    VirtualMachine *vm = p->vm;
    for (int i = m.calls; i < vm->compiler.num_call_patches; i++)
        pin_location(p, vm->compiler.call_patches[i].location);
    for (int i = m.faddrs; i < vm->compiler.num_func_addr_patches; i++)
        pin_location(p, vm->compiler.func_addr_patches[i].location);
}

static void free_peep(Peep *p) {
    free(p->ins);
    free(p->at);
    free(p->blocks);
    free(p->blk_of);
    free(p->edges);
    free(p->labels);
}

static uint8_t promoted_ext(const Type *ty) {
    bool u = ty->is_unsigned;
    switch (ty->kind) {
        case TY_BOOL:
            return ext_of_range(0, 255);
        case TY_CHAR:
            return u ? ext_of_range(0, 255) : ext_of_range(-128, 127);
        case TY_SHORT:
            return u ? ext_of_range(0, 65535) : ext_of_range(-32768, 32767);
        case TY_INT:
        case TY_ENUM:
            if (ty->size != 4)
                return 0;
            return u ? ext_of_range(0, 4294967295LL)
                     : ext_of_range(INT_MIN, INT_MAX);
        default:
            return 0;
    }
}

void cg_peep_function(VirtualMachine *vm, Obj *fn, PeepMark m) {
    if (!cg_opt_enabled(vm))
        return;
    Peep p = {.vm    = vm,
              .fn    = fn,
              .start = (Pc)obj_fn(fn)->code_addr,
              .end   = vm->text_ptr + 1};
    if (p.end <= p.start || !decode_window(&p) || !mark_leaders(&p, m)) {
        free_peep(&p);
        return;
    }
    pin_patched(&p, m);
    for (int i = 0; i < vm->compiler.promoted_count; i++) {
        p.seed[vm->compiler.promoted_regs[i]] =
            promoted_ext(vm->compiler.promoted_locals[i]->ty);
        p.promoted |= BIT(vm->compiler.promoted_regs[i]);
    }
    build_blocks(&p);

    bool any = false;
    for (int round = 0; round < 6; round++) {
        p.changed = false;
        compute_succs(&p);
        compute_entry_facts(&p);
        for (int b = 0; b < p.nblocks; b++) {
            Facts f = p.blocks[b].in;
            forward_block(&p, &p.blocks[b], &f, true);
        }
        compute_liveness(&p);
        for (int b = 0; b < p.nblocks; b++)
            backward_block(&p, &p.blocks[b]);
        drop_jump_to_next(&p);
        if (!p.changed)
            break;
        any = true;
    }
    if (any)
        compact(&p, m);
    free_peep(&p);

    if (any) {
        Peep v = {
            .vm = vm, .fn = fn, .start = p.start, .end = vm->text_ptr + 1};
        if (!decode_window(&v) || !mark_leaders(&v, m) ||
            !cleanup_ranges_aligned(&v))
            error("codegen: internal error: peephole left invalid bytecode in "
                  "'%s'",
                  fn->name);
        free_peep(&v);
    }
}
