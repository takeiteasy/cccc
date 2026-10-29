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
// TODO: copy and extension facts stop at every branch target, and functions
// with setjmp, computed goto or cleanup attributes are left as generated;
// forward dataflow over the block graph lifts both
// (https://todo.sr.ht/~takeiteasy/cccc/1869).

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

typedef struct {
    Pc     pc;
    int    op;
    int    size;
    bool   dead;
    bool   leader;
    bool   known; // op_spec() applies and every register byte is in range
    OpSpec spec;
} Insn;

typedef struct {
    int      first, last; // instruction indices, [first, last)
    int      succ[2];
    uint32_t iin, fin, iout, fout;
} Block;

typedef struct {
    VirtualMachine *vm;
    Pc              start, end;
    Insn           *ins;
    int             n;
    int            *at;     // word offset - start -> instruction index, or -1
    Block          *blocks;
    int            *blk_of; // instruction index -> block index
    int             nblocks;
    bool            changed;
    uint8_t         seed[NUM_REGS]; // extension facts that hold throughout
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
        Insn *in = &p->ins[i];
        if (in->op == SETJMP || in->op == JMPI)
            return false;
        InstrWord w = vm->text_seg[in->pc + 1];
        if (in->known) {
            for (int k = 0; k < 4; k++)
                if (in->spec.role[k] != R_NO &&
                    ((w >> (8 * k)) & 0xFF) >= NUM_REGS)
                    in->known = false;
        }
    }
    return true;
}

// Marks every branch target as a leader. False if one is not an instruction.
static bool mark_leaders(Peep *p) {
    VirtualMachine *vm = p->vm;
    for (int i = 0; i < p->n; i++) {
        Insn *in = &p->ins[i];
        bool  ok = true;
        switch (in->op) {
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
                ok = mark_target(
                    p, cc_byte_offset_to_pc(cc_read_i64_at(vm, in->pc + 2)));
                break;
        }
        if (!ok)
            return false;
    }
    return true;
}

// ---- Control-flow graph and liveness ----

static void build_blocks(Peep *p) {
    p->blocks = calloc((size_t)p->n + 1, sizeof(Block));
    int *bi = p->blk_of = malloc(((size_t)p->n + 1) * sizeof(int));
    if (!p->blocks || !bi)
        error("out of memory");
    p->nblocks = 0;
    bool open  = false;
    for (int i = 0; i < p->n; i++) {
        if (!open || p->ins[i].leader) {
            if (open)
                p->blocks[p->nblocks - 1].last = i;
            p->blocks[p->nblocks].first = i;
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

static void compute_succs(Peep *p) {
    VirtualMachine *vm = p->vm;
    for (int b = 0; b < p->nblocks; b++) {
        Block *blk   = &p->blocks[b];
        blk->succ[0] = blk->succ[1] = -1;
        const Insn *last            = &p->ins[blk->last - 1];
        int         kind            = ctrl_kind(last);
        bool        fall            = kind == C_NONE || kind == C_COND;
        if (fall && b + 1 < p->nblocks)
            blk->succ[1] = b + 1;
        if (kind == C_JMP || kind == C_COND) {
            Pc t = vm->text_seg[last->pc + (kind == C_JMP ? 1 : 2)];
            if (in_window(p, t))
                blk->succ[0] = p->blk_of[p->at[t - p->start]];
        }
    }
}

static void step_back(Peep *p, const Insn *in, uint32_t *il, uint32_t *fl) {
    Eff e;
    effects(p, in, &e);
    *il = (*il & ~e.idef) | e.iuse;
    *fl = (*fl & ~e.fdef) | e.fuse;
}

static void compute_liveness(Peep *p) {
    compute_succs(p);
    for (int b = 0; b < p->nblocks; b++)
        p->blocks[b].iin = p->blocks[b].fin = 0;
    bool again = true;
    while (again) {
        again = false;
        for (int b = p->nblocks - 1; b >= 0; b--) {
            Block   *blk = &p->blocks[b];
            uint32_t il = 0, fl = 0;
            for (int s = 0; s < 2; s++) {
                if (blk->succ[s] >= 0) {
                    il |= p->blocks[blk->succ[s]].iin;
                    fl |= p->blocks[blk->succ[s]].fin;
                }
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

// Extension facts a value in [lo, hi] satisfies.
static int ext_of_range(long long lo, long long hi) {
    int x = 0;
    if (lo >= -128 && hi <= 127)
        x |= X_SX1;
    if (lo >= -32768 && hi <= 32767)
        x |= X_SX2;
    if (lo >= INT_MIN && hi <= INT_MAX)
        x |= X_SX4;
    if (lo >= 0 && hi <= 255)
        x |= X_ZX1;
    if (lo >= 0 && hi <= 65535)
        x |= X_ZX2;
    if (lo >= 0 && hi <= 4294967295LL)
        x |= X_ZX4;
    return x;
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
        case FEQ3_F32:
        case FNE3_F32:
        case FLT3_F32:
        case FLE3_F32:
        case FGT3_F32:
        case FGE3_F32:
            return ext_of_range(0, 1);
        default:
            return 0;
    }
}

typedef struct {
    int     icopy[NUM_REGS];
    int     fcopy[NUM_REGS];
    uint8_t ext[NUM_REGS];
} Facts;

// A promoted local's register is written only through emit_promoted_write,
// which normalises it to the variable's type, and a callee restores it, so it
// holds an in-range value at every point of the body.
static void facts_reset(Facts *f, const uint8_t *seed) {
    for (int r = 0; r < NUM_REGS; r++) {
        f->icopy[r] = f->fcopy[r] = -1;
        f->ext[r]                 = seed[r];
    }
    f->ext[REG_ZERO] = X_ALL;
}

static void kill_int(Facts *f, int d) {
    f->icopy[d] = -1;
    for (int r = 0; r < NUM_REGS; r++)
        if (f->icopy[r] == d)
            f->icopy[r] = -1;
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

static void forward_block(Peep *p, Block *blk) {
    VirtualMachine *vm = p->vm;
    Facts           f;
    facts_reset(&f, p->seed);
    for (int i = blk->first; i < blk->last; i++) {
        Insn *in = &p->ins[i];
        if (in->dead)
            continue;
        int kind = ctrl_kind(in);
        if ((in->op == MOV3 || in->op == FMOV3) &&
            get_byte(p, in, 0) == get_byte(p, in, 1)) {
            in->dead   = true;
            p->changed = true;
            continue;
        }
        if (!in->known) {
            if (in->op == JZ3 || in->op == JNZ3) {
                int r = get_byte(p, in, 0);
                if (r > REG_ZERO && r < NUM_REGS && f.icopy[r] >= 0) {
                    set_byte(p, in, 0, f.icopy[r]);
                    p->changed = true;
                }
            } else if (kind == C_NONE) {
                facts_reset(&f, p->seed);
            }
            continue;
        }

        // A sign/zero extension of a value already known to fit is a copy.
        int need = ext_needed(in->op);
        if (need) {
            int rd = get_byte(p, in, 0), rs = get_byte(p, in, 1);
            if (f.ext[rs] & need) {
                p->changed = true;
                if (rd == rs) {
                    in->dead = true;
                    continue;
                }
                vm->text_seg[in->pc] = MOV3;
                in->op               = MOV3;
                op_spec(MOV3, &in->spec);
            }
        }

        for (int k = 0; k < 4; k++) {
            int role = in->spec.role[k];
            int r    = get_byte(p, in, k);
            if (role == R_IU && r > REG_ZERO && f.icopy[r] >= 0) {
                set_byte(p, in, k, f.icopy[r]);
                p->changed = true;
            } else if (role == R_FU && f.fcopy[r] >= 0) {
                set_byte(p, in, k, f.fcopy[r]);
                p->changed = true;
            }
        }

        Eff e;
        effects(p, in, &e);
        int ext = ext_result(in->op);
        if (in->op == MOV3)
            ext = f.ext[get_byte(p, in, 1)];
        if (in->op == LI3)
            ext = ext_of_range(cc_read_i64_at(vm, in->pc + 2),
                               cc_read_i64_at(vm, in->pc + 2));
        for (int r = 1; r < NUM_REGS; r++) {
            if (e.idef & BIT(r)) {
                kill_int(&f, r);
                f.ext[r] = (uint8_t)ext;
            }
            if (e.fdef & BIT(r))
                kill_float(&f, r);
        }
        if (in->op == MOV3) {
            int rd = get_byte(p, in, 0), rs = get_byte(p, in, 1);
            if (rd != rs && rd != REG_ZERO && rs != REG_ZERO)
                f.icopy[rd] = rs;
        } else if (in->op == FMOV3) {
            int rd = get_byte(p, in, 0), rs = get_byte(p, in, 1);
            if (rd != rs)
                f.fcopy[rd] = rs;
        }
    }
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

static void backward_block(Peep *p, Block *blk) {
    uint32_t il = blk->iout, fl = blk->fout;
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
        }
        step_back(p, in, &il, &fl);
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

    vm->text_ptr = out - 1;
    free(map);
}

// ---- Entry points ----

PeepMark cg_peep_mark(VirtualMachine *vm) {
    return (PeepMark){
        vm->compiler.num_call_patches, vm->compiler.num_func_addr_patches,
        vm->dbg.source_map ? vm->dbg.source_map_count : 0, num_global_labels};
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
    if (!cg_opt_enabled(vm) || fn->cleanup_ranges)
        return;
    Peep p = {.vm = vm, .start = (Pc)fn->code_addr, .end = vm->text_ptr + 1};
    if (p.end <= p.start || !decode_window(&p) || !mark_leaders(&p)) {
        free_peep(&p);
        return;
    }
    pin_patched(&p, m);
    for (int i = 0; i < vm->compiler.promoted_count; i++)
        p.seed[vm->compiler.promoted_regs[i]] =
            promoted_ext(vm->compiler.promoted_locals[i]->ty);
    build_blocks(&p);

    bool any = false;
    for (int round = 0; round < 6; round++) {
        p.changed = false;
        for (int b = 0; b < p.nblocks; b++)
            forward_block(&p, &p.blocks[b]);
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
        Peep v = {.vm = vm, .start = p.start, .end = vm->text_ptr + 1};
        if (!decode_window(&v) || !mark_leaders(&v))
            error("codegen: internal error: peephole left invalid bytecode in "
                  "'%s'",
                  fn->name);
        free_peep(&v);
    }
}
