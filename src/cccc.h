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

#ifndef CCCC_H
#define CCCC_H

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <ffi.h>
#include <libgen.h>
#include <math.h>
#include <setjmp.h>
#include <signal.h>
#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
#else
#include <dlfcn.h>
#include <sys/mman.h>
#include <unistd.h>
#endif


#ifdef __cplusplus
extern "C" {
#endif

#define OPS_X                                                                  \
    /* Control flow */                                                         \
    X(JMP, 1)   /* Unconditional jump */                                       \
    X(CALL, 1)  /* Call function (direct) */                                   \
    X(CALLT, 1) /* Tail call (direct): reuse current frame */                  \
    X(CALLI, 1) /* Call function (indirect via register) */                    \
    X(CALLN, 6) /* Native-aware indirect call */                               \
    X(JMPT, 3)  /* Jump table */                                               \
    X(JMPI, 1)  /* Indirect jump */                                            \
    /* VM memory operations (self-contained, no system calls) */               \
    X(MALC, 0)                                                                 \
    X(MFRE, 0)                                                                 \
    X(MCPY, 0)                                                                 \
    X(MSET, 0) /* memset to 0: dest=REG_A0, count=REG_A2; backs ND_MEMZERO */   \
    X(REALC, 0)                                                                \
    X(CALC, 0)                                                                 \
    X(REALCA, 0) /* reallocarray: ptr=A0, nmemb=A1, size=A2, result=A0 (#699) */ \
    X(MALCA, 0) /* aligned_alloc: size=A0, alignment=A1, result=A0 */          \
    X(PMEMA, 0) /* posix_memalign: memptr=A0, alignment=A1, size=A2, result=A0 */ \
    /* Type conversion instructions (in-register) */                           \
    X(SX1, 1)   /* Sign extend 1 byte to 8 bytes */                            \
    X(SX2, 1)   /* Sign extend 2 bytes to 8 bytes */                           \
    X(SX4, 1)   /* Sign extend 4 bytes to 8 bytes */                           \
    X(ZX1, 1)   /* Zero extend 1 byte to 8 bytes */                            \
    X(ZX2, 1)   /* Zero extend 2 bytes to 8 bytes */                           \
    X(ZX4, 1)   /* Zero extend 4 bytes to 8 bytes */                           \
    X(CALLF, 6) /* Foreign function interface */                               \
    X(DLOPEN, 0)                                                                \
    X(DLSYM, 0)                                                                 \
    X(DLCLOSE, 0)                                                               \
    X(DLERROR, 0)                                                               \
    /* Memory safety opcodes (keep legacy for instrumentation) */              \
    X(CHKB, 1)  /* Check array bounds */                                       \
    X(CHKI, 2)  /* Check initialization */                                     \
    X(MARKI, 2) /* Mark as initialized */                                      \
    X(CHKPA, 1) /* Check pointer arithmetic (invalid arithmetic detection) */  \
    X(MARKP, 4) /* Mark provenance (track pointer origin) */                   \
    /* Stack instrumentation opcodes */                                        \
    X(SCOPEIN, 1)  /* Mark scope entry (allocate/activate variables) */        \
    X(SCOPEOUT, 1) /* Mark scope exit (invalidate variables, detect dangling   \
                   pointers) */                                                \
    X(CHKL, 3)     /* Check variable liveness before access: [offset:i64]      \
                   [scope_id] -- liveness itself is keyed by runtime address   \
                   (bp+offset); scope_id only names the variable in the error  \
                   message on failure (#671) */                                \
    X(MARKR, 2)    /* Mark variable read access: [offset:i64] */               \
    X(MARKW, 2)    /* Mark variable write access: [offset:i64] */              \
    /* Non-local jump instructions (setjmp/longjmp) */                         \
    X(SETJMP, 0)  /* Save execution context to jmp_buf, return 0 */            \
    X(LONGJMP, 0) /* Restore execution context from jmp_buf, return val */     \
    /* Register-based arithmetic */                                            \
    X(ADD3, 1) /* rd = rs1 + rs2 */                                            \
    X(SUB3, 1) /* rd = rs1 - rs2 */                                            \
    X(MUL3, 1) /* rd = rs1 * rs2 */                                            \
    X(MULI3, 3) /* rd = rs1 * immediate */                                     \
    X(MULADD3, 1) /* rd = rs1 + rs2 * rs3 */                                   \
    X(MULADDI3, 3) /* rd = rs1 + rs2 * immediate */                            \
    X(DIV3, 1) /* rd = rs1 / rs2 (signed) */                                   \
    X(ADDC, 1) /* checked signed add: rd = rs1 + rs2 */                        \
    X(SUBC, 1) /* checked signed sub: rd = rs1 - rs2 */                        \
    X(MULC, 1) /* checked signed mul: rd = rs1 * rs2 */                        \
    X(DIVC, 1) /* checked signed div: rd = rs1 / rs2 */                        \
    X(UDIV3, 1) /* rd = rs1 / rs2 (unsigned) */                                \
    X(MOD3, 1) /* rd = rs1 % rs2 (signed) */                                   \
    X(UMOD3, 1) /* rd = rs1 % rs2 (unsigned) */                                \
    X(AND3, 1) /* rd = rs1 & rs2 */                                            \
    X(OR3, 1)  /* rd = rs1 | rs2 */                                            \
    X(XOR3, 1) /* rd = rs1 ^ rs2 */                                            \
    X(SHL3, 1) /* rd = rs1 << rs2 */                                           \
    X(SHR3, 1) /* rd = rs1 >> rs2 (arithmetic, signed) */                      \
    X(USHR3, 1) /* rd = rs1 >> rs2 (logical, unsigned) */                      \
    /* Wide _BitInt(N>64) multi-word arithmetic/shifts.  All operand-free   */ \
    /* (args read from fixed REG_A0-A5, like MCPY): arithmetic ops take     */ \
    /* dst=A0,a=A1,b=A2,words=A3,width=A4 (DIV/MOD add is_signed=A5);       */ \
    /* shifts take dst=A0,src=A1,shift_amount=A2,words=A3,width=A4.         */ \
    X(WIDE_ADD, 0)  /* dst[] = a[] + b[] */                                   \
    X(WIDE_SUB, 0)  /* dst[] = a[] - b[] */                                   \
    X(WIDE_MUL, 0)  /* dst[] = a[] * b[] */                                   \
    X(WIDE_DIV, 0)  /* dst[] = a[] / b[] (signed per A5) */                   \
    X(WIDE_MOD, 0)  /* dst[] = a[] % b[] (signed per A5) */                   \
    X(WIDE_SHL, 0)  /* dst[] = src[] << shift_amount */                       \
    X(WIDE_SHR, 0)  /* dst[] = src[] >> shift_amount (arithmetic, signed) */  \
    X(WIDE_USHR, 0) /* dst[] = src[] >> shift_amount (logical, unsigned) */   \
    /* Register-based comparisons */                                           \
    X(SEQ3, 1) /* rd = (rs1 == rs2) */                                         \
    X(SNE3, 1) /* rd = (rs1 != rs2) */                                         \
    X(SLT3, 1) /* rd = (rs1 < rs2) */                                          \
    X(SGE3, 1) /* rd = (rs1 >= rs2) */                                         \
    X(SGT3, 1) /* rd = (rs1 > rs2) */                                          \
    X(SLE3, 1) /* rd = (rs1 <= rs2) */                                         \
    X(ULT3, 1) /* rd = (rs1 <  rs2), unsigned 64-bit */                        \
    X(ULE3, 1) /* rd = (rs1 <= rs2), unsigned 64-bit */                        \
    /* Register operations */                                                  \
    X(LI3, 3)   /* rd = immediate */                                           \
    X(LDA3, 3)  /* rd = data_seg + immediate byte offset */                    \
    X(LDTLS3, 3) /* rd = current_tls_seg + immediate byte offset */            \
    X(LTA3, 3)  /* rd = text_seg + immediate byte offset */                    \
    X(MOV3, 1)  /* rd = rs1 */                                                 \
    X(NEG3, 1)  /* rd = -rs1 (integer unary negation) */                       \
    X(NOT3, 1)  /* rd = !rs (logical not) */                                   \
    X(BNOT3, 1) /* rd = ~rs (bitwise not) */                                   \
    X(ADDI3, 3) /* rd = rs1 + immediate */                                     \
    X(LEA3, 3)  /* rd = bp + immediate (local variable address) */             \
    X(STKTAG, 5) /* Tag an aggregate local's [bp+offset, bp+offset+size) extent \
                    with the current frame's liveness epoch, for interior      \
                    dangling-pointer resolution (#675). Emitted immediately     \
                    after the LEA3 base of an escaping array/struct local.      \
                    Format: [STKTAG][unused:32][offset:i64][size:i64] */        \
    /* Register-based control flow */                                          \
    X(JZ3, 2)  /* if (regs[rs] == 0) pc = target */                            \
    X(JNZ3, 2) /* if (regs[rs] != 0) pc = target */                            \
    /* Register-based function frame */                                        \
    X(ENT3, 4) /* Enter function: stack_size|param_count */                    \
    X(LEV3, 0) /* Leave function: return value in REG_A0 */                    \
    X(ADJ, 2)  /* Adjust stack pointer */                                      \
    X(PSH3, 1) /* Push regs[rs] onto stack: *--sp = regs[rs] */                \
    X(POP3, 1) /* Pop from stack into regs[rd]: rd = *sp++ */                  \
    /* Register-based load/store */                                            \
    X(LDR_B, 1) /* regs[rd] = *(char*)regs[rs] (load byte, sign-extend) */     \
    X(LDR_H, 1) /* regs[rd] = *(short*)regs[rs] (load halfword, sign-extend) */\
    X(LDR_W, 1) /* regs[rd] = *(int*)regs[rs] (load word, sign-extend) */      \
    X(LDR_D, 1) /* regs[rd] = *(long long*)regs[rs] (load dword) */            \
    X(STR_B, 1) /* *(char*)regs[rs] = regs[rd] (store byte) */                 \
    X(STR_H, 1) /* *(short*)regs[rs] = regs[rd] (store halfword) */            \
    X(STR_W, 1) /* *(int*)regs[rs] = regs[rd] (store word) */                  \
    X(STR_D, 1) /* *(long long*)regs[rs] = regs[rd] (store dword) */           \
    /* Atomic register-based load/store (ALDR/ASTR) and RMW (AXCHG/ACAS) */  \
    X(ALDR, 3)  /* atomic load:  rd = *(T*)regs[rs]; width_enc in i64        \
                   width_enc = (size<<1)|is_unsigned; tags atomic_shadow */   \
    X(ASTR, 3)  /* atomic store: *(T*)regs[rs] = rd; width_enc in i64        \
                   tags atomic_shadow for mixed-access detection */            \
    X(AXCHG, 2) /* atomic exchange: old=*(T*)A0; *(T*)A0=(T)A1; A0=old      \
                   width_enc in i64 */                                         \
    X(ACAS, 2)  /* atomic CAS: if *(T*)A0==*(T*)A1 {*(T*)A0=(T)A2;A0=1}    \
                   else {*(T*)A1=*(T*)A0;A0=0}; width_enc in i64 */           \
    /* Floating-point register operations */                                   \
    X(FLDR, 1)  /* fregs[rd] = *(double*)regs[rs] */                           \
    X(FSTR, 1)  /* *(double*)regs[rs] = fregs[rd] */                           \
    X(FADD3, 1) /* fregs[rd] = fregs[rs1] + fregs[rs2] */                      \
    X(FSUB3, 1) /* fregs[rd] = fregs[rs1] - fregs[rs2] */                      \
    X(FMUL3, 1) /* fregs[rd] = fregs[rs1] * fregs[rs2] */                      \
    X(FDIV3, 1) /* fregs[rd] = fregs[rs1] / fregs[rs2] */                      \
    X(FMOV3, 1) /* fregs[rd] = fregs[rs1] */                                   \
    X(FNEG3, 1) /* fregs[rd] = -fregs[rs1] */                                  \
    X(FEQ3, 1)  /* rd = (fregs[rs1] == fregs[rs2]) */                          \
    X(FNE3, 1)  /* rd = (fregs[rs1] != fregs[rs2]) */                          \
    X(FLT3, 1)  /* rd = (fregs[rs1] < fregs[rs2]) */                           \
    X(FLE3, 1)  /* rd = (fregs[rs1] <= fregs[rs2]) */                          \
    X(FGT3, 1)  /* rd = (fregs[rs1] > fregs[rs2]) */                           \
    X(FGE3, 1)  /* rd = (fregs[rs1] >= fregs[rs2]) */                          \
    X(I2F3, 1)  /* fregs[rd] = (double)regs[rs] */                             \
    X(F2I3, 1)  /* regs[rd] = (long long)fregs[rs] */                          \
    X(FR2R, 1)  /* regs[rd] = *(long long*)&fregs[rs] (bit-pattern transfer) */\
    X(R2FR, 1)  /* fregs[rd] = *(double*)&regs[rs] (bit-pattern transfer, reverse \
                of FR2R) */                                                    \
    X(FADD3_F32, 1) /* fregs[rd] = (float)(fregs[rs1] + fregs[rs2]) */         \
    X(FSUB3_F32, 1) /* fregs[rd] = (float)(fregs[rs1] - fregs[rs2]) */         \
    X(FMUL3_F32, 1) /* fregs[rd] = (float)(fregs[rs1] * fregs[rs2]) */         \
    X(FDIV3_F32, 1) /* fregs[rd] = (float)(fregs[rs1] / fregs[rs2]) */         \
    X(FNEG3_F32, 1) /* fregs[rd] = (float)-fregs[rs1] */                       \
    X(FEQ3_F32, 1)  /* rd = ((float)fregs[rs1] == (float)fregs[rs2]) */        \
    X(FNE3_F32, 1)  /* rd = ((float)fregs[rs1] != (float)fregs[rs2]) */        \
    X(FLT3_F32, 1)  /* rd = ((float)fregs[rs1] < (float)fregs[rs2]) */         \
    X(FLE3_F32, 1)  /* rd = ((float)fregs[rs1] <= (float)fregs[rs2]) */        \
    X(FGT3_F32, 1)  /* rd = ((float)fregs[rs1] > (float)fregs[rs2]) */         \
    X(FGE3_F32, 1)  /* rd = ((float)fregs[rs1] >= (float)fregs[rs2]) */        \
    X(I2F3_F32, 1)  /* fregs[rd] = (float)regs[rs] */                          \
    X(F2I3_F32, 1)  /* regs[rd] = (long long)(float)fregs[rs] */               \
    X(FR2R_F32, 1)  /* regs[rd] = raw float payload bits from fregs[rs] */      \
    X(R2FR_F32, 1)  /* fregs[rd] = raw float payload bits from regs[rs] */      \
    /* Register-based safety opcodes */                                        \
    X(CHKP3, 1) /* Check pointer validity: regs[rs] */                         \
    X(CHKA3, 3) /* Check alignment: regs[rs], immediate alignment */           \
    X(CHKT3, 3) /* Check type: regs[rs], immediate TypeKind */                 \
    /* Struct return buffer support */                                         \
    X(RETBUF, 0) /* Get next return buffer: REG_A0 = rotating pool buffer */    \
    X(FLDR_F32, 1)   /* fregs[rd] = *(float*)regs[rs] (widened to double) */    \
    X(FSTR_F32, 1)   /* *(float*)regs[rs] = fregs[rd] as f32 */                \
    X(FROUND_F32, 1) /* fregs[rd] = (float)fregs[rs] (rounded to float prec) */ \
    X(BTRAP, 0)      /* Halt execution (unreachable/builtin trap) */    \
    /* VM-managed signal handling */                                      \
    X(VSIGNAL, 0)   /* signal(sig, handler): register VM signal action */ \
    X(VRAISE,  0)   /* raise(sig): deliver signal from VM context */      \
    /* Bit-manipulation builtins */                                        \
    X(CLZ,      3)   /* rd = clz(rs); operand2 = bit-width (32 or 64) */  \
    X(CTZ,      3)   /* rd = ctz(rs); operand2 = bit-width (32 or 64) */  \
    X(POPCOUNT, 1)   /* rd = popcount(rs) — width-agnostic 64-bit */       \
    X(FFS,      3)   /* rd = ffs(rs); operand2 = bit-width; 0 maps to 0 */\
    X(BSWAP,    3)   /* rd = bswap(rs); operand2 = byte-width (2,4,8) */   \
    /* Checked arithmetic builtins */                                      \
    X(IOVFL,    2)   /* overflow arith: a=A0,b=A1,ptr=A2,bool→A0;         \
                        operand = (op_type<<8)|type_kind */                \
    /* Fused bp-relative (local) load/store — replaces LEA3+LDR/STR */    \
    X(LDR_LOCAL_B, 3) /* regs[rd] = *(char*)(bp+offset) */                \
    X(LDR_LOCAL_H, 3) /* regs[rd] = *(short*)(bp+offset) */               \
    X(LDR_LOCAL_W, 3) /* regs[rd] = *(int*)(bp+offset) */                 \
    X(LDR_LOCAL_D, 3) /* regs[rd] = *(long long*)(bp+offset) */           \
    X(STR_LOCAL_B, 3) /* *(char*)(bp+offset) = regs[rd] */                \
    X(STR_LOCAL_H, 3) /* *(short*)(bp+offset) = regs[rd] */               \
    X(STR_LOCAL_W, 3) /* *(int*)(bp+offset) = regs[rd] */                 \
    X(STR_LOCAL_D, 3) /* *(long long*)(bp+offset) = regs[rd] */           \
    X(FLDR_LOCAL,     3) /* fregs[rd] = *(double*)(bp+offset) */          \
    X(FSTR_LOCAL,     3) /* *(double*)(bp+offset) = fregs[rd] */          \
    X(FLDR_LOCAL_F32, 3) /* fregs[rd] = *(float*)(bp+offset) */           \
    X(FSTR_LOCAL_F32, 3) /* *(float*)(bp+offset) = (float)fregs[rd] */    \
    /* Fused indexed load/store: base + index * scale + byte offset */    \
    X(LDR_INDEX_B, 3) /* regs[rd] = *(char*)(base+idx*scale+off) */       \
    X(LDR_INDEX_H, 3) /* regs[rd] = *(short*)(base+idx*scale+off) */      \
    X(LDR_INDEX_W, 3) /* regs[rd] = *(int*)(base+idx*scale+off) */        \
    X(LDR_INDEX_D, 3) /* regs[rd] = *(long long*)(base+idx*scale+off) */  \
    X(STR_INDEX_B, 3) /* *(char*)(base+idx*scale+off) = regs[rd] */       \
    X(STR_INDEX_H, 3) /* *(short*)(base+idx*scale+off) = regs[rd] */      \
    X(STR_INDEX_W, 3) /* *(int*)(base+idx*scale+off) = regs[rd] */        \
    X(STR_INDEX_D, 3) /* *(long long*)(base+idx*scale+off) = regs[rd] */  \
    X(FLDR_INDEX,     3) /* fregs[rd] = *(double*)(base+idx*scale+off) */ \
    X(FSTR_INDEX,     3) /* *(double*)(base+idx*scale+off) = fregs[rd] */ \
    X(FLDR_INDEX_F32, 3) /* fregs[rd] = *(float*)(base+idx*scale+off) */  \
    X(FSTR_INDEX_F32, 3) /* *(float*)(base+idx*scale+off) = fregs[rd] */  \
    /* Fused floating-point multiply-add: fregs[rd] = fregs[rs1] + fregs[rs2]*fregs[rs3] */ \
    X(FMADD3,         1) /* f64 two-rounding: product rounded to double, then added */      \
    X(FMADD3_F32,     1) /* f32 two-rounding: product rounded to float, then added */       \
    X(FMADD3_FMA,     1) /* f64 single-rounding: fma(rs2,rs3,rs1)  (--fma opt-in) */       \
    X(FMADD3_F32_FMA, 1) /* f32 single-rounding: fmaf(rs2,rs3,rs1) (--fma opt-in) */  \
    /* Fused floating-point multiply-subtract: fregs[rd] = fregs[rs2]*fregs[rs3] - fregs[rs1] */ \
    X(FMSUB3,         1) /* f64 two-rounding: product rounded to double, then subtracted */ \
    X(FMSUB3_F32,     1) /* f32 two-rounding: product rounded to float, then subtracted */ \
    X(FMSUB3_FMA,     1) /* f64 single-rounding: fma(rs2,rs3,-rs1)  (--fma opt-in) */     \
    X(FMSUB3_F32_FMA, 1) /* f32 single-rounding: fmaf(rs2,rs3,-rs1) (--fma opt-in) */    \
    /* Fused floating-point negated multiply-subtract: fregs[rd] = fregs[rs1] - fregs[rs2]*fregs[rs3] */ \
    X(FNMSUB3,         1) /* f64 two-rounding: rs1 minus product, rounded to double */    \
    X(FNMSUB3_F32,     1) /* f32 two-rounding: rs1 minus product, rounded to float */     \
    X(FNMSUB3_FMA,     1) /* f64 single-rounding: fma(-rs2,rs3,rs1)  (--fma opt-in) */   \
    X(FNMSUB3_F32_FMA, 1) /* f32 single-rounding: fmaf(-rs2,rs3,rs1) (--fma opt-in) */  \
    /* Return-address capture */                                                       \
    X(RETADDR, 3) /* rd = return address n frames up; NULL past outermost frame.   \
                     Format: [RETADDR][rd:8|unused:56][level:i64] */               \
    /* Runtime dynamic object sizing */                                             \
    X(DYNOBJSZ, 3) /* rd = runtime object byte-size at regs[rs].                  \
                      Reads AllocHeader.requested_size for VM heap allocations;    \
                      falls back to (size_t)-1 (type 0/1) or 0 (type 2/3) for    \
                      non-heap, freed, or unknown pointers.                        \
                      Format: [DYNOBJSZ][rd:8|rs:8|unused:48][type:i64] */          \
    /* SIMD vector registers, 128/256/512-bit (tracker #72/#463, widened by   \
       #722). The lane type is carried by the opcode, mirroring the           \
       FADD3/FADD3_F32 scalar split; the register itself (VReg) is a raw      \
       union sized to the widest supported vector (64 bytes). The active      \
       WIDTH (byte count for VLDR/VSTR) or LANE COUNT (for every other op)    \
       rides in the operand word's otherwise-unused "scale" byte -- all such  \
       ops are encoded/decoded with ENCODE_RRRS/DECODE_RRRS (2-register ops   \
       like VLDR/VSTR/VSPLAT/VNEG/VNOT/VCVT set rs2 to 0 and leave it unread,  \
       exactly like the existing VEC_EXTRACT_* handlers already ignore rs2).  \
       RRRS-encoded extract/insert instead put the LANE INDEX in "scale"      \
       (unchanged from #72). Opcode names below no longer carry a fixed       \
       lane count suffix (that was true only while                           \
       every vector was exactly 128 bits); one opcode per element family      \
       now serves all three widths. */                                       \
    X(VLDR, 1) /* vregs[rd] = <width> raw bytes at regs[rs] (unaligned-safe) */    \
    X(VSTR, 1) /* <width> raw bytes at regs[rs] = vregs[rd] */                     \
    X(VMOV3, 1) /* vregs[rd] = vregs[rs1] (full-register copy, all 64 bytes) */    \
    X(VSPLAT_F64, 1) /* vregs[rd].f64[0..count-1] = fregs[rs1] */                  \
    X(VSPLAT_F32, 1) /* vregs[rd].f32[0..count-1] = (float)fregs[rs1] */           \
    X(VSPLAT_I64, 1) /* vregs[rd].i64[0..count-1] = regs[rs1] */                   \
    X(VSPLAT_I32, 1) /* vregs[rd].i32[0..count-1] = (int32_t)regs[rs1] */          \
    X(VSPLAT_I16, 1) /* vregs[rd].i16[0..count-1] = (int16_t)regs[rs1] */          \
    X(VSPLAT_I8,  1) /* vregs[rd].i8[0..count-1] = (int8_t)regs[rs1] */            \
    X(VEXTRACT_F64, 1) /* fregs[rd] = vregs[rs1].f64[lane] */                      \
    X(VEXTRACT_F32, 1) /* fregs[rd] = (double)vregs[rs1].f32[lane] */              \
    X(VEXTRACT_I64, 1) /* regs[rd] = vregs[rs1].i64[lane] */                       \
    X(VEXTRACT_I32, 1) /* regs[rd] = (long long)vregs[rs1].i32[lane] */            \
    X(VEXTRACT_I16, 1) /* regs[rd] = (long long)vregs[rs1].i16[lane] */            \
    X(VEXTRACT_I8,  1) /* regs[rd] = (long long)vregs[rs1].i8[lane] */             \
    X(VINSERT_F64, 1) /* vregs[rd].f64[lane] = fregs[rs1] */                       \
    X(VINSERT_F32, 1) /* vregs[rd].f32[lane] = (float)fregs[rs1] */                \
    X(VINSERT_I64, 1) /* vregs[rd].i64[lane] = regs[rs1] */                        \
    X(VINSERT_I32, 1) /* vregs[rd].i32[lane] = (int32_t)regs[rs1] */               \
    X(VINSERT_I16, 1) /* vregs[rd].i16[lane] = (int16_t)regs[rs1] */               \
    X(VINSERT_I8,  1) /* vregs[rd].i8[lane] = (int8_t)regs[rs1] */                 \
    X(VADD_F64, 1) /* vregs[rd].f64[i] = vregs[rs1].f64[i] + vregs[rs2].f64[i], i<count */ \
    X(VSUB_F64, 1) /* ditto, - */                                                  \
    X(VMUL_F64, 1) /* ditto, * */                                                  \
    X(VDIV_F64, 1) /* ditto, / */                                                  \
    X(VNEG_F64, 1) /* vregs[rd].f64[i] = -vregs[rs1].f64[i], i<count */            \
    X(VADD_F32, 1) /* vregs[rd].f32[i] = vregs[rs1].f32[i] + vregs[rs2].f32[i], i<count */ \
    X(VSUB_F32, 1) /* ditto, - */                                                  \
    X(VMUL_F32, 1) /* ditto, * */                                                  \
    X(VDIV_F32, 1) /* ditto, / */                                                  \
    X(VNEG_F32, 1) /* vregs[rd].f32[i] = -vregs[rs1].f32[i], i<count */            \
    X(VADD_I64, 1) /* vregs[rd].i64[i] = vregs[rs1].i64[i] + vregs[rs2].i64[i], i<count */ \
    X(VSUB_I64, 1) /* ditto, - */                                                  \
    X(VMUL_I64, 1) /* ditto, * */                                                  \
    X(VNEG_I64, 1) /* vregs[rd].i64[i] = -vregs[rs1].i64[i], i<count */            \
    X(VADD_I32, 1) /* vregs[rd].i32[i] = vregs[rs1].i32[i] + vregs[rs2].i32[i], i<count */ \
    X(VSUB_I32, 1) /* ditto, - */                                                  \
    X(VMUL_I32, 1) /* ditto, * */                                                  \
    X(VNEG_I32, 1) /* vregs[rd].i32[i] = -vregs[rs1].i32[i], i<count */            \
    X(VADD_I16, 1) /* vregs[rd].i16[i] = vregs[rs1].i16[i] + vregs[rs2].i16[i], i<count */ \
    X(VSUB_I16, 1) /* ditto, - */                                                  \
    X(VMUL_I16, 1) /* ditto, * */                                                  \
    X(VNEG_I16, 1) /* vregs[rd].i16[i] = -vregs[rs1].i16[i], i<count */            \
    X(VADD_I8, 1) /* vregs[rd].i8[i] = vregs[rs1].i8[i] + vregs[rs2].i8[i], i<count */ \
    X(VSUB_I8, 1) /* ditto, - */                                                   \
    X(VMUL_I8, 1) /* ditto, * */                                                   \
    X(VNEG_I8, 1) /* vregs[rd].i8[i] = -vregs[rs1].i8[i], i<count */               \
    /* Bitwise (tracker #715): width-agnostic, over i64[0..words-1] where     \
       words = <width bytes>/8 (rides in the operand, like VLDR/VSTR). */         \
    X(VAND, 1) /* vregs[rd].i64[i] = vregs[rs1].i64[i] & vregs[rs2].i64[i], i<words */ \
    X(VOR,  1) /* ditto, | */                                                     \
    X(VXOR, 1) /* ditto, ^ */                                                     \
    X(VNOT, 1) /* vregs[rd].i64[i] = ~vregs[rs1].i64[i], i<words */               \
    /* Integer lane division/modulo (tracker #715): traps on divide-by-zero and  \
       INT_MIN/-1 overflow, same policy as scalar DIVC/MODC. */                   \
    X(VDIV_I64, 1) /* vregs[rd].i64[i] = vregs[rs1].i64[i] / vregs[rs2].i64[i], traps on 0 or overflow, i<count */ \
    X(VDIV_I32, 1) /* ditto, i32 lanes */                                         \
    X(VDIV_I16, 1) /* ditto, i16 lanes */                                         \
    X(VDIV_I8, 1) /* ditto, i8 lanes */                                           \
    X(VMOD_I64, 1) /* vregs[rd].i64[i] = vregs[rs1].i64[i] % vregs[rs2].i64[i], traps on 0 or overflow, i<count */ \
    X(VMOD_I32, 1) /* ditto, i32 lanes */                                         \
    X(VMOD_I16, 1) /* ditto, i16 lanes */                                         \
    X(VMOD_I8, 1) /* ditto, i8 lanes */                                           \
    /* Comparisons (tracker #715): GCC semantics -- per-lane all-ones (-1) if    \
       true, all-zero if false, written into a same-width SIGNED integer lane.  \
       VCLT/VCLE compare the signed view; VCLTU/VCLEU compare the unsigned view  \
       (int lanes only -- float lanes are always signed-ordered). `>`/`>=` are  \
       parsed as swapped-operand `<`/`<=`, so no separate opcodes are needed. */ \
    X(VCEQ_F64, 1) /* vregs[rd].i64[i] = (vregs[rs1].f64[i] == vregs[rs2].f64[i]) ? -1 : 0, i<count */ \
    X(VCNE_F64, 1) /* ditto, != */                                                \
    X(VCLT_F64, 1) /* ditto, < */                                                 \
    X(VCLE_F64, 1) /* ditto, <= */                                                \
    X(VCEQ_F32, 1) /* vregs[rd].i32[i] = (vregs[rs1].f32[i] == vregs[rs2].f32[i]) ? -1 : 0, i<count */ \
    X(VCNE_F32, 1) /* ditto, != */                                                \
    X(VCLT_F32, 1) /* ditto, < */                                                 \
    X(VCLE_F32, 1) /* ditto, <= */                                                \
    X(VCEQ_I64, 1) /* vregs[rd].i64[i] = (vregs[rs1].i64[i] == vregs[rs2].i64[i]) ? -1 : 0, i<count */ \
    X(VCNE_I64, 1) /* ditto, != */                                                \
    X(VCLT_I64, 1) /* ditto, signed < */                                          \
    X(VCLE_I64, 1) /* ditto, signed <= */                                         \
    X(VCLTU_I64, 1) /* ditto, unsigned < */                                       \
    X(VCLEU_I64, 1) /* ditto, unsigned <= */                                      \
    X(VCEQ_I32, 1) /* vregs[rd].i32[i] = (vregs[rs1].i32[i] == vregs[rs2].i32[i]) ? -1 : 0, i<count */ \
    X(VCNE_I32, 1) /* ditto, != */                                                \
    X(VCLT_I32, 1) /* ditto, signed < */                                          \
    X(VCLE_I32, 1) /* ditto, signed <= */                                         \
    X(VCLTU_I32, 1) /* ditto, unsigned < */                                       \
    X(VCLEU_I32, 1) /* ditto, unsigned <= */                                      \
    X(VCEQ_I16, 1) /* vregs[rd].i16[i] = (vregs[rs1].i16[i] == vregs[rs2].i16[i]) ? -1 : 0, i<count */ \
    X(VCNE_I16, 1) /* ditto, != */                                                \
    X(VCLT_I16, 1) /* ditto, signed < */                                          \
    X(VCLE_I16, 1) /* ditto, signed <= */                                         \
    X(VCLTU_I16, 1) /* ditto, unsigned < */                                       \
    X(VCLEU_I16, 1) /* ditto, unsigned <= */                                      \
    X(VCEQ_I8, 1) /* vregs[rd].i8[i] = (vregs[rs1].i8[i] == vregs[rs2].i8[i]) ? -1 : 0, i<count */ \
    X(VCNE_I8, 1) /* ditto, != */                                                 \
    X(VCLT_I8, 1) /* ditto, signed < */                                           \
    X(VCLE_I8, 1) /* ditto, signed <= */                                          \
    X(VCLTU_I8, 1) /* ditto, unsigned < */                                        \
    X(VCLEU_I8, 1) /* ditto, unsigned <= */                                       \
    /* Select (tracker #715): GCC vector ?: -- nonzero-per-lane condition.       \
       rd is pre-loaded with the else-arm by codegen; VSEL then overwrites only  \
       the lanes where cond is nonzero, leaving the rest (the else values          \
       already in rd) untouched -- a read-modify-write on rd, like VINSERT_*.    \
       Still keyed by lane BYTE WIDTH (8/16/32/64), not opcode-family -- that     \
       determines the mask shape, independent of element family; the lane        \
       COUNT (2/4/8/.../64) rides in the operand as with every other op. */       \
    X(VSEL_8,  1) /* vregs[rd].i8[i]  = vregs[rcond].i8[i]  ? vregs[rthen].i8[i]  : vregs[rd].i8[i], i<count */ \
    X(VSEL_16, 1) /* ditto, i16 lanes */                                          \
    X(VSEL_32, 1) /* ditto, i32 lanes */                                          \
    X(VSEL_64, 1) /* ditto, i64 lanes */                                          \
    /* __builtin_convertvector (tracker #715): same lane count on both sides,    \
       element size changes (i32<->f32, i64<->f64). Lane count rides in the      \
       operand as with every other op. Integer conversion truncates toward zero  \
       (C cast semantics). */ \
    X(VCVT_I32_F32, 1) /* vregs[rd].i32[i] = (int32_t)vregs[rs1].f32[i], truncating, i<count */ \
    X(VCVT_F32_I32, 1) /* vregs[rd].f32[i] = (float)vregs[rs1].i32[i], i<count */  \
    X(VCVT_I64_F64, 1) /* vregs[rd].i64[i] = (int64_t)vregs[rs1].f64[i], truncating, i<count */ \
    X(VCVT_F64_I64, 1) /* vregs[rd].f64[i] = (double)vregs[rs1].i64[i], i<count */

typedef uint32_t InstrWord;
typedef uint32_t Pc;
#define CCCC_INVALID_PC UINT32_MAX

/*!
 @enum CCCC_OP
 @abstract VM instruction opcodes for the CCCC bytecode.
 @discussion
 The VM is stack-based with an accumulator `ax`. These opcodes are
 emitted by the code generator and interpreted by the VM executor.
*/
typedef enum {
#define X(NAME, OPERANDS) NAME,
    OPS_X
#undef X
    OP_COUNT,
} CCCC_OP;

// CHKT3's mode operand (#653): widened from a plain load/store bool to a
// 3-way mode since union member accesses need a "clear" behavior distinct
// from both check (load) and stamp (store). Shared between codegen.c
// (emission) and ops.c (op_CHKT3_fn, interpretation).
typedef enum { CHKT3_MODE_CHECK = 0, CHKT3_MODE_STAMP = 1, CHKT3_MODE_CLEAR = 2 } CHKT3Mode;

// One CHKT3 type shadow instance (#653, page-chunked #753), covering a
// single VM segment. `pages` is a sparse page table of TYPE_SHADOW_PAGE_SIZE-
// byte chunks (NULL entries read back as all TY_VOID -- "no info" -- at
// zero host cost); `page_count` is the length of `pages`. See
// type_shadow_ensure/type_shadow_for in ops.c for how a segment's base
// pointer and committed size pick which instance a given address maps to.
// vm->heap_shadow covers vm->heap_seg; vm->data_shadow covers vm->data_seg
// (globals, #752) -- data_seg entries are never reused, so unlike the heap
// this instance is only ever grown/stamped, never explicitly cleared aside
// from the struct-return buffer pool (see op_RETBUF_fn).
typedef struct TypeShadowSeg {
    unsigned char **pages;
    size_t page_count;
} TypeShadowSeg;

/*!
 @enum CCCCFlags
 @abstract Bitwise flags for CCCC runtime features and safety checks.
 @discussion
 These flags control memory safety features, debugging, and runtime behavior.
 Flags can be combined with bitwise OR. Some flags are convenience constants
 that represent multiple underlying flags.
*/
typedef enum {
    // Memory safety flags (bits 0-19)
    CCCC_BOUNDS_CHECKS = (1 << 0), // 0x00000001 - Array bounds checking
    CCCC_UAF_DETECTION = (1 << 1), // 0x00000002 - Use-after-free detection
    CCCC_TYPE_CHECKS = (1 << 2),   // 0x00000004 - Runtime type checking
    CCCC_UNINIT_DETECTION =
        (1 << 3), // 0x00000008 - Uninitialized variable detection
    CCCC_OVERFLOW_CHECKS = (1 << 4), // 0x00000010 - Integer overflow detection
    CCCC_STACK_CANARIES = (1 << 5),  // 0x00000020 - Stack canary protection
    CCCC_HEAP_CANARIES = (1 << 6),   // 0x00000040 - Heap canary protection
    CCCC_MEMORY_LEAK_DETECT = (1 << 7), // 0x00000080 - Memory leak detection
    CCCC_STACK_INSTR = (1 << 8), // 0x00000100 - Stack variable instrumentation
    CCCC_DANGLING_DETECT = (1 << 9),   // 0x00000200 - Dangling pointer detection
    CCCC_ALIGNMENT_CHECKS = (1 << 10), // 0x00000400 - Pointer alignment checking
    CCCC_PROVENANCE_TRACK =
        (1 << 11), // 0x00000800 - Pointer provenance tracking
    CCCC_INVALID_ARITH =
        (1 << 12), // 0x00001000 - Invalid pointer arithmetic detection
    CCCC_FORMAT_STR_CHECKS = (1 << 13), // 0x00002000 - Format string validation
    CCCC_RANDOM_CANARIES = (1 << 14),   // 0x00004000 - Random canary values
    CCCC_MEMORY_POISONING =
        (1 << 15), // 0x00008000 - Poison allocated/freed memory
    CCCC_MEMORY_TAGGING = (1 << 16), // 0x00010000 - Temporal memory tagging
    CCCC_VM_HEAP = (1 << 17),        // 0x00020000 - Force VM-managed heap
    CCCC_CFI = (1 << 18),            // 0x00040000 - Control flow integrity
    CCCC_STACK_INSTR_ERRORS =
        (1 << 19), // 0x00080000 - Stack instrumentation errors
    CCCC_ENABLE_DEBUGGER = (1 << 20), // 0x00100000 - Interactive debugger
    CCCC_THREAD_SAFETY = (1 << 21),   // 0x00200000 - Threading safety diagnostics
    CCCC_NO_DEBUG_ON_CRASH =
        (1 << 22), // 0x00400000 - Suppress auto-debugger-on-crash
    CCCC_FMA =
        (1 << 23), // 0x00800000 - FMA fusion codegen (--fma)
    CCCC_FFI_ERRORS_FATAL =
        (1 << 24), // 0x01000000 - Fatal FFI errors (--ffi-errors-fatal)

    // Convenience flag combinations
    CCCC_POINTER_SANITIZER =
        (CCCC_BOUNDS_CHECKS | CCCC_UAF_DETECTION | CCCC_TYPE_CHECKS),
    CCCC_ALL_SAFETY = 0x000FFFFF, // All safety features (bits 0-19)

    // Preset safety levels (use with -S0/-S1/-S2/-S3 or
    // --safety=none/basic/standard/max)
    CCCC_SAFETY_BASIC =
        (CCCC_STACK_CANARIES | CCCC_HEAP_CANARIES | CCCC_MEMORY_LEAK_DETECT |
         CCCC_OVERFLOW_CHECKS | CCCC_FORMAT_STR_CHECKS | CCCC_VM_HEAP),
    CCCC_SAFETY_STANDARD =
        (CCCC_POINTER_SANITIZER | CCCC_STACK_CANARIES | CCCC_HEAP_CANARIES |
         CCCC_MEMORY_LEAK_DETECT | CCCC_OVERFLOW_CHECKS | CCCC_UNINIT_DETECTION |
         CCCC_FORMAT_STR_CHECKS | CCCC_MEMORY_POISONING | CCCC_VM_HEAP),
    CCCC_SAFETY_MAX =
        (CCCC_ALL_SAFETY | CCCC_RANDOM_CANARIES | CCCC_STACK_INSTR_ERRORS),

    // Union of every bit any safety preset (BASIC/STANDARD/MAX) can touch.
    // Used by `#pragma cccc config(safety = N)` to know which bits it is
    // allowed to clear/set without disturbing unrelated flags.
    CCCC_SAFETY_PRESET_BITS =
        (CCCC_SAFETY_BASIC | CCCC_SAFETY_STANDARD | CCCC_SAFETY_MAX),

    // VM heap is auto-enabled when any of these flags are set
    CCCC_VM_HEAP_TRIGGERS =
        (CCCC_VM_HEAP | CCCC_HEAP_CANARIES | CCCC_MEMORY_LEAK_DETECT |
         CCCC_UAF_DETECTION | CCCC_POINTER_SANITIZER | CCCC_BOUNDS_CHECKS |
         CCCC_MEMORY_TAGGING),

    // Pointer validity checks
    CCCC_POINTER_CHECKS = (CCCC_UAF_DETECTION | CCCC_BOUNDS_CHECKS |
                          CCCC_DANGLING_DETECT | CCCC_MEMORY_TAGGING),
} CCCCFlags;

/*!
 @enum CCCCWarning
 @abstract Bitwise flags for compiler warning categories.
 @discussion
 These flags control suppressible compiler diagnostics. They are separate from
 CCCCFlags because the warning set can grow independently of runtime safety
 flags and uses a 64-bit mask.
*/
typedef enum {
    CCCC_WARN_UNUSED = (1ULL << 0),
    CCCC_WARN_IMPLICIT_FUNCTION_DECLARATION = (1ULL << 1),
    CCCC_WARN_IMPLICIT_INT = (1ULL << 2),
    CCCC_WARN_RETURN_TYPE = (1ULL << 3),
    CCCC_WARN_SHADOW = (1ULL << 4),
    CCCC_WARN_FORMAT = (1ULL << 5),
    CCCC_WARN_CONVERSION = (1ULL << 6),
    CCCC_WARN_SIGN_COMPARE = (1ULL << 7),
    CCCC_WARN_POINTER_ARITH = (1ULL << 8),
    CCCC_WARN_PEDANTIC = (1ULL << 9),
    CCCC_WARN_DEPRECATED = (1ULL << 10),
    CCCC_WARN_CPP = (1ULL << 11),
    CCCC_WARN_EXTRA_TOKENS = (1ULL << 12),
    CCCC_WARN_LARGE_FILE_EMBED = (1ULL << 13),
    CCCC_WARN_CCCC_MACRO = (1ULL << 14),
    CCCC_WARN_COMPTIME_BLOCK_LEAK = (1ULL << 15), // unclosed #pragma cccc comptime begin in included file
    // Conversion sub-categories (CCCC_WARN_CONVERSION is integer narrowing)
    CCCC_WARN_SIGN_CONVERSION = (1ULL << 16),  // signed/unsigned mismatch on assign/arg/return
    CCCC_WARN_FLOAT_CONVERSION = (1ULL << 17), // float<->int or float narrowing
    CCCC_WARN_IGNORED_FEATURES = (1ULL << 18), // parsed-but-ignored features (_Atomic, TLS, etc.)
    CCCC_WARN_ATTRIBUTES       = (1ULL << 19), // unknown attributes
    CCCC_WARN_NODISCARD        = (1ULL << 20), // [[nodiscard]] return value discarded
    CCCC_WARN_FALLTHROUGH      = (1ULL << 21), // switch case falls through without [[fallthrough]]
    CCCC_WARN_STATIC_ARRAY_SIZE = (1ULL << 22), // [static N] array param: argument too small
    CCCC_WARN_STRICT_PROTOTYPES = (1ULL << 23), // () without void in pre-C23
    CCCC_WARN_DISCARDED_QUALIFIERS = (1ULL << 24), // const/volatile/restrict discarded in pointer assignment

    // Ticket #410: lower-priority / safety-overlap flags
    CCCC_WARN_NULL_DEREFERENCE    = (1ULL << 25), // no-op; runtime safety covers at -S2/-S3
    CCCC_WARN_RESTRICT            = (1ULL << 26), // no-op; runtime safety covers
    CCCC_WARN_ARRAY_BOUNDS        = (1ULL << 27), // no-op; runtime safety covers at -S2/-S3
    CCCC_WARN_STRINGOP_OVERFLOW   = (1ULL << 28), // no-op; runtime safety covers
    CCCC_WARN_STRINGOP_TRUNCATION = (1ULL << 29), // no-op; runtime safety covers
    CCCC_WARN_DUPLICATED_BRANCHES = (1ULL << 30), // identical then/else bodies
    CCCC_WARN_DUPLICATED_COND     = (1ULL << 31), // same condition repeated in if/else-if chain
    CCCC_WARN_UNUSED_VALUE        = (1ULL << 32), // expression result discarded, no side effects
    CCCC_WARN_MULTICHAR           = (1ULL << 33), // multi-character character constant
    CCCC_WARN_MAIN                = (1ULL << 34), // suspicious main() signature
    CCCC_WARN_SWITCH_DEFAULT      = (1ULL << 35), // switch with no default: label
    CCCC_WARN_SWITCH_BOOL         = (1ULL << 36), // switch on _Bool / bool expression
    CCCC_WARN_FLOAT_EQUAL         = (1ULL << 37), // == or != on floating-point operands
    CCCC_WARN_SHIFT_NEGATIVE_VALUE    = (1ULL << 38), // shift amount is a negative constant
    CCCC_WARN_SHIFT_OVERFLOW          = (1ULL << 39), // shift amount >= promoted type bit-width
    CCCC_WARN_LOGICAL_OP              = (1ULL << 40), // constant operand in && or ||
    CCCC_WARN_TAUTOLOGICAL_COMPARE    = (1ULL << 41), // comparison always true or always false
    CCCC_WARN_SIZEOF_POINTER_MEMACCESS = (1ULL << 42), // sizeof(ptr) passed as size to mem fn
    CCCC_WARN_SWITCH                = (1ULL << 43), // missing enum case, no default
    CCCC_WARN_SWITCH_ENUM           = (1ULL << 44), // missing enum case, even with default
    CCCC_WARN_ENUM_COMPARE          = (1ULL << 45), // == / != / < etc. between different named enums
    CCCC_WARN_OLD_STYLE_DEFINITION  = (1ULL << 46), // K&R-style function parameter declarations

    // Ticket #469: pointer safety, cast quality, and prototype flags
    CCCC_WARN_INCOMPATIBLE_POINTER_TYPES = (1ULL << 47), // implicit pointer type mismatch (not void*)
    CCCC_WARN_CAST_QUAL                  = (1ULL << 48), // explicit cast drops const/volatile/restrict
    CCCC_WARN_CAST_ALIGN                 = (1ULL << 49), // explicit cast raises pointer alignment requirement
    CCCC_WARN_MISSING_PROTOTYPES         = (1ULL << 50), // external fn defined without prior full prototype
    CCCC_WARN_MISSING_DECLARATIONS       = (1ULL << 51), // external fn defined without any prior declaration

    // Ticket #471: misc analysis flags
    CCCC_WARN_REDUNDANT_DECLS  = (1ULL << 52), // same-linkage declaration seen twice in same scope
    CCCC_WARN_OVERRIDE_INIT    = (1ULL << 53), // later designator overrides earlier initializer
    CCCC_WARN_UNUSED_MACROS    = (1ULL << 54), // #define that is never expanded
    CCCC_WARN_NONNULL          = (1ULL << 55), // null passed to a nonnull param / returned from returns_nonnull
    CCCC_WARN_MAYBE_NONNULL    = (1ULL << 56), // maybe-null (post-branch-merge) value reaching a nonnull param / returns_nonnull return; opt-in, not in -Wall/-Wextra (#687)
    CCCC_WARN_SENTINEL         = (1ULL << 57), // missing/non-literal NULL terminator in a call to a sentinel-marked variadic function (#658)
    CCCC_WARN_DESIGNATED_INIT  = (1ULL << 58), // positional member initializer of a struct declared __attribute__((designated_init)); opt-in, not in -Wall/-Wextra (#659)

    // Umbrella for all three conversion sub-types; -Wconversion enables this group.
    CCCC_WARN_CONVERSION_GROUP = CCCC_WARN_CONVERSION |
                                CCCC_WARN_SIGN_CONVERSION |
                                CCCC_WARN_FLOAT_CONVERSION,

CCCC_WARN_ALL = CCCC_WARN_UNUSED |
                   CCCC_WARN_IMPLICIT_FUNCTION_DECLARATION |
                   CCCC_WARN_IMPLICIT_INT |
                   CCCC_WARN_RETURN_TYPE |
                   CCCC_WARN_SHADOW |
                   CCCC_WARN_FORMAT |
                   CCCC_WARN_CONVERSION |
                   CCCC_WARN_SIGN_COMPARE |
                   CCCC_WARN_POINTER_ARITH |
                   CCCC_WARN_PEDANTIC |
                   CCCC_WARN_DEPRECATED |
                   CCCC_WARN_CPP |
                   CCCC_WARN_EXTRA_TOKENS |
                   CCCC_WARN_LARGE_FILE_EMBED |
                   CCCC_WARN_CCCC_MACRO |
                   CCCC_WARN_IGNORED_FEATURES |
                   CCCC_WARN_ATTRIBUTES |
                   CCCC_WARN_NODISCARD |
                   CCCC_WARN_STATIC_ARRAY_SIZE |
                   CCCC_WARN_DISCARDED_QUALIFIERS |
                   CCCC_WARN_MULTICHAR |
                   CCCC_WARN_MAIN |
                   CCCC_WARN_SWITCH_DEFAULT |
                   CCCC_WARN_SWITCH_BOOL |
                   CCCC_WARN_FLOAT_EQUAL |
                   CCCC_WARN_SHIFT_NEGATIVE_VALUE |
                   CCCC_WARN_SHIFT_OVERFLOW |
                   CCCC_WARN_LOGICAL_OP |
                   CCCC_WARN_TAUTOLOGICAL_COMPARE |
                   CCCC_WARN_SIZEOF_POINTER_MEMACCESS |
                   CCCC_WARN_SWITCH |
                   CCCC_WARN_ENUM_COMPARE |
                   CCCC_WARN_INCOMPATIBLE_POINTER_TYPES |
                   CCCC_WARN_OVERRIDE_INIT |
                   CCCC_WARN_NONNULL |
                   CCCC_WARN_SENTINEL,
    CCCC_WARN_EXTRA = CCCC_WARN_SHADOW |
                      CCCC_WARN_SIGN_COMPARE |
                      CCCC_WARN_CONVERSION |
                      CCCC_WARN_POINTER_ARITH |
                      CCCC_WARN_FALLTHROUGH |
                      CCCC_WARN_STRICT_PROTOTYPES |
                      CCCC_WARN_OLD_STYLE_DEFINITION |
                      CCCC_WARN_REDUNDANT_DECLS |
                      CCCC_WARN_COMPTIME_BLOCK_LEAK,
} CCCCWarning;

/*!
 @struct HashEntry
 @abstract Simple key/value bucket used by the project's HashMap.
 @field key Null-terminated string key.
 @field keylen Length of the key in bytes.
 @field val Pointer to the stored value.
*/
typedef struct HashEntry {
    char *key;
    int keylen;
    void *val;
} HashEntry;

/*!
 @struct HashMap
 @abstract Lightweight open-addressing hashmap used for symbol tables,
           macros, and other small maps.
 @field buckets Pointer to an array of HashEntry buckets.
 @field capacity Number of buckets allocated.
 @field used Number of occupied buckets, including tombstones.
*/
typedef struct HashMap {
    HashEntry *buckets;
    int capacity;
    int used;
} HashMap;

/*!
 @struct File
 @abstract Represents the contents and metadata of a source file.
 @field name Original filename string.
 @field file_no Unique numeric id for the file.
 @field contents File contents as a NUL-terminated buffer.
 @field display_name Optional name emitted by a `#line` directive.
 @field line_delta Line number delta applied for `#line` handling.
*/
typedef struct File {
    char *name;
    int file_no;
    char *contents;

    // For #line directive
    char *display_name;
    int line_delta;

    bool is_system_header; // true when included via <...> from system search paths
} File;

/*!
 @struct Relocation
 @abstract A relocation record for a global variable initializer that
           references another global symbol.
 @discussion
 Each relocation describes a pointer-sized slot within a global's
 initializer data that must be patched with the address of another
 global (or label) when codegen finalizes the data segment.
*/
typedef struct Relocation {
    struct Relocation *next;
    int offset;
    char **label;
    long addend;
} Relocation;

/*!
 @struct StringArray
 @abstract Dynamic array of strings used for include paths and similar
           small lists.
 @field data Pointer to N entries of char* strings.
 @field capacity Allocated capacity of the array.
 @field len Current length (number of strings stored).
*/
typedef struct StringArray {
    char **data;
    int capacity;
    int len;
} StringArray;

/*!
 @struct EnumConstant
 @abstract Represents an enumerator constant within an enum type.
 @field name Name of the enumerator.
 @field value Integer value of the enumerator (int64_t to support C23 wide underlying types).
 @field next Pointer to the next enumerator in the linked list.
*/
typedef struct EnumConstant {
    char *name;
    int64_t value;
    struct EnumConstant *next;
} EnumConstant;

/*!
 @struct Hideset
 @abstract Represents a set of macro names that have been hidden to
           prevent recursive macro expansion.
*/
typedef struct Hideset {
    struct Hideset *next;
    char *name;
} Hideset;

/*!
 @enum TokenKind
 @abstract Kinds of lexical tokens produced by the tokenizer and
           used by the preprocessor and parser.
*/
typedef enum {
    TK_IDENT,   // Identifiers
    TK_PUNCT,   // Punctuators
    TK_KEYWORD, // Keywords
    TK_STR,     // String literals
    TK_BACKTICK_STR, // Raw quasi-quote fragment between ` / ${ / }
    TK_NUM,     // Numeric literals
    TK_PP_NUM,  // Preprocessing numbers
    TK_MACRO_SCOPE_PUSH, // Synthetic: push macro-table snapshot (#283); never from lexer
    TK_MACRO_SCOPE_POP,  // Synthetic: pop/restore macro-table snapshot (#283)
    TK_EOF,     // End-of-file markers
} TokenKind;

typedef struct Type Type;

/*!
 @struct Token
 @abstract Token produced by the lexer or by macro expansion.
 @field kind Token kind (see TokenKind).
 @field loc Pointer into the source buffer where the token text begins.
 @field len Number of characters in the token text.
 @field str For string literals: pointer to the unescaped contents.
*/
typedef struct Token {
    TokenKind kind;     // Token kind
    struct Token *next; // Next token
    int64_t val;        // If kind is TK_NUM, its value
    long double fval;   // If kind is TK_NUM, its value
    char *loc;          // Token location
    int len;            // Token length
    Type *ty;           // Used if TK_NUM or TK_STR
    char *str;          // TK_STR contents, or TK_BACKTICK_STR fragment; NUL-terminated
    char *wide_digits;  // wb/uwb _BitInt literal: full-precision digit text
                         // (no prefix/suffix/separators) when bit_width > 64
    int wide_base;      // base (2/8/10/16) for wide_digits, else unused

    File *file;       // Source location
    char *filename;   // Filename
    int line_no;      // Line number
    int col_no;       // Column number (1-based)
    int line_delta;   // Line number
    bool at_bol;      // True if this token is at beginning of line
    bool has_space;   // True if this token follows a space character
    Hideset *hideset; // For macro expansion
    struct Token
        *origin; // If this is expanded from a macro, the original token

    // Effective diagnostic state stamped by the preprocessor.
    // Bit 63 is a sentinel: if set, bits 0-62 are the active warning mask
    // at the point this token was emitted (overrides vm->compiler.warnings).
    uint64_t diag_warnings; // effective CCCCWarning mask (bit63 = stamped)
    uint64_t diag_werror;   // effective warning_errors mask (bit63 = stamped)
} Token;

/*!
 @enum TypeKind
 @abstract Kind tag for the `Type` structure describing C types.
*/
typedef enum {
    TY_VOID = 0,
    TY_BOOL = 1,
    TY_CHAR = 2,
    TY_SHORT = 3,
    TY_INT = 4,
    TY_LONG = 5,
    TY_FLOAT = 6,
    TY_DOUBLE = 7,
    TY_LDOUBLE = 8,
    TY_ENUM = 9,
    TY_PTR = 10,
    TY_FUNC = 11,
    TY_ARRAY = 12,
    TY_VLA = 13, // variable-length array
    TY_STRUCT = 14,
    TY_UNION = 15,
    TY_ERROR = 16, // error type for recovery
    TY_BLOCK = 17, // Apple blocks (closures)
    TY_COMPLEX = 18, // C99 complex scalar, base is float/double/long double
    TY_NULLPTR_T = 19, // C23 nullptr_t
    TY_BITINT = 20,    // C23 _BitInt(N), N in [1,256]; N>64 uses multi-word storage
    TY_AUTO = 21,      // C23 auto type-inference sentinel (never reaches codegen)
    TY_VECTOR = 22,    // GNU __attribute__((vector_size(N))); base=element type,
                       // vec_len=lane count, size=N bytes (tracker #72)
} TypeKind;

typedef struct Node Node;
typedef struct Obj Obj;
typedef struct Scope Scope;
typedef struct ThreadRecord ThreadRecord;
typedef struct PthreadKeyRecord PthreadKeyRecord;
typedef struct PthreadState PthreadState;

typedef enum {
    CCCC_EMIT_SOURCE,
    CCCC_EMIT_OBJECT,
} EmitEventKind;

typedef struct EmitEvent {
    struct EmitEvent *next;
    EmitEventKind kind;
    char *source;
    Obj *obj;
} EmitEvent;

/*!
 @struct Type
 @abstract Central representation of a C type in the compiler.
 @field kind One of TypeKind indicating the category of this type.
 @field size sizeof() value in bytes.
 @field align Alignment requirement in bytes.
 @field base For pointer/array types: the referenced element/base type.
*/
struct Type {
    TypeKind kind;
    int size;            // sizeof() value
    int align;           // alignment
    bool is_unsigned;    // unsigned or signed
    bool is_atomic;      // true if _Atomic
    bool is_const;       // true if const-qualified
    bool is_volatile;    // true if volatile-qualified
    bool is_restrict;    // true if restrict-qualified
    struct Type *origin; // for type compatibility check

    // Pointer-to or array-of type. We intentionally use the same member
    // to represent pointer/array duality in C.
    //
    // In many contexts in which a pointer is expected, we examine this
    // member instead of "kind" member to determine whether a type is a
    // pointer or not. That means in many contexts "array of T" is
    // naturally handled as if it were "pointer to T", as required by
    // the C spec.
    struct Type *base;

    // Declaration
    Token *name;
    Token *name_pos;
    char *asm_label; // GNU asm("symbol") label for function declarations

    // Array
    int array_len;
    int static_min;   // [static N] minimum required elements; 0 = no constraint

    // GNU vector_size vector (TY_VECTOR): lane count. `base` is the element
    // type, `size` is the total byte size (element size * vec_len).
    int vec_len;

    // Variable-length array
    Node *vla_len; // # of elements
    Obj *vla_size; // sizeof() value

    // Struct
    struct Member *members;
    bool is_flexible;
    bool is_packed;
    bool designated_init; // __attribute__((designated_init)): all initializers of this struct type must be designated (#659)

    // Enum
    EnumConstant *enum_constants;
    struct Type *enum_base_type;  // C23: underlying type (NULL = default int)
    Token *enum_tag;              // tag name token (survives declarator name-overwrite)

    // Function type
    struct Type *return_ty;
    struct Type *params;
    bool is_variadic;
    struct Type *next;

    // Declaration attributes used by semantic warnings
    bool is_maybe_unused;
    bool is_deprecated;
    bool is_noreturn;
    bool is_nodiscard;
    bool is_pure;       // __attribute__((pure)): no side effects, may read globals
    bool is_func_const; // __attribute__((const)): no side effects, no global reads
    char *deprecated_msg;
    char *nodiscard_msg;
    char *attr_error_msg;   // __attribute__((error("msg"))): error if callee is called
    char *attr_warning_msg; // __attribute__((warning("msg"))): warn if callee is called
    struct CustomAttrUse *custom_attrs;
    struct Obj *cleanup_fn; // transport: __attribute__((cleanup(fn))); copied to Obj by new_var()

    // Per-function optimization level (__attribute__((optimize)) / [[cccc::optimize]])
    int  fn_optimize_level; // requested opt level (0–4); only valid if fn_optimize_set
    bool fn_optimize_set;   // true if an optimize attribute was present

    // _BitInt(N): bit width for TY_BITINT types
    int bit_width;

    // _Decimal32/64/128: binary float placeholder (not real decimal encoding)
    bool is_decimal;

    // Format string validation (__attribute__((format(...))))
    int format_style;          // 0=none, 1=printf, 2=scanf
    int format_string_index;   // 1-based index of format string arg
    int format_fmt_first_arg;  // 1-based index of first variadic arg to check

    // Nonnull argument / return checking (__attribute__((nonnull / returns_nonnull)))
    bool     nonnull_all;    // bare nonnull: every pointer parameter is non-null
    uint64_t nonnull_mask;   // 1-based arg indices marked non-null (bit i-1); >64 args ignored
    bool     returns_nonnull;

    // NULL-terminated variadic argument check (__attribute__((sentinel[(N)])))
    bool is_sentinel;    // true if the function requires a NULL sentinel arg
    int  sentinel_pos;   // trailing non-sentinel args allowed before the NULL (0 = last arg)

    // __attribute__((alloc_size(n[,m]))): 1-based arg indices (0 = unset).
    // Generalizes #642's name-based malloc-family detection: any function
    // declared with this attribute participates in __builtin_object_size
    // heap-allocation sizing (#649).
    int  alloc_size_idx;   // size arg, or first factor for the 2-arg form
    int  alloc_size_idx2;  // second factor (calloc-style product), 0 if 1-arg form

    // __attribute__((malloc)): noalias/fresh-pointer hint (informational;
    // not wired to any optimization or nonnull inference yet -- #649 followup).
    bool is_malloc;

    // __attribute__((constructor[(priority)])) / ((destructor[(priority)]))
    bool is_constructor; // run before main(), ordered by init_priority
    bool is_destructor;  // run after main() returns normally, reverse order
    int  init_priority;  // CCCC_NO_INIT_PRIORITY unless an explicit priority was given
};

// Sentinel meaning "no explicit constructor/destructor priority given" — such
// functions form the default-priority group, which GCC runs last for
// constructors and first for destructors relative to explicitly prioritised ones.
#define CCCC_NO_INIT_PRIORITY (-1)

/*!
 @struct Member
 @abstract Member (field) descriptor for struct and union types.
 @field ty Type of the member.
 @field name Token pointing to the member identifier.
 @field offset Byte offset of the member within its aggregate.
*/
typedef struct Member {
    struct Member *next;
    Type *ty;
    Token *tok; // for error message
    Token *name;
    int idx;
    int align;
    int offset;

    // Bitfield
    bool is_bitfield;
    int bit_offset;
    int bit_width;
} Member;

/*!
 @enum NodeKind
 @abstract Kinds of AST nodes produced by the parser.
*/
typedef enum {
    ND_NULL_EXPR = 0,      // Do nothing
    ND_ADD = 1,            // +
    ND_SUB = 2,            // -
    ND_MUL = 3,            // *
    ND_DIV = 4,            // /
    ND_NEG = 5,            // unary -
    ND_MOD = 6,            // %
    ND_BITAND = 7,         // &
    ND_BITOR = 8,          // |
    ND_BITXOR = 9,         // ^
    ND_SHL = 10,           // <<
    ND_SHR = 11,           // >>
    ND_EQ = 12,            // ==
    ND_NE = 13,            // !=
    ND_LT = 14,            // <
    ND_LE = 15,            // <=
    ND_ASSIGN = 16,        // =
    ND_COND = 17,          // ?:
    ND_COMMA = 18,         // ,
    ND_MEMBER = 19,        // . (struct member access)
    ND_ADDR = 20,          // unary &
    ND_DEREF = 21,         // unary *
    ND_NOT = 22,           // !
    ND_BITNOT = 23,        // ~
    ND_LOGAND = 24,        // &&
    ND_LOGOR = 25,         // ||
    ND_RETURN = 26,        // "return"
    ND_IF = 27,            // "if"
    ND_FOR = 28,           // "for" or "while"
    ND_DO = 29,            // "do"
    ND_SWITCH = 30,        // "switch"
    ND_CASE = 31,          // "case"
    ND_BLOCK = 32,         // { ... }
    ND_GOTO = 33,          // "goto"
    ND_GOTO_EXPR = 34,     // "goto" labels-as-values
    ND_LABEL = 35,         // Labeled statement
    ND_LABEL_VAL = 36,     // [GNU] Labels-as-values
    ND_FUNCALL = 37,       // Function call
    ND_EXPR_STMT = 38,     // Expression statement
    ND_STMT_EXPR = 39,     // Statement expression
    ND_VAR = 40,           // Variable
    ND_VLA_PTR = 41,       // VLA designator
    ND_NUM = 42,           // Integer
    ND_CAST = 43,          // Type cast
    ND_MEMZERO = 44,       // Zero-clear a stack variable
    ND_ASM = 45,           // "asm"
    ND_CAS = 46,           // Atomic compare-and-swap
    ND_EXCH = 47,          // Atomic exchange
    ND_FRAME_ADDR = 48,    // __builtin_frame_address(0) - returns base pointer
    ND_BLOCK_LITERAL = 49, // Block literal ^{ ... }
    ND_BLOCK_CALL = 50,    // Block invocation
    ND_MACRO_CALL = 51, // Pragma macro invocation (deferred until macro pass)
    ND_COMPLEX = 52,    // Native complex construction/projection helper
    ND_UNREACHABLE = 53, // __builtin_unreachable() - code path that must not execute
    ND_BITOP = 54,      // Bit-manipulation builtins; val = (op<<8)|bit_width, lhs = arg
    ND_OVERFLOW_ARITH = 55, // Checked arithmetic; val = 0/1/2 (add/sub/mul), lhs=a, rhs=b, cas_addr=ptr
    ND_INIT_SPLICE = 56,    // Deferred compound-literal $@k splice; expanded by quote_substitute
                            // var=lvar, lhs=ND_VAR($@k placeholder)
    ND_ALOAD = 57,          // Atomic load via __builtin_atomic_load; lhs=addr ptr
    ND_ASTORE = 58,         // Atomic store via __builtin_atomic_store; lhs=addr ptr, rhs=value
    ND_RETURN_ADDR = 59,    // __builtin_return_address(n) - returns return address n frames up
                            // val = level (compile-time constant); ty = void*
    ND_DYNOBJ_SIZE = 60,    // __builtin_dynamic_object_size(ptr, type) — runtime heap size.
                            // lhs = ptr expr (evaluated at runtime); val = type arg (0-3);
                            // ty = size_t (ulong).  Emits DYNOBJSZ opcode.
    ND_CONVERTVECTOR = 61,  // __builtin_convertvector(expr, type) (tracker #715).
                            // lhs = source vector expr (already add_type'd at
                            // parse time); ty = target vector type (preset at
                            // parse time, like ND_CAST via new_cast) --
                            // cross-lane-family element conversion (e.g.
                            // int32 lanes <-> float32 lanes), NOT a
                            // bit-reinterpret cast.
} NodeKind;

// Linked list of locals with __attribute__((cleanup(fn))) in one block scope.
// Used by ND_BLOCK (parse) and CleanupScopeEntry (codegen) to emit LIFO calls.
typedef struct CleanupVar {
    struct Obj *var;          // local variable with cleanup
    struct Obj *cleanup_fn;   // void fn(T *) to call at scope exit
    struct CleanupVar *next;
} CleanupVar;

// Parse-time ancestry node for cleanup scopes. Unlike the flat
// cleanup_scope_depth counter, the parent chain gives each cleanup scope an
// identity so resolve_goto_labels can compute the lowest common ancestor of a
// goto and its label and distinguish ancestors from same-depth siblings.
typedef struct CleanupChainNode {
    int depth;                        // == cleanup_scope_depth of this scope
    struct CleanupChainNode *parent;  // enclosing cleanup scope (NULL at fn level)
} CleanupChainNode;

/*!
 @struct Node
 @abstract Represents a node in the parser's abstract syntax tree.
 @field kind Node kind (see NodeKind).
 @field ty Resolved type of this node (after semantic analysis).
 @field lhs Left-hand side child (when applicable).
 @field rhs Right-hand side child (when applicable).
*/
struct Node {
    NodeKind kind;     // Node kind
    struct Node *next; // Next node
    Type *ty;          // Type, e.g. int or pointer to int
    Token *tok;        // Representative token

    struct Node *lhs; // Left-hand side
    struct Node *rhs; // Right-hand side

    // "if" or "for" statement
    struct Node *cond;
    struct Node *then;
    struct Node *els;
    struct Node *init;
    struct Node *inc;

    // "break" and "continue" labels
    char *brk_label;
    char *cont_label;

    // Block or statement expression
    struct Node *body;

    // Struct member access
    Member *member;

    // Function call
    Type *func_ty;
    struct Node *args;
    bool pass_by_stack;
    bool has_splice_arg; // deferred arity/cast check needed after quote_substitute
    Obj *ret_buffer;

    // Goto or labeled statement, or labels-as-values
    char *label;
    char *unique_label;
    struct Node *goto_next;
    bool label_used;
    bool label_maybe_unused;
    bool is_fallthrough; // [[fallthrough]] on a null statement
    bool is_sizeof_ptr_expr; // ND_NUM from sizeof(pointer_type) — for -Wsizeof-pointer-memaccess
    int cleanup_target_depth; // for ND_GOTO (break/continue): cleanup_scope_depth of target
    CleanupChainNode *cleanup_chain; // ND_GOTO/ND_LABEL: innermost active cleanup scope (NULL if none)

    // ND_BLOCK: cleanup vars declared in this scope (declaration order); codegen emits LIFO
    CleanupVar *cleanup_vars;
    int cleanup_scope_depth; // parse-time cleanup_scope_depth when this block was built

    // Switch
    struct Node *case_next;
    struct Node *default_case;

    // Case
    long begin;
    long end;

    // "asm" string literal
    char *asm_str;

    // Atomic compare-and-swap
    struct Node *cas_addr;
    struct Node *cas_old;
    struct Node *cas_new;

    // Atomic op= operators
    Obj *atomic_addr;
    struct Node *atomic_expr;

    // Variable
    Obj *var;
    struct Node *init_tail; // Tail expressions after a deferred initializer splice
    int init_start_index;   // Positional field/element index for ND_INIT_SPLICE
    bool init_inferred_array; // True when array length must be finalized post-splice

    // Numeric literal
    int64_t val;
    long double fval;
    char *wide_digits; // wb/uwb _BitInt literal digit text, when bit_width > 64
    int wide_base;      // base (2/8/10/16) for wide_digits, else unused

    // Block literal (Apple blocks extension)
    Obj *block_fn;          // Synthetic function for block's body
    Obj **block_captures;   // Array of captured variables
    int num_block_captures; // Number of captured variables
    Obj *block_desc_var;    // Stack slot in enclosing frame for descriptor storage

    // Pragma macro call (ND_MACRO_CALL)
    char *macro_name;    // Name of pragma macro to invoke
    int macro_arg_count; // Number of arguments
    Scope *macro_scope;  // Parser scope active at the macro call site
};

/*!
 @struct Obj
 @abstract Represents a C object: either a variable (global/local) or a
           function. The parser and code generator use Obj for symbol
           and storage tracking.
 @field name Identifier name of the object.
 @field ty Type of the object.
 @field is_local True for local (stack) variables; false for globals.
 @field offset For local variables: stack offset. For globals/functions
               some fields (like code_addr) are used instead.
 @field is_function True when this Obj represents a function.
 @field code_addr For functions compiled to VM bytecode: start address
                 in the text segment.
*/
struct Obj {
    struct Obj *next;
    char *name;    // Variable name
    char *display_name; // Source identifier when storage uses a synthetic name
    char *asm_label; // External symbol name from GNU asm("symbol") label
    Type *ty;      // Type
    Token *tok;    // representative token
    bool is_local; // local or global/function
    int align;     // alignment
    bool is_used;
    bool is_maybe_unused;
    bool is_deprecated;
    bool is_noreturn;
    bool is_nodiscard;
    bool is_pure;
    bool is_func_const;
    bool may_return_null; // #688: function has a provable null-returning path (whole-TU summary)
    bool always_returns_null; // #692: every reachable return in the function is provably null
    bool is_local_symbol;
    char *deprecated_msg;
    char *nodiscard_msg;
    char *attr_error_msg;   // __attribute__((error("msg")))
    char *attr_warning_msg; // __attribute__((warning("msg")))

    // Per-function optimization level (GCC-style: attribute overrides global -O)
    int  fn_optimize_level; // requested opt level (0–4); only valid if fn_optimize_set
    bool fn_optimize_set;   // true if an optimize attribute was present

    // Local variable
    int offset;
    bool is_param;    // true if this is a function parameter
    bool is_captured; // true if accessed by a nested function (for optimization
                      // hints)
    struct Obj *cleanup_fn; // non-NULL: void fn(T*) called at scope exit (__attribute__((cleanup)))
    int cleanup_fp_retval_offset; // stack offset for float retval save slot (set by assign_stack_offsets)

    // Escape analysis for LEA3 epoch-recording pruning (#676). Set by a
    // post-parse scan (mark_addr_escapes in parse.c) whenever this local's
    // address is provably observed escaping its creating frame -- passed as
    // a call argument, returned, or stored into a pointer/aggregate lvalue
    // (walking through interior addressing, e.g. &arr[i] marks arr, and
    // through cast/comma/ternary/chained-assign wrappers). Defaults to false
    // (safe: LEA3 records into vm->stack_ptr_epochs unless this is proven
    // true) -- any address-of this scan fails to classify simply stays
    // recorded, so under-approximation here can only cost a wasted hashmap
    // entry, never reintroduce the #673 false negative. See docs/SAFETY.md.
    bool addr_escapes;

    // __builtin_object_size: constant malloc-family allocation tracking (#642).
    // Only honored when the pointer is assigned exactly once (its declaration
    // initializer) and never has its address taken; see resolve_objsize_queries.
    int   objsize_alloc;       // bytes from a const malloc-family initializer;
                               // meaningless when objsize_derived_from is set
    bool  objsize_has_alloc;   // true if this var is alloc-tracked, either
                               // directly (objsize_alloc) or derived
                               // (objsize_derived_from) (#642, #700)
    bool  objsize_unsafe;      // true if reassigned or address-taken in scope
    Node *objsize_init_assign; // the initializer ND_ASSIGN node (exempt from poisoning)
    struct Obj *objsize_decl_fn; // the function this var was declared in; a
                                 // query is only ever registered when it's
                                 // asked from this same function — a query
                                 // inside a nested function/block on an
                                 // enclosing-scope pointer resolves (and gets
                                 // frozen) before the enclosing function's own
                                 // poison scan can see a later reassignment,
                                 // so such queries must stay conservative
    // #700: `q = p + const` initializer tracking, where p is itself
    // alloc-tracked (directly or transitively). objsize_derived_from chains
    // resolve at query time (see objsize_effective_remaining), so a var may
    // be derived from another derived var. NULL means this var's size comes
    // straight from objsize_alloc (the #642/#649 direct case).
    struct Obj *objsize_derived_from;
    int   objsize_derived_offset; // byte offset from objsize_derived_from

    // Global variable or function
    bool is_function;
    bool is_definition;
    bool is_static;
    bool is_builtin_alloca; // the builtin alloca() used to lower VLAs (#588)
    bool is_constexpr;
    bool is_implicit; // synthesized by an implicit function declaration
    bool is_macro_generated; // true if created by a #pragma macro via $function/$global_var
    bool is_splice_placeholder; // true for $@k vars synthesised by quote_core

    // Global variable
    bool is_tentative;
    bool is_tls;
    bool is_compound_literal; // Anonymous global synthesized to hold a
                              // compound literal's value (postfix's
                              // compound-literal branch). GCC/clang extend
                              // constant-initializer folding to a compound
                              // literal's own (constant) elements but not to
                              // an arbitrary global reference by value, so
                              // write_gvar_data's #720 splice-in path gates
                              // on this flag rather than merely having
                              // init_data.
    char *init_data;
    Relocation *rel;
    Node *init_expr; // For constexpr: AST of initializer expression
    void *constexpr_init; // For constexpr: private Initializer tree; also used
                          // temporarily for pending macro-init Initializer trees
    bool has_pending_macro_init; // gvar init deferred: init expr contains ND_MACRO_CALL

    // Function
    bool is_inline;
    Obj *params;
    Node *body;
    Obj *locals;
    Obj *va_area;
    Obj *alloca_bottom;
    int stack_size;

    // Nested function support (GNU C extension)
    struct Obj *parent_fn; // Enclosing function (NULL if top-level)
    bool is_nested;        // True if defined inside another function
    int nesting_depth;     // 0 = top-level, 1 = one level deep, etc.

    // Block support (Apple blocks extension)
    bool is_block;            // True if this is a block's synthetic function
    Obj **captures;           // Array of captured outer variables
    int num_captures;         // Number of captured variables
    struct Obj *block_outer_locals; // Parent scope's locals at block creation time (for transitive capture)
    bool is_block_var;        // True if declared with __block storage qualifier

    // Static inline function
    bool is_live;
    bool is_root;
    StringArray refs;

    // Code generation (for VM)
    long long code_addr; // Address in text segment where function code starts
    long long code_end_addr; // Address after function code in text segment

    // __attribute__((constructor[(priority)])) / ((destructor[(priority)]))
    bool is_constructor;
    bool is_destructor;
    int  init_priority; // CCCC_NO_INIT_PRIORITY unless an explicit priority was given

    // Lazy membership set for belongs_to_outer_function (#165).
    // Populated on first query; keyed by (long long)(intptr_t)var_ptr → var_ptr.
    // Free with hashmap_deinit_borrowed after codegen completes.
    HashMap local_set;
    bool local_set_built;
};

/*!
 @struct CondIncl
 @abstract Stack entry used to track nested #if/#elif/#else processing
           during preprocessing.
*/
typedef struct CondIncl {
    struct CondIncl *next;
    enum { IN_THEN, IN_ELIF, IN_ELSE } ctx;
    Token *tok;
    bool included;
} CondIncl;

typedef struct CustomAttrUse CustomAttrUse;

typedef enum {
    ATTR_TARGET_TYPEDEF = 1,
    ATTR_TARGET_TYPE = 2,
    ATTR_TARGET_FUNCTION = 3,
    ATTR_TARGET_GLOBAL = 4,
} AttrTargetKind;

typedef struct AttrTarget {
    AttrTargetKind kind;
    char *name;
    Type *ty;
    Obj *obj;
    Token *tok;
} AttrTarget;

/*!
 @struct MacroFn
 @abstract Represents a compile-time macro function.
 @discussion Macro functions are functions marked with [[cccc::macro]] or
             __attribute__((macro)) that execute during compilation to generate
             or transform AST nodes.
 @field name Function name.
 @field body_tokens Original token stream for function body (from preprocessor).
 @field compiled_fn Compiled function object (NULL until compiled).
 @field is_compiled True after successful compilation.
 @field is_macro_entry True if callable from user program macro call sites (always true for [[cccc::comptime]] functions).
 @field is_void_macro True if declared with void return type (definition-only).
 @field next Pointer to next macro in linked list.
*/
typedef struct MacroFn {
    char *name;               // Function name
    Token *body_tokens;       // Original token stream for function body
    Obj *compiled_fn;         // Compiled function object
    bool is_compiled;         // True after successful compilation
    bool is_macro_entry;      // True for all comptime functions (entry-callable from user code)
    bool is_void_macro;       // True if declared void — definition-only, no splice node
    bool is_variadic;         // True if declaration has a trailing ...
    bool is_attribute_handler; // True for @macro(attribute("name")) handlers
    char *attribute_name;      // Registered custom attribute name
    int fixed_param_count;    // Number of named parameters before ...
    struct MacroFn *next;     // Next macro in linked list
} MacroFn;

// A struct member value captured from a comptime struct variable.
typedef struct ComptimeVarMember {
    char *name;
    bool is_float;
    int64_t int_val;
    double float_val;
    struct ComptimeVarMember *next;
} ComptimeVarMember;

// Comparison operator for test attribute assertions (error_count, return).
typedef enum {
    CMP_NONE = 0, // field not specified
    CMP_EQ,       // = or ==
    CMP_NE,       // !=
    CMP_LT,       // <
    CMP_LE,       // <=
    CMP_GT,       // >
    CMP_GE,       // >=
} CmpOp;

// Discriminator for the return= assertion type.
typedef enum {
    RET_NONE  = 0, // return= not specified
    RET_INT,       // integer / char / enum (via vm->regs[REG_A0])
    RET_FLOAT,     // float / double (via vm->fregs[FREG_A0])
    RET_STR,       // char* compared with strcmp (pointer via vm->regs[REG_A0])
    RET_STRUCT,    // struct/union: compound literal; field-by-field comparison
} RetKind;

// One expected field value for a RET_STRUCT return assertion.
// Linked list built by parse_test_args; consumed by cc_run_tests.
typedef struct TestRetField TestRetField;
struct TestRetField {
    char      *name;    // designated field name (e.g. "x")
    RetKind    kind;    // RET_INT / RET_FLOAT / RET_STR per scalar type
    union {
        int64_t  i;    // RET_INT
        double   f;    // RET_FLOAT
        char    *s;    // RET_STR — heap-allocated strdup
    } val;
    TestRetField *next;
};

// Output bundle for cc_parse_test_flags(). Holds the full delta to apply when
// lazily recompiling for a per-test flags= attribute.
typedef struct {
    uint32_t or_bits;             // CCCCFlags bits to force on
    uint32_t set_mask;            // CCCCFlags bits explicitly named (on or off)
    int      opt_level;           // -O level (valid only when opt_set)
    bool     opt_set;             // true if -O/-Ox/--optimize=N was present
    uint64_t warn_or;             // CCCCWarning bits to enable
    uint64_t warn_mask;           // CCCCWarning bits explicitly named
    uint64_t warn_errors_or;      // warning_errors bits to enable
    uint64_t warn_errors_mask;    // warning_errors bits explicitly named
    bool     warn_as_errors;      // true if -Werror (global) was given
    bool     warn_as_errors_set;  // true if any -Werror variant was present
    uint32_t f_enable;            // CcccOptPass bits to force ON
    uint32_t f_disable;           // CcccOptPass bits to force OFF
    char   **ffi_allow;           // names to add to allow-list (heap-allocated)
    int      ffi_allow_count;     // number of entries in ffi_allow
} CcTestFlagsDelta;

// A test function registered via [[cccc::test]].
typedef struct TestFnRecord TestFnRecord;
struct TestFnRecord {
    char *name;         // C function name (used for address lookup)
    char *display_name; // human-readable name from name = "..."; NULL = use name
    char *suite;        // NULL if no suite assigned
    char *error_pat;    // expected error substring; NULL = normal test
    bool  error_pat_negate; // true = error must NOT contain error_pat
    int   neg_passed;   // 1=passed, 0=no error produced, -1=wrong error
    char  neg_actual[256]; // first actual error for failure diagnostics
    long  timeout_ms;   // per-test timeout in ms (0 = use global --test-timeout)
    bool  expect_compile_error; // true = any compile error passes the test (#615)
    int   expect_errors;   // operand for error_count assertion (0 if unset)
    CmpOp error_count_op;  // CMP_NONE = not set; otherwise the comparison operator
    RetKind ret_kind;      // RET_NONE = no return assertion
    CmpOp   ret_op;        // comparison operator for return (default CMP_EQ)
    union {
        int64_t      ret_int;    // for RET_INT
        double       ret_float;  // for RET_FLOAT
        char        *ret_str;    // for RET_STR
        TestRetField *ret_fields; // for RET_STRUCT (linked list of expected fields)
    } ret_expect;
    char  *ret_struct_text;    // raw source span of compound literal (for error msg)
    double ret_epsilon;    // 0.0 = use default 1e-9; set by return_epsilon=
    int    expect_exit_code; // -1 = disabled; >=0 = expected shell-convention exit code
    // Per-test flags (from flags = "..." in [[cccc::test(flags = "...")]])
    char     *test_flags;      // raw flags string for display; NULL = no per-test flags
    uint32_t  test_flags_or;   // bits to force on when compiling this test
    uint32_t  test_flags_mask; // which bits the string explicitly controls (on or off)
    int       test_opt_level;  // per-test optimisation level (0..4)
    bool      test_opt_set;    // true if flags= specified an -O/-Ox/--optimize=N level
    // Per-test warning delta (#612)
    uint64_t  test_warn_or;          // CCCCWarning bits to enable
    uint64_t  test_warn_mask;        // CCCCWarning bits explicitly named
    uint64_t  test_warn_errors_or;   // warning_errors bits to enable
    uint64_t  test_warn_errors_mask; // warning_errors bits explicitly named
    bool      test_warn_as_errors;   // -Werror (global) given
    bool      test_warn_as_errors_set; // any -Werror variant present
    // Per-test -f pass delta (#612)
    uint32_t  test_f_enable;   // CcccOptPass bits to force ON
    uint32_t  test_f_disable;  // CcccOptPass bits to force OFF
    bool      test_f_set;      // true if any -f/-fno- flag given
    // Per-test ffi-allow list
    char    **test_ffi_allow;       // NULL or names to allow; heap-allocated
    int       test_ffi_allow_count; // number of entries
    // Per-test output assertions (#614)
    char *expect_stderr;   // POSIX ERE; test FAILS if stderr does not match
    char *reject_stderr;   // POSIX ERE; test FAILS if stderr matches
    char *expect_stdout;   // POSIX ERE; test FAILS if stdout does not match
    char *reject_stdout;   // POSIX ERE; test FAILS if stdout matches
    TestFnRecord *next;
};

// A setup or teardown function registered via [[cccc::test_setup]] / [[cccc::test_teardown]].
typedef struct TestSetupRecord TestSetupRecord;
struct TestSetupRecord {
    char *fn_name;      // C function name (address lookup in prog)
    char *name_pat;     // NULL = all tests; fnmatch glob on test display name
    char *suite;        // NULL = all suites; exact suite name to match
    bool  once;         // run once at suite boundary (suite) or first/last match (name_pat)
    bool  once_fired;   // true after the once-hook has been executed
    bool  is_teardown;  // false = setup, true = teardown
    bool  inherit;      // if true, hook applies to sub-suites via suite_matches (#515)
    TestSetupRecord *next;
};

// A build-entry function registered via [[cccc::build]]. Only the C function
// name is recorded; the --build runner resolves and invokes it by address.
typedef struct BuildFnRecord BuildFnRecord;
struct BuildFnRecord {
    char *name;             // C function name (used for address lookup)
    BuildFnRecord *next;
};

// A factory function registered via [[cccc::build_target]] (or with kind=native).
// When --build-target=NAME matches a factory name the runner calls the factory
// directly (skipping build_main) and builds its returned target.
typedef struct BuildTargetFnRecord BuildTargetFnRecord;
struct BuildTargetFnRecord {
    char *name;                  // C function name
    char *kind;                  // "native" (default) or "bytecode" (#545)
    BuildTargetFnRecord *next;
};

// Test output format selector.
typedef enum {
    TEST_FORMAT_TAP,    // TAP version 13 (default)
    TEST_FORMAT_PLAIN,  // Human-readable plain text
    TEST_FORMAT_JSON,   // JSON machine-readable output
} CcTestFormat;

// Options controlling which tests are run and how output is formatted.
typedef struct {
    const char *test_glob;    // --test=GLOB pattern, or NULL to run all
    const char *suite_filter; // --test-suite=NAME, or NULL to run all suites
    bool list_only;           // --list-tests: enumerate without running
    bool fail_fast;           // --fail-fast: stop after first failure
    int  test_timeout;        // --test-timeout=N: per-test timeout in seconds (0=off)
    CcTestFormat format;      // --test-format=tap|plain|json
} CcTestOptions;

// A variable or struct instance declared with #pragma comptime.
// Values are read from the macro VM's data segment after compilation and
// cached here so macro FFI callbacks can return them without VM access.
typedef struct ComptimeVar {
    char *name;               // Variable name
    Token *decl_tokens;       // Full declaration tokens (included in macro program)
    bool is_struct;           // True if the top-level type is a struct/union
    bool is_evaluated;        // True after values have been read from the data segment
    bool is_float;            // True for float/double scalar types
    int64_t int_val;          // Scalar integer value (or 0 for structs)
    double float_val;         // Scalar float/double value
    Obj *ptr_obj;             // Parent-side shadow object for $get_comptime_ptr
    ComptimeVarMember *members; // Per-field values for struct vars (NULL for scalars)
    struct ComptimeVar *next;
} ComptimeVar;

/*!
 @struct Scope
 @abstract Represents a parser block scope. Two kinds of block scopes are
           used: one for variables/typedefs and another for tags.
*/
/*!
 @struct VarScopeNode
 @abstract Linked list node for variable/typedef scope entries.
 @field var Pointer to variable object (if variable).
 @field type_def Pointer to typedef type (if typedef).
 @field enum_ty Pointer to enum type (if enum constant).
 @field enum_val Enum constant value (int64_t to support C23 wide underlying types).
 @field name Variable or typedef name.
 @field name_len Length of name.
 @field next Pointer to next node in list.

 @discussion The first 4 fields match VarScope layout for safe casting.
*/
typedef struct VarScopeNode {
    // VarScope fields (must come first for casting)
    Obj *var;
    Type *type_def;
    Type *enum_ty;
    int64_t enum_val;
    bool is_deprecated;
    char *deprecated_msg;
    // Additional fields for linked list
    char *name;
    int name_len;
    struct VarScopeNode *next;
} VarScopeNode;

/*!
 @struct TagScopeNode
 @abstract Linked list node for struct/union/enum tag scope entries.
 @field name Tag name.
 @field name_len Length of name.
 @field ty Pointer to tagged type.
 @field next Pointer to next node in list.
*/
typedef struct TagScopeNode {
    char *name;
    int name_len;
    Type *ty;
    struct TagScopeNode *next;
} TagScopeNode;

struct Scope {
    struct Scope *next;
    // C has two block scopes; one is for variables/typedefs and
    // the other is for struct/union/enum tags.
    VarScopeNode *vars; // Linked list (kept for relfection.c iteration)
    TagScopeNode *tags; // Linked list (kept for relfection.c iteration)
    HashMap var_map;    // O(1) name lookup; NULL buckets = use linear fallback
    HashMap tag_map;
};

/*!
 @struct LabelEntry
 @abstract Tracks labels used for goto and labeled statements and their
           defined addresses in the generated text segment.
*/
typedef struct LabelEntry {
    char *name;         // Label name (for named labels)
    char *unique_label; // Unique label identifier (for break/continue)
    Pc address;      // Instruction index where label is defined
} LabelEntry;

/*!
 @struct GotoPatch
 @abstract Records a forward jump (JMP) that must be patched once the
           destination label is defined.
*/
typedef struct GotoPatch {
    char *name;          // Label name to jump to
    char *unique_label;  // Or unique label identifier
    Pc location;      // Instruction index of JMP target operand
} GotoPatch;

typedef struct VirtualMachine VirtualMachine;

/* Per-signal action slot for VM-managed signal handling */
#define CCCC_NSIG 32

typedef struct {
    int      action;      /* 0=DFL, 1=IGN, 2=VM handler */
    long long handler_fn; /* VM function pointer when action==2 */
    /* sa_mask/sa_flags (#738): populated and round-tripped through oact by
       sigaction() (signal.c) for guest struct-field fidelity, but NOT
       enforced at the OS level -- cccc_set_guest_signal_action only ever
       installs a fixed handler/ignore/default disposition (same as
       signal()/VSIGNAL), so SA_RESTART/SA_NODEFER/SA_RESETHAND etc. are
       stored but inert. signal()/raise() (VSIGNAL/VRAISE, ops.c) leave
       these at 0. */
    unsigned int sa_mask;
    int sa_flags;
} SigSlot;

/* The floating-point register file is a flat double. The frontend already
 * emits type-specific opcodes (FADD3 vs FADD3_F32, FLDR vs FLDR_F32), so the
 * register itself does not need to carry a precision tag. A `float` value is
 * stored as the double that results from rounding to float precision and then
 * widening (which is exact), so reading any register as a double is correct
 * with no per-access branch; the F32 opcodes round their result through
 * `(float)` before the (exact) widening store. SIMD/vector state will live in
 * a separate wide-register file (see tracker #463), not here. */
typedef struct {
    double f64;
} FReg;

/* Up-to-512-bit SIMD vector register: a lane-type-agnostic raw container,
 * sized to the widest currently-supported vector_size (64 bytes). The active
 * lane view AND the active width are determined entirely by the opcode/
 * operand that touches it (mirrors the FReg design: the opcode carries the
 * type, not the register) -- a 128- or 256-bit value only occupies a prefix
 * of the union; handlers loop up to the operand-carried lane count, not
 * sizeof(VReg). Backs GCC vector_size(N) types (tracker #72, widened to
 * 256/512-bit by #722) and the autovectorizer (#463). */
typedef union {
    double   f64[8];
    float    f32[16];
    int64_t  i64[8];
    int32_t  i32[16];
    int16_t  i16[32];
    int8_t   i8[64];
} VReg;

/*!
 @typedef AsmCallback
 @abstract Callback invoked when an `asm("...")` statement is encountered
           during code generation.
 @param vm The VM/compiler instance.
 @param asm_str The asm string literal content.
 @param user_data User-provided context pointer (set via cc_set_asm_callback).
 @discussion The callback may emit custom bytecode into the VM's text
             segment, perform logging, or otherwise handle the asm string.
*/
typedef void (*AsmCallback)(VirtualMachine *vm, const char *asm_str, void *user_data);

/*!
 @struct ForeignFunc
 @abstract Represents a registered foreign (native C) function callable from VM
 code.
 @field name Function name (for lookup during compilation).
 @field func_ptr Pointer to the native C function.
 @field num_args Number of arguments the function expects (total for
 non-variadic, fixed args for variadic).
 @field returns_double True if function returns double, false if returns long
 long.
 @field returns_float True if function returns a single-precision float
 (mutually exclusive with returns_double).
 @field is_variadic True if function is variadic (accepts ... arguments).
 @field num_fixed_args For variadic functions: number of fixed args before ...
 (e.g., printf has 1: format string).
*/
typedef struct ForeignFunc {
    char *name;
    size_t name_len;  // strlen(name), cached to avoid O(n) strlen per lookup (#164)
    void *func_ptr;
    int num_args;
    int returns_double;
    int returns_float; // 1 if function returns float (32-bit), 0 otherwise (#406)
    int is_variadic;    // 1 if function is variadic (e.g., printf), 0 otherwise
    int num_fixed_args; // For variadic functions, number of fixed args (rest
                        // are variable)
    uint64_t double_arg_mask; // Bitmask indicating which args are doubles (bit
                              // N = arg N)
    int is_dynamic_placeholder; // 1 for extern declarations awaiting dlsym
    int is_asm_passthru;        // 1 if generated by --asm-passthru
    char *asm_src;              // original asm string, NULL unless is_asm_passthru
} ForeignFunc;

typedef struct DynamicLibrary {
    void *handle;
    char *path;
    int token;
    int live_symbol_count;
    int is_closed;
} DynamicLibrary;

typedef struct DynamicSymbol {
    void *func_ptr;
    char *name;
    int token;
    int library_index;
    int is_live;
} DynamicSymbol;

/*!
 @struct AllocHeader
 @abstract Metadata header stored before each heap allocation for tracking.
 @field size Size of allocation (excluding header), rounded up for alignment
 @field requested_size Original requested size (for bounds checking)
 @field magic Magic number (0xDEADBEEF) for detecting corruption.
 @field canary Front canary value for heap overflow detection (when enabled)
 @field freed Flag indicating if this block has been freed (for UAF detection)
 @field generation Generation counter incremented on each free (for UAF
 detection)
 @field alloc_pc Program counter at allocation site (for debugging)
*/
typedef struct AllocHeader {
    size_t size;             // Allocated size (rounded, excluding header)
    size_t requested_size;   // Original requested size (for bounds checking)
    int magic;               // Magic number for debugging (0xDEADBEEF)
    long long canary;        // Front canary (if heap canaries enabled)
    int freed;               // 1 if freed (for UAF detection)
    int generation;          // Generation counter (incremented on free)
    int creation_generation; // Generation when pointer was created (for
                             // temporal safety)
    long long alloc_pc;      // PC at allocation site (for leak detection)
    // Per-allocation type_kind was removed (#653): type tracking now lives
    // in a byte-granular shadow (vm->type_shadow_pages) so member/interior
    // accesses can be checked too, not just the base pointer. See CHKT3
    // in ops.c.
} AllocHeader;

// The default (malloc) allocation path assumes 8-byte alignment introduces
// zero padding before the header (vm_heap_bump_alloc's alignment=8 fast
// path). That only holds if the header itself is a multiple of 8 bytes.
_Static_assert(sizeof(AllocHeader) % 8 == 0,
               "AllocHeader size must be a multiple of 8 for the default "
               "malloc alignment path to introduce zero padding");

/*!
 @struct FreeBlock
 @abstract Free list node for tracking freed memory blocks.
 @field next Pointer to next free block in the list.
 @field size Size of this free block (excluding header).
*/
typedef struct FreeBlock {
    struct FreeBlock *next;
    size_t size;
} FreeBlock;

/*!
 @struct AllocRecord
 @abstract Tracks an active heap allocation for leak detection.
 @field next Pointer to next record in the list.
 @field address Address of the allocated memory (user pointer).
 @field size Size of the allocation in bytes.
 @field alloc_pc Program counter at allocation site.
*/
typedef struct AllocRecord {
    struct AllocRecord *next;
    void *address;
    size_t size;
    long long alloc_pc;
} AllocRecord;

/*!
 @struct StackVarMeta
 @abstract Unified metadata for stack variable instrumentation.
 @field name Variable name (for debugging/reporting).
 @field bp Base pointer value when variable is active.
 @field offset Offset from BP (negative for locals, positive for params).
 @field ty Type information for the variable.
 @field scope_id Unique identifier for the scope where variable was declared.
 @field is_alive 1 if variable is in scope, 0 if out of scope.
 @field initialized 1 if variable has been initialized, 0 if uninitialized.
 @field read_count Number of read accesses to this variable.
 @field write_count Number of write accesses to this variable.
*/
typedef struct StackVarMeta {
    char *name;
    long long bp;
    long long offset;
    Type *ty;
    int scope_id;
    int is_alive;
    int initialized;
    long long read_count;
    long long write_count;
} StackVarMeta;

/*!
 @struct ScopeVarNode
 @abstract Linked list node for tracking variables within a scope.
 @field meta Pointer to the variable's metadata.
 @field next Pointer to the next variable in the scope.
 */
typedef struct ScopeVarNode {
    StackVarMeta *meta;
    struct ScopeVarNode *next;
} ScopeVarNode;

/*!
 @struct ScopeVarList
 @abstract Linked list of variables belonging to a specific scope.
 @field head Head of the linked list.
 @field tail Tail for efficient O(1) append operations.
 */
typedef struct ScopeVarList {
    ScopeVarNode *head;
    ScopeVarNode *tail;
} ScopeVarList;

/*!
 @struct ProvenanceInfo
 @abstract Tracks pointer provenance (origin) for validation.
 @field origin_type Type of origin: 0=HEAP, 1=STACK, 2=GLOBAL.
 @field base Base address of the original object.
 @field size Size of the original object.
*/
typedef struct ProvenanceInfo {
    int origin_type; // 0=HEAP, 1=STACK, 2=GLOBAL
    long long base;
    size_t size;
} ProvenanceInfo;

/*!
 @struct SourceMap
 @abstract Maps bytecode offsets to source file locations for debugger support.
 @field pc_offset Offset in text segment (bytecode) where this mapping applies.
 @field file Source file containing this code.
 @field line_no Line number in source file.
*/
typedef struct SourceMap {
    long long pc_offset; // Offset in text segment
    File *file;          // Source file
    int line_no;         // Line number in source
    int col_no;          // Column number (1-based)
    int end_col_no;      // End column number (1-based)
} SourceMap;

/*!
 @struct SourceIndex
 @abstract Secondary index keyed by (file, line_no) for O(log n) breakpoint lookups.
 @field file Source file.
 @field line_no Line number in source.
 @field first_pc First bytecode PC offset for this source location.
*/
typedef struct SourceIndex {
    File *file;
    int line_no;
    Pc first_pc;
} SourceIndex;

/*!
 @struct DebugSymbol
 @abstract Represents a variable's debug information for expression evaluation.
 @field name Variable name (for lookup).
 @field offset Offset from BP (negative for locals) or address in data segment
 (globals).
 @field ty Type of the variable.
 @field is_local True if local variable (BP-relative), false if global.
 @field scope_depth Scope depth (for handling shadowing).
*/
typedef struct DebugSymbol {
    char *name;       // Variable name
    long long offset; // BP offset (locals) or data segment address (globals)
    Type *ty;         // Variable type
    int is_local;     // 1=local (BP-relative), 0=global (data segment)
    int scope_depth;  // Scope depth for shadowing resolution
    Obj *owner_fn;    // Owning function for locals, NULL for globals
} DebugSymbol;

typedef struct TypeNameRecord {
    Type *ty;
    char *name;
    int name_len;
    Obj *owner_fn;
    bool is_tag;
    struct TypeNameRecord *next;
} TypeNameRecord;

/*!
 @struct Watchpoint
 @abstract Represents a data breakpoint that triggers on memory access.
 @field address Memory address being watched.
 @field size Size of watched region in bytes.
 @field type Type flags: WATCH_READ | WATCH_WRITE | WATCH_CHANGE.
 @field old_value Last known value (for change detection).
 @field expr Original expression string (for display).
 @field enabled Whether this watchpoint is currently active.
 @field hit_count Number of times this watchpoint has been triggered.
*/
#ifndef MAX_WATCHPOINTS
#define MAX_WATCHPOINTS 64
#endif

// Watchpoint type flags
#define WATCH_READ (1 << 0)   // Trigger on reads
#define WATCH_WRITE (1 << 1)  // Trigger on writes
#define WATCH_CHANGE (1 << 2) // Only trigger if value actually changes

typedef struct Watchpoint {
    void *address;       // Address being watched
    int size;            // Size in bytes
    int type;            // WATCH_READ | WATCH_WRITE | WATCH_CHANGE
    long long old_value; // Last value (for change detection)
    char *expr;          // Original expression (for display)
    int enabled;         // 1 if enabled, 0 if disabled
    int hit_count;       // Number of times triggered
} Watchpoint;

/*!
 @struct Breakpoint
 @abstract Represents a debugger breakpoint at a specific program counter
 location.
 @field pc Program counter address where the breakpoint is set.
 @field enabled Whether this breakpoint is currently active.
 @field hit_count Number of times this breakpoint has been hit.
 @field condition Optional condition expression source (NULL if unconditional).
 @field cond_fn Compiled condition wrapper function (NULL until the first hit
        compiles it, or compilation fails -- see cond_compile_failed).
        Compiled once per breakpoint (its pc fixes the enclosing function and
        therefore every local's frame offset) and reused on every later hit;
        only the live frame pointer varies between hits (ticket 113).
 @field cond_compile_failed Set when compiling `condition` failed, so later
        hits don't retry and re-print the same diagnostic every time.
*/
#ifndef MAX_BREAKPOINTS
#define MAX_BREAKPOINTS 256
#endif

typedef struct Breakpoint {
    Pc pc;        // Instruction index of breakpoint
    int enabled;     // 1 if enabled, 0 if disabled
    int hit_count;   // Number of times hit
    char *condition; // Optional condition expression (NULL if unconditional)
    struct Obj *cond_fn;      // Compiled condition wrapper (see doc above)
    bool cond_compile_failed; // true if compiling `condition` failed once
} Breakpoint;

/*!
 @struct CompileError
 @abstract Represents a compilation error or warning collected during
 compilation.
 @field next Pointer to the next error in the linked list.
 @field message Formatted error message string.
 @field filename Source file name where the error occurred.
 @field line_no Line number in the source file.
 @field col_no Column number in the source file.
 @field severity 0 for error, 1 for warning.
*/
typedef struct CompileError {
    struct CompileError *next; // Next error in linked list
    char *message;             // Formatted error message
    char *filename;            // Source file name
    int line_no;               // Line number
    int col_no;                // Column number
    int severity;              // 0 = error, 1 = warning
    const char *warn_name;     // Warning option name, or NULL for errors
} CompileError;

/*!
 @struct ArenaBlock
 @abstract Represents a single memory block in an arena allocator.
 @field base Pointer to the start of the memory block (from mmap/VirtualAlloc).
 @field ptr Current allocation pointer (bump pointer).
 @field size Total size of this block in bytes.
 @field next Pointer to the next block in the chain.
*/
typedef struct ArenaBlock {
    char *base;              // Start of memory block
    char *ptr;               // Current allocation pointer (bump pointer)
    size_t size;             // Total block size
    struct ArenaBlock *next; // Next block in chain
} ArenaBlock;

/*!
 @struct Arena
 @abstract Arena allocator for fast, bulk memory allocation with single
 deallocation.
 @field current Currently active block for allocations.
 @field blocks Linked list of all allocated blocks (for cleanup).
 @field default_block_size Default size for new blocks (typically 1MB).
 @discussion Arena allocators use bump-pointer allocation within large memory
             blocks (allocated via mmap/VirtualAlloc). All allocations are
             freed together when the arena is destroyed. Used for parser
             frontend (tokens, AST nodes) which have fire-and-forget lifetime.
*/
typedef struct Arena {
    ArenaBlock *current;       // Current block for allocations
    ArenaBlock *blocks;        // All blocks (for cleanup)
    size_t default_block_size; // Default block size (1MB)
} Arena;

/*!
 @struct Debugger
 @abstract Encapsulates all debugger state for the CCCC VM.
 @discussion Contains breakpoints, stepping control, source mapping,
             debug symbols, and watchpoints. Enabled via CCCC_ENABLE_DEBUGGER
 flag.
*/
#ifndef MAX_DEBUG_SYMBOLS
#define MAX_DEBUG_SYMBOLS 4096
#endif
typedef struct Debugger {
    // Breakpoints
    Breakpoint breakpoints[MAX_BREAKPOINTS];
    int num_breakpoints;

    // Stepping control
    int single_step; // Single-step mode (stop after each instruction)
    int step_over;   // Step over mode (skip function calls)
    int step_out;    // Step out mode (run until function returns)
    Pc step_over_return_addr;      // Return PC for step over
    long long *step_out_bp;           // Base pointer for step out
    int debugger_attached;            // Debugger REPL is active
    bool crash_debug_auto;            // Debugger auto-enabled for crash-trapping
                                       // only (not via explicit -g): run
                                       // normally instead of stopping at entry
    volatile sig_atomic_t host_fault_signal; // Terminal native signal, if any

    // Source mapping (bytecode ↔ source lines)
    SourceMap *source_map;   // Array of PC to source location mappings
    int source_map_count;    // Number of source map entries
    int source_map_capacity; // Allocated capacity
    File *last_debug_file;   // Last file during debug info emission
    int last_debug_line;     // Last line number during debug info emission
    int last_debug_col;      // Last column number during debug info emission

    // Secondary source index for O(log n) line→PC lookups
    SourceIndex *source_index;   // Sorted by (file, line_no)
    int source_index_count;      // Number of entries in source_index

    // Debug symbols for expression evaluation
    DebugSymbol debug_symbols[MAX_DEBUG_SYMBOLS];
    int num_debug_symbols;

    // Watchpoints (data breakpoints)
    Watchpoint watchpoints[MAX_WATCHPOINTS];
    int num_watchpoints;

    // Compile-and-run conditional breakpoints (ticket 113): a single global
    // pointer variable, lazily declared once per session, that each compiled
    // condition wrapper dereferences to reach the paused frame's locals. Set
    // to the live vm->bp immediately before each conditional-breakpoint hit
    // runs its cached wrapper -- see debugger_eval_condition in debugger.c.
    struct Obj *dbg_frame_var;
} Debugger;

#ifndef MAX_CALLS
#define MAX_CALLS 16384
#endif

#ifndef MAX_LABELS
#define MAX_LABELS 256
#endif

#ifndef MAX_SPARSE_CASES
#define MAX_SPARSE_CASES 256
#endif

#define RETURN_BUFFER_POOL_SIZE 8

/*!
 @struct Compiler
 @abstract Encapsulates all compiler frontend state: preprocessor, parser, and
 code generator.
 @discussion Contains state for preprocessing (#include, #define, #if), parsing
 (AST construction, scope management), and code generation (labels, patches,
 FFI). Separated from VM runtime state to clarify the compilation/execution
 boundary.
*/
/*!
 @enum CStdVersion
 @abstract Selected C language standard version.
 @discussion Used to drive __STDC_VERSION__ and other standard-dependent
 predefined macros. GNU variants (gnu99, gnu11, etc.) are stored separately
 in Compiler.c_std_gnu.
*/
typedef enum {
    CCCC_STD_C89,  // C89/C90 / GNU89/GNU90 — __STDC_VERSION__ not defined
    CCCC_STD_C99,  // C99  / GNU99  — __STDC_VERSION__ 199901L
    CCCC_STD_C11,  // C11  / GNU11  — __STDC_VERSION__ 201112L
    CCCC_STD_C17,  // C17/C18 / GNU17/GNU18 — __STDC_VERSION__ 201710L (default)
    CCCC_STD_C23,  // C23/C2x / GNU23/GNU2x — __STDC_VERSION__ 202311L
} CStdVersion;

/*!
 @enum CCCCAttrTarget
 @abstract Attribute spelling used when CCCC emits generated C source.
 @discussion Controls how non-CCCC attributes are printed for frontend output
 modes such as -E, -m, -G, and native compilation.
*/
typedef enum {
    CCCC_ATTR_TARGET_AUTO,
    CCCC_ATTR_TARGET_C23,
    CCCC_ATTR_TARGET_GNU,
    CCCC_ATTR_TARGET_MSVC,
    CCCC_ATTR_TARGET_STRIP,
} CCCCAttrTarget;

typedef enum { CTX_COMPTIME, CTX_EMIT } ComptimeCtxType;

typedef struct {
    ComptimeCtxType type;
    bool            needs_end;  // false = bare whole-file form, true = begin/end
    File           *file;       // for comptime auto-close on file boundary
    Token          *open_tok;   // for unclosed-block diagnostics
} ComptimeCtxEntry;

// Bitmask of individual optimisation passes for -f/-fno- CLI overrides.
// The effective pass set is (level_to_opt_mask(opt_level) | opt_f_enable) & ~opt_f_disable.
typedef enum {
    CCCC_OPT_FOLD      = 1 << 0, // Constant folding           (-ffold)
    CCCC_OPT_PEEPHOLE  = 1 << 1, // Peephole reductions        (-fpeephole)
    CCCC_OPT_COPY_PROP = 1 << 2, // Copy propagation           (-fcopy-prop)
    CCCC_OPT_DCE       = 1 << 3, // Dead code elimination      (-fdce)
    CCCC_OPT_CSE       = 1 << 4, // Common-subexp elimination  (-fcse)
    CCCC_OPT_FUSE      = 1 << 5, // Opcode fusion              (-ffuse)
    CCCC_OPT_ELIM_EXT  = 1 << 6, // Redundant extension elim   (-felim-ext)
} CcccOptPass;

// One __attribute__((constructor)) / ((destructor)) function, gathered by
// gen() (codegen.c) and run around main() by cc_run() (vm.c).
typedef struct {
    long long code_addr; // Address in text_seg where the function starts
    int       priority;  // CCCC_NO_INIT_PRIORITY unless explicitly given
    int       seq;       // Declaration order, for a stable sort tie-break
} CCCCInitEntry;

typedef struct Compiler {
    // Preprocessor state
    bool skip_preprocess;     // Skip preprocessing step
    HashMap macros;           // Macro definitions
    CondIncl *cond_incl;      // Conditional inclusion stack
    HashMap pragma_once;      // #pragma once tracking
    HashMap included_headers; // Track included headers for lazy stdlib loading
    HashMap include_guards;   // Header include guard cache (path -> guard name)
    HashMap guard_macros;     // Set of macro names used as include guards
    int include_next_idx;     // Index for #include_next

    // Compile-time macro state
    MacroFn *macro_fns;              // Linked list of captured macro functions
    ComptimeVar *comptime_vars;      // Linked list of [[cccc::comptime]] variable decls
    TestFnRecord *test_fns;          // Linked list of [[cccc::test]] function names
    TestSetupRecord *test_setups;    // Linked list of [[cccc::test_setup/teardown]] records
    BuildFnRecord *build_fns;        // Linked list of [[cccc::build]] entry function names
    BuildTargetFnRecord *build_target_fns; // Linked list of [[cccc::build_target]] factory names
    bool build_mode;                 // True when running under --build (no main required)
    bool testing_mode;               // True when running under --testing (no main required)
    char *current_suite;             // Active suite path ("a/b/c") set by nested #pragma cccc suite begin
    struct SuiteLenEntry {
        size_t prev_len;   // strlen(current_suite) before this begin was pushed
        Token *open_tok;   // token of the #pragma cccc suite begin (for error messages)
    } *suite_len_stack;              // Stack entry per open suite begin block
    int     suite_stack_len;         // Number of currently open suite begin blocks
    int     suite_stack_cap;         // Allocated capacity of suite_len_stack
    bool in_macro_mode;              // True when compiling/executing a macro function
    bool in_macro_expansion;         // True during macro AST expansion pass
    ComptimeCtxEntry *ctx_stack;     // Stack of active comptime/emit contexts
    int               ctx_stack_len; // Number of entries in ctx_stack
    int               ctx_stack_cap; // Allocated capacity of ctx_stack
    bool macro_fns_compiled;         // True after compile_all_macros has run
    // Holds a copy of the hashmap_snapshot taken by compile_macro_program.
    // The snapshot is heap-allocated and must survive a longjmp exit.
    // cc_destroy frees it when has_macro_snapshot is true; normal paths
    // call hashmap_restore and clear the flag before returning.
    bool has_macro_snapshot;
    HashMap macro_snapshot_backup;
    bool reflection_attrs_registered; // True after ensure_reflection_attrs_registered has run (#235)
    bool no_comptime;                // --no-comptime: skip entire comptime/macro phase (for TUs that don't use comptime)
    bool comptime_include_all;       // --comptime-include-all: forward all #include decls to comptime pass (legacy behavior)
    HashMap *macro_scope_stack;       // Snapshot stack for per-comptime-fn macro isolation (#283)
    int      macro_scope_stack_len;
    int      macro_scope_stack_cap;
    bool     allow_comptime_pp_bleed; // --allow-comptime-pp-bleed: restore pre-#283 shared macro table across comptime fn bodies
    int macro_recursion_limit;       // 0 = unlimited, default = 256
    Token *macro_call_tok;           // Active macro invocation token
    Node **macro_vararg_nodes;        // Active inline macro variadic AST args
    char **macro_vararg_strs;         // Active global macro variadic string args
    int macro_vararg_count;           // Number of active variadic args
    bool macro_vararg_string_mode;    // True when varargs are char* token strings
    Token *macro_context_tokens;      // Safe file-scope decls visible to macro bytecode
    Scope *macro_context_scope;       // Parser scope produced from macro_context_tokens
    Obj *macro_globals; // Globals defined by inline macros (injected into
                        // the final program before codegen)
    EmitEvent *emit_events_head;  // Ordered generated-output events
    EmitEvent *emit_events_tail;
    bool macro_emit_recording;        // True while a macro call records generated output

    // #embed directive limits
    size_t embed_limit;      // Soft limit for #embed size (default: 10MB)
    size_t embed_hard_limit; // Secondary warning threshold (default: 50MB)
    bool embed_hard_error;   // If true, exceeding limit is a hard error

    // Warning configuration
    uint64_t warnings;        // Enabled CCCCWarning categories
    uint64_t warning_errors;  // Categories promoted by -Werror=<name>
    uint64_t warning_no_errors; // Categories demoted after global -Werror

    // #pragma GCC diagnostic push/pop stack
    uint64_t *diag_stack_warnings;   // saved warnings bitmasks
    uint64_t *diag_stack_werror;     // saved warning_errors bitmasks
    int       diag_stack_depth;      // current stack depth
    int       diag_stack_cap;        // allocated capacity

    // Diagnostic output format
    bool diagnostic_json; // --json (general JSON output flag)

    // System-header mode (--use-system-headers / --no-builtin-includes / --sysroot)
    bool use_system_headers;      // Prefer SDK headers over CCCC polyfills for non-owned std headers
    bool no_builtin_includes;     // Do not fall back to ./include for non-owned std headers
    const char *builtin_include_dir; // Path of CCCC's own header dir; excluded from SDK-first search

    // Tokenization state
    File *current_file;  // Input file
    File **input_files;  // A list of all input files
    File *primary_file;  // Top-level source file (set in cc_preprocess; used for auto-emit capture)
    bool at_bol;         // True if at beginning of line
    bool has_space;      // True if follows a space character
    int file_no;          // Next real-input file index (indexes input_files); was a
                           // function-local static in tokenize_file, which corrupted
                           // input_files across multiple VMs in one process (#181)
    int embedded_file_no; // Next embedded/in-memory file counter (negative file numbers);
                           // was a function-local static in tokenize_string (#181)

    // Parser state
    Obj *locals;           // All local variable instances during parsing
    Obj *globals;          // Global variables accumulated list
    Obj error_var;         // Error placeholder variable for recovery (per-VM;
                           // was a shared static Obj across all instances, #706).
                           // .name/.ty set in cc_init_parser/parse().
    Scope *scope;          // Current scope
    Obj *initializing_var; // Variable being initialized (for const
                           // initialization)
    bool in_const_gvar_init; // True while parsing a global/static variable's
                             // initializer expression (see gvar_initializer);
                             // forces a nested compound literal to resolve to
                             // an anonymous constant global even when the
                             // literal itself has no storage-class specifier
                             // and lexical scope is not file scope (#720).
    Obj *current_fn;       // Function being parsed
    int fn_nesting_depth;  // Current function nesting depth (0 = top-level)
    bool in_type_lookahead; // Parsing a declarator only to classify it
    int dead_code_depth;    // >0 while parsing a statically-dead branch (counter
                            // so nesting composes: if(0){ if(1){ f(); } })
    bool saw_diag_attr;     // true once any error/warning attribute is seen in
                            // this TU; gates all deadness computation so normal
                            // compiles pay no extra overhead
    Node *gotos;           // Goto statements in current function
    Node *labels;          // Labels in current function
    struct ObjSizeQuery *objsize_queries; // Pending __builtin_object_size(ptr,...)
                            // queries on malloc-tracked pointers in current
                            // function; resolved by resolve_objsize_queries (#642)
    char *brk_label;       // Current break jump target
    char *cont_label;      // Current continue jump target
    int cleanup_scope_depth;   // number of active cleanup scopes (blocks with cleanup vars)
    CleanupChainNode *cur_cleanup_chain; // innermost active cleanup scope (ancestry for goto LCA)
    int brk_cleanup_depth;     // cleanup_scope_depth when current brk_label was established
    int cont_cleanup_depth;    // cleanup_scope_depth when current cont_label was established
    Node *current_switch;  // Switch statement being parsed (NULL if none)
    Obj *builtin_alloca;   // Builtin alloca function
    Obj *builtin_strlen;   // Builtin strlen (forwarded to libc)
    Obj *builtin_strcmp;   // Builtin strcmp (forwarded to libc)
    Obj *builtin_setjmp;   // Builtin setjmp function
    Obj *builtin_longjmp;  // Builtin longjmp function
    Obj *builtin__setjmp;  // Builtin _setjmp (POSIX alias, same semantics as setjmp)
    Obj *builtin__longjmp; // Builtin _longjmp (POSIX alias, same semantics as longjmp)
    Obj *builtin_signal;   // VM-managed signal() registration
    Obj *builtin_raise;    // VM-managed raise() delivery
    Obj *builtin_dlopen;   // VM-managed dlopen
    Obj *builtin_dlsym;    // VM-managed dlsym
    Obj *builtin_dlclose;  // VM-managed dlclose
    Obj *builtin_dlerror;  // VM-managed dlerror
    Obj *builtin_block_copy; // Block_copy() heap-duplication helper (__cccc_block_copy_impl)
    Obj *builtin_free;       // free() prototype so Block_release always resolves (#458)
    Obj *builtin_pc_to_name;   // __builtin_pc_function_name: (void*) -> const char*
    Obj *builtin_pc_to_source; // __builtin_pc_source_location: (void*, const char**, int*) -> int
    TypeNameRecord *type_names; // Persistent typedef/tag declarations for -m

    // Arena allocator for parser frontend (tokens, AST, preprocessor state)
    Arena parser_arena; // Fast bump-pointer allocator

    StringArray include_paths;        // Quote include search paths
    StringArray system_include_paths; // System header search paths for <...>
    HashMap include_cache;            // Cache for search_include_paths
    StringArray file_buffers;         // Track allocated file buffers for cleanup

    // URL include cache (only used when CCCC_HAS_CURL is enabled)
    char *url_cache_dir; // Directory for caching downloaded headers
    HashMap url_to_path; // Maps URLs to cached file paths
    StringArray emit_directives; // Preprocessor directives to prepend to serialized output
    int emit_strict;             // --emit-only: suppress auto-capture; only explicitly tagged content appears in -G output
    StringArray comptime_pending_includes; // #include [[cccc::comptime]] filenames queued for comptime pass
    StringArray pragma_link_libs; // Library names queued by #pragma cccc link(...) /
                                   // #pragma comment(lib, ...) (#357), merged into
                                   // the -l/--library list before FFI resolution

    // Code generation state
    int label_counter; // For generating unique labels
    int local_offset;  // Current local variable offset

    // Heap-allocated, grow-on-demand patch tables (initial capacity 256,
    // doubles on overflow).  The four tables share the same shape: a pointer
    // to the entries, a count, and a capacity.  No hard ceiling; very large
    // translation units (e.g. minilua 31k lines) grow gracefully.
    struct {
        Pc location;      // Location in text segment to patch
        Obj *function;    // Function to call
    } *call_patches;
    int num_call_patches;
    int call_patches_cap;

    // Function address patches for function pointers
    struct {
        Pc location;      // Location of IMM operand to patch
        Obj *function;    // Function whose address to use
    } *func_addr_patches;
    int num_func_addr_patches;
    int func_addr_patches_cap;

    struct {
        long long data_offset;   // Pointer slot offset in data segment
        long long target_offset; // Target offset in text/data segment
        long long addend;        // Byte addend applied to target
        int target_segment;      // 0 = data, 1 = text
    } *data_relocs;
    int num_data_relocs;
    int data_relocs_cap;

    // TLS pointer relocations: same shape as data_relocs but the pointer
    // slot lives in tls_template, not data_seg.  Serialised with the
    // bytecode so tls_template can be re-materialized after .c4 load (#493).
    struct {
        long long tls_offset;    // Pointer slot offset in tls_template
        long long target_offset; // Target offset in data segment (text NYI)
        long long addend;        // Byte addend applied to target
        int target_segment;      // 0 = data (only supported value currently)
    } *tls_relocs;
    int num_tls_relocs;
    int tls_relocs_cap;

    // Exported symbol table [V3]: non-static function definitions emitted by
    // -c bytecode so --link and cc_load_module() can resolve CALLs (#565).
    struct {
        Pc     pc_offset;  // Instruction-word index of the function
        char  *name;       // Heap-allocated symbol name (owned)
        size_t name_len;
    } *sym_table;
    int num_sym_table;
    int sym_table_cap;

    // Text relocations [V3]: unresolved external CALL sites recorded instead
    // of erroring when building a -c bytecode target or when --link libs are
    // provided (#565).  Resolved by --link (compile-time) or cc_load_module()
    // (runtime).
    struct {
        Pc    location;  // Instruction-word index of the CALL operand to patch
        char *name;      // Heap-allocated target symbol name (owned)
        size_t name_len;
        int   resolved;  // 1 once patched by cc_link_bytecode / cc_load_module
    } *text_relocs;
    int num_text_relocs;
    int text_relocs_cap;

    // Address relocations [V3+]: unresolved function-pointer address sites
    // recorded when compile_only or deferred_link is set (#566).
    // location = instruction-word index of the lo-word of the LTA3 i64 immediate.
    // Resolved by --link (compile-time) or cc_load_module() (runtime).
    struct {
        Pc    location;  // Instruction-word index of the lo-word of the LTA3 i64
        char *name;      // Heap-allocated target symbol name (owned)
        size_t name_len;
        int   resolved;  // 1 once patched by cc_link_bytecode / cc_load_module
    } *addr_relocs;
    int num_addr_relocs;
    int addr_relocs_cap;

    // Set to 1 when --link libs are provided: defers undefined-symbol errors
    // from codegen to the post-link check in main.c (#565).
    int deferred_link;

    LabelEntry label_table[MAX_LABELS];
    int num_labels;
    GotoPatch goto_patches[MAX_LABELS];
    int num_goto_patches;

    // Switch statement code generation
    void *current_switch_cases;       // Codegen-owned SwitchCasePatch array
    int current_switch_num;           // Number of case entries
    Pc current_switch_table_start; // Dense jump table start, or invalid
    long current_switch_min;          // Minimum case value for dense switches
    long current_switch_size;         // Jump table size for dense switches
    Node *current_switch_default;     // Default case node
    Pc current_default_patch;      // Patch location for default case jump

    // Inline assembly callback
    AsmCallback asm_callback; // User-provided callback for asm statements
    void *asm_user_data;         // User-provided context for callback
    bool asm_passthru;           // --asm-passthru flag: compile asm via native CC

    // Foreign Function Interface (FFI)
    ForeignFunc *ffi_table; // Registry of foreign C functions
    int ffi_count;          // Number of registered functions
    int ffi_capacity;       // Capacity of ffi_table array

    // Current function being compiled (for VLA cleanup)
    Obj *current_codegen_fn;

    // Struct/union return buffer pool (copy-before-return approach)
    char *return_buffer_pool[RETURN_BUFFER_POOL_SIZE]; // Pool of return buffers
    long long return_buffer_offsets[RETURN_BUFFER_POOL_SIZE]; // Data offsets
                                                              // serialized in .c4
    int return_buffer_count; // Number of active return buffers
    int return_buffer_index; // Current buffer index (rotates 0-7)
    int return_buffer_size;  // Size of each buffer (1024 bytes)

    // Linked programs for extern offset propagation
    Obj **link_progs;    // Array of original program lists
    int link_prog_count; // Number of programs

    // Per-instance state (moved from static globals for thread-safety)
    int unique_name_counter; // Counter for new_unique_name()
    int macro_gensym_counter; // Counter for __cccc_gensym()
    int counter_macro_value; // __COUNTER__ macro value

    // Optimization settings
    int opt_level; // Optimization level (0=none, 1=basic, 2=standard,
                   // 3=aggressive; 4 enables fused-op pass)
    bool ffp_contract_fma;  // --fma: emit FMADD3_FMA (single-rounding) instead of FMADD3
    int inline_node_limit; // Max AST nodes for full inlining (0=disable)
    bool have_fn_opt_attrs; // True if any function carries an optimize attribute;
                            // gates the per-function optimizer path in cc_optimize
    // Per-pass overrides from -f<pass> / -fno-<pass> (bitmask of CcccOptPass).
    // Applied on top of the level-derived default: effective = (level_mask | opt_f_enable) & ~opt_f_disable.
    uint32_t opt_f_enable;  // Passes forced ON regardless of -O level
    uint32_t opt_f_disable; // Passes forced OFF regardless of -O level
    uint32_t cli_f_mask;    // -f/-fno- pass bits pinned by CLI (#612: pragma config won't override)

    // #pragma cccc config(...) support
    uint32_t cli_flags_mask;  // CCCCFlags bits explicitly set on the CLI; these
                              // win over `#pragma cccc config(...)` (#357)
    bool cli_opt_level_set;  // True if -O/--optimize was passed on the CLI;
                              // `config(optimisation = N)` is ignored if so
    bool native_mode;        // True when compile_format == COMPILE_NATIVE;
                              // config()'s flag/opt_level effects are skipped

    // Inlining context (used during codegen when expanding inline bodies)
    char *inline_exit_name; // Exit label name for inlined returns (NULL = not inlining)
    int inline_result_reg;  // Register for inlined return values

    // Tail-call context (used during codegen for return f(args) TCO)
    bool emitting_tail_call;  // Set in ND_RETURN; cleared immediately in ND_FUNCALL
    Obj *pending_tail_callee; // Callee recorded by ND_FUNCALL; NULL if inlined/elided

    // ENT3 stack patching for inlined locals
    Pc ent3_stack_loc;   // PC of ENT3 stack_size low word (for patching)
    int ent3_base_stack;    // Original stack_size before inlining additions
    int ent3_extra_stack;   // Additional stack slots from inlined locals

    // ENT3 masks patching for lazy frame-epoch activation (#703). Tracked
    // while generating one function's body: set by emit_lea3_var whenever it
    // emits a recorded LEA3/STKTAG for an escaping local or param of that
    // function, then OR'd into the ENT3 masks word (bits 31 of each half,
    // beyond the param-count-capped float/f32 masks) once the body is done.
    // See ENT3_PUSH_EPOCH_AGG/SCALAR in internal.h and op_ENT3_fn.
    Pc ent3_masks_loc;      // PC of ENT3 masks word (for patching)
    bool frame_has_esc_agg;    // body emits STKTAG for an escaping aggregate
    bool frame_has_esc_scalar; // body emits a recorded LEA3 for an escaping scalar

    // Scalar local promotion (#249). Active only while generating one function.
    // Capped at 4 slots (REG_S0-S3) to leave S4-S7 for the restrict cache.
    Obj *promoted_locals[4];
    int promoted_regs[4];
    int promoted_save_offsets[4];
    bool promoted_dirty[4];
    int promoted_count;

    // FP scalar local promotion (#461). Parallel to integer promotion above.
    Obj *fp_promoted_locals[4];
    int fp_promoted_regs[4];         // FREG_S0–S3
    int fp_promoted_save_offsets[4]; // stack slots (after integer promoted slots)
    bool fp_promoted_dirty[4];
    int fp_promoted_count;

    Obj *promotion_alias_vars[16];
    Obj *promotion_alias_targets[16];
    int promotion_alias_count;

    // True while gen_expr/gen_addr are emitting a load or store through a
    // union member access (#653). Threaded as compiler-scoped state rather
    // than a parameter on emit_load_ex/emit_store_ex (mirroring
    // in_const_gvar_init's parser-side pattern, #720) since only a handful
    // of the ~16 call sites need it. Consumed by emit_load_ex/emit_store_ex
    // to keep CHKT3 from false-positiving on legal union member punning:
    // a union load skips the type check entirely, a union store clears the
    // accessed range's effective-type shadow instead of stamping it.
    bool in_union_member_access;

    // Restrict-param deref cache (#267). Active only while generating one function.
    // Cache key is (param, byte_offset); up to MAX_RESTRICT_CACHE distinct pairs.
    // Slots are lazily bound on first access; restrict_cache_capacity tracks the
    // number of stack/register slots pre-reserved in the frame for sizing purposes.
#define MAX_RESTRICT_CACHE 4
    int restrict_cache_count;    // number of lazily-bound entries so far
    int restrict_cache_capacity; // slots pre-reserved (0 or MAX_RESTRICT_CACHE)
    Obj *restrict_cache_params[MAX_RESTRICT_CACHE];
    long restrict_cache_offsets[MAX_RESTRICT_CACHE]; // byte offset per entry
    int restrict_cache_regs[MAX_RESTRICT_CACHE];
    int restrict_cache_save_offsets[MAX_RESTRICT_CACHE];
    bool restrict_cache_valid[MAX_RESTRICT_CACHE];

    // Restrict derived-local map (#269). Populated by a pre-pass AST walk before
    // codegen; maps local pointer variables provably derived from restrict params
    // to their (param, byte_offset) pair so the deref cache can treat *q like *p.
    // var_offset[i]=true means the offset is non-constant (used for invalidation only).
#define MAX_RESTRICT_DERIVED 16
    int restrict_derived_count;
    Obj  *restrict_derived_vars[MAX_RESTRICT_DERIVED];
    Obj  *restrict_derived_params[MAX_RESTRICT_DERIVED];
    long  restrict_derived_offsets[MAX_RESTRICT_DERIVED];
    bool  restrict_derived_var_offset[MAX_RESTRICT_DERIVED];

    // C language standard selection
    CStdVersion c_std;  // Selected standard version (default: CCCC_STD_C23)
    bool c_std_gnu;     // True for gnuXX variants (gnu17, gnu11, …)
    CCCCAttrTarget attr_target; // Generated/preprocessed attribute spelling

    // Custom entry point name (NULL means "main")
    char *entry_name;

    // __attribute__((constructor)) / ((destructor)) — functions to run
    // before/after main(), sorted by priority in gen() (codegen.c). Each
    // entry's code_addr indexes vm->text_seg once codegen completes.
    // Populated by gen(); consumed by cc_run() in vm.c.
    CCCCInitEntry *ctor_list;
    int ctor_count;
    int ctor_capacity;
    CCCCInitEntry *dtor_list;
    int dtor_count;
    int dtor_capacity;

    // Fuzzing / compile-only mode
    bool compile_only; // If true, compile to bytecode but do not require main()
                       // or execute (used by AFL++ and other fuzzers)
} Compiler;

/*!
 @struct CCCC
 @abstract Encapsulates all state for the CCCC compiler and virtual
           machine. Instances are independent and support embedding.
 @discussion The structure contains registers, memory segments, frontend
             state (preprocessor, tokenizer, parser) and codegen/VM
             bookkeeping. All public API functions accept an
             `CCCC *` as the first parameter.
*/
struct VirtualMachine {
    // VM Registers (pure register-based architecture)
    long long regs[32]; // General-purpose register file (NUM_REGS)
    FReg fregs[32];  // Flat-double floating-point register file
    VReg vregs[32];  // Up-to-512-bit SIMD vector register file (see tracker #72/#463/#722)
    Pc pc;           // Program counter (instruction index)
    long long *bp;      // Base pointer (frame pointer)
    long long *sp;      // Stack pointer
    long long cycle;    // Instruction cycle counter

    // Exit detection (for returning from main)
    long long *initial_sp; // Initial stack pointer (for exit detection)
    long long *initial_bp; // Initial base pointer (for exit detection)

    // Stack bounds checking
    long long *stack_base; // Lower bound of stack (stack_seg start)

    // Memory Segments
    InstrWord *text_seg;  // Text segment (32-bit bytecode words)
    Pc text_ptr;          // Current write position (for code generation)
    long long *stack_seg;    // Stack segment
    InstrWord *old_text_seg; // Backup of original text segment pointer
    char *data_seg;          // Data segment (global variables/constants)
    char *data_ptr;          // Current write position in data segment
    // Thread-local storage
    char   *tls_template;      // Canonical init image for TLS variables (written by gen())
    size_t  tls_template_size; // Byte length of tls_template
    size_t  tls_template_cap;  // Allocated capacity of tls_template
    char   *current_tls_seg;   // Active thread's private TLS copy (updated on context switch)
    char *heap_seg;          // Heap segment (for VM malloc/free)
    char *heap_ptr;          // Current allocation pointer (bump allocator)
    char *heap_end;          // End of heap segment
    FreeBlock *free_list;    // Head of free blocks list (for memory reuse)

    // Byte-granular subobject type shadow for CHKT3 (#653), one instance
    // per tracked segment: heap_shadow covers [heap_seg, heap_seg +
    // heap_committed) (the original #653 scope), data_shadow covers
    // [data_seg, data_seg + data_committed) -- globals (#752). Logically
    // one TypeKind byte per segment byte (TY_VOID == 0 means "no effective
    // type established"), physically a sparse page table (#753): see
    // TypeShadowSeg's doc comment above. Both are lazily grown/populated
    // by type_shadow_ensure/type_shadow_for in ops.c, so they stay in sync
    // even if CCCC_TYPE_CHECKS is toggled on mid-run by
    // #pragma cccc config(safety=N). Replaces the old single
    // AllocHeader.type_kind-per-allocation model, which only supported
    // checking at a heap allocation's offset 0 and didn't cover globals at
    // all. Stack subobjects remain untracked (deferred: stack-slot reuse
    // across frames needs its own liveness bookkeeping, mirroring the
    // false-positive history behind frame_epochs/stack_intervals).
    TypeShadowSeg heap_shadow;
    TypeShadowSeg data_shadow;

    // Segregated free lists for optimized allocation
    // Size classes: 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, LARGE
    // (>8192)
    FreeBlock *
        size_class_lists[12]; // One free list per size class (NUM_SIZE_CLASSES)
    FreeBlock *large_list;    // For allocations > MAX_SMALL_ALLOC (8192)

    // Memory safety tracking
    AllocRecord *alloc_list; // List of active allocations (for leak detection)
    HashMap init_state; // Track initialization state of stack variables (for
                        // uninitialized detection)
    HashMap provenance; // Track pointer provenance for stack/global (ptr ->
                        // {origin_type, base, size})
    HashMap stack_var_meta; // Declaration-level stack variable metadata,
                            // one persistent entry per (scope_id, offset) --
                            // see stack_var_meta_key(). Read/write counts and
                            // name/type info live here, aggregated across all
                            // activations of a variable (including recursion).
    HashMap stack_var_active; // Runtime liveness set: currently-live stack
                              // slots, keyed by actual address (bp+offset) ->
                              // StackVarMeta*. One entry per activation, so
                              // recursive calls of the same function don't
                              // collide the way a single scope_id-keyed bp
                              // field would (#671).
    HashMap ptr_tags; // ptr address → creation_generation (CCCC_MEMORY_TAGGING)

    // Per-frame liveness epochs for dangling-stack-pointer detection through
    // a deeper call (#673). Mirrors the heap temporal-safety scheme (ptr_tags
    // vs AllocHeader.generation) but for stack frames: each activation gets a
    // monotonic epoch at ENT3; every `&local` (LEA3) records the creating
    // frame's epoch; CHKP3 flags a deref iff that epoch is no longer live.
    //
    // Population (ENT3/LEV3 push/pop, STKTAG recording, longjmp truncate) is
    // gated on stack_extents_enabled() below: either CCCC_DANGLING_DETECT, or
    // the program contains a DYNOBJSZ opcode (#648), which also consults
    // stack_intervals/live_epochs to size escaping fixed-size stack buffers.
    // Consumption sites specific to dangling detection (stack_ptr_epochs
    // exact-address recording, CHKP3) stay gated on CCCC_DANGLING_DETECT only.
    bool dynobjsz_present; // set by an incremental text-segment scan (#648)
                           // iff any DYNOBJSZ opcode appears; survives .c4
                           // round-trip because it's derived from the
                           // serialized bytecode, not a codegen-time flag.
    long long dynobjsz_scan_pc; // resume point for that scan -- text can
                                // still be growing when cc_run_at first runs
                                // (comptime macros, --build factories), so
                                // each call scans only the newly-appended
                                // words rather than a fixed one-shot prefix.
    unsigned long long frame_epoch_counter; // monotonic, bumped per ENT3
    struct {
        long long **bps;            // parallel array: bp at push time
        unsigned long long *epochs; // parallel array: epoch assigned
        int count;
        int capacity;
    } frame_epochs; // mirrors the saved-bp chain, for ordered pop/truncate
    HashMap live_epochs;      // epoch -> present; O(1) liveness membership
    HashMap stack_ptr_epochs; // &local address -> creating frame's epoch

    // Retained address intervals for interior stack-pointer resolution
    // (#675), the stack analogue of sorted_allocs below. STKTAG, emitted
    // immediately after the LEA3 base of an escaping array/struct local,
    // records [bp+offset, bp+offset+size) tagged with the creating frame's
    // epoch. Unlike sorted_allocs (a bump allocator, so bases are globally
    // ordered and never reused), stack addresses ARE reused across frames,
    // so intervals from dead frames are retained rather than pruned: a dead
    // frame's extent and a later live frame's extent can overlap at the
    // same addresses. Recency is resolved by epoch order (strictly
    // increasing per activation), so "the interval that currently owns this
    // address" is simply the max-epoch interval containing it -- see
    // stack_interval_stab in ops.c. Exact-address hits are already covered
    // by stack_ptr_epochs above; this table is consulted by CHKP3 only when
    // that lookup misses (i.e. only for interior addresses).
    struct {
        struct {
            long long lo, hi;           // [lo, hi) byte range, raw host addrs
            unsigned long long epoch;   // creating frame's epoch
        } *iv;
        int count;
        int capacity;
    } stack_intervals;

    // Sorted allocation array for O(log n) base-address range queries.
    // Populated by MALC (CALC/REALC delegate to MALC) as a simple append —
    // the VM heap is a bump allocator so heap_ptr only grows, meaning every
    // new base address is higher than all previous ones and the array stays
    // sorted without a search. Entries for freed allocations are left in
    // place (addresses are never reused) and are filtered out by checking
    // AllocHeader.freed at lookup time. Used by DYNOBJSZ to resolve interior
    // pointers (p + k) to their containing allocation; CHKB/CHKP3 could use
    // the same table for interior-pointer provenance checks (follow-up).
    struct {
        void **addresses;      // Sorted array of base addresses (ascending)
        AllocHeader **headers; // Parallel array of headers
        int count;             // Number of tracked allocations (incl. freed)
        int capacity;          // Allocated array capacity
    } sorted_allocs;

    // Segment growth tracking (reserve-and-commit scheme)
    size_t text_committed;  // committed bytes in text segment
    size_t data_committed;  // committed bytes in data segment
    size_t stack_committed; // committed bytes in stack segment
    size_t heap_committed;  // committed bytes in heap segment

    // Configuration
    int poolsize;     // Initial committed element count per segment (256K default)
    int poolsize_max; // Maximum element count for the reserved virtual range
    int debug_vm; // Enable debug output during execution

    // Runtime flags (bitwise combination of CCCCFlags)
    uint32_t flags; // CCCCFlags bitfield for all safety and runtime features
    long long stack_canary; // Stack canary value (random if CCCC_RANDOM_CANARIES
                            // set, else fixed)
    int in_vm_alloc; // Reentrancy guard: prevents HashMap from triggering VM
                     // heap recursion

    // Control Flow Integrity (shadow stack)
    long long *shadow_stack; // Shadow stack for return addresses (CFI)
    long long *shadow_sp;    // Shadow stack pointer

    // Stack instrumentation state
    int current_scope_id;          // Incremented for each scope entry
    int current_function_scope_id; // Scope ID of current function being
                                   // generated
    long long stack_high_water;    // Maximum stack usage tracking
    ScopeVarList *scope_vars;      // Array of per-scope variable lists
    int scope_vars_capacity;       // Capacity of scope_vars array

    // Struct return buffer runtime state (runtime rotation for clean chained
    // calls)
    int runtime_return_buffer_index; // Runtime rotating index for return
                                     // buffers

    char **ffi_allow_list;
    int ffi_allow_count;
    int ffi_allow_capacity;
    char **ffi_deny_list;
    int ffi_deny_count;
    int ffi_deny_capacity;
    int disable_all_ffi;
    int ffi_errors_fatal;
    int enable_ffi_type_checking;

    // VM opcode execution profiling
    bool vm_profile_enabled;
    uint64_t vm_profile_counts[OP_COUNT];
    uint64_t vm_profile_total;
    // Dynamic opcode bigram (transition) profile. Indexed as
    // bigram_counts[prev * OP_COUNT + cur]. vm_profile_bigram_total counts the
    // number of recorded transitions (== total opcodes - 1 when the profile
    // spans the entire run).
    uint64_t vm_profile_bigram_counts[OP_COUNT * OP_COUNT];
    uint64_t vm_profile_bigram_total;
    int vm_profile_prev_op;
    bool vm_profile_bigram_started;
    // Dynamic opcode trigram profile. Heap-allocated OP_COUNT^3 array (too
    // large for inline storage). NULL until profiling is enabled.
    uint64_t *vm_profile_trigram_counts;
    uint64_t vm_profile_trigram_total;
    int vm_profile_prev2_op;
    bool vm_profile_trigram_started;

    // Debugger state (enable via CCCC_ENABLE_DEBUGGER flag)
    Debugger dbg;

    DynamicLibrary *dynlibs;
    int dynlib_count;
    int dynlib_capacity;
    DynamicSymbol *dynsyms;
    int dynsym_count;
    int dynsym_capacity;
    int dyn_next_token;
    char *dyn_error;

    // Compiler state (preprocessor, parser, codegen)
    Compiler compiler;

    // VM-managed signal table
    SigSlot vm_sigslots[CCCC_NSIG];

    // VM-managed atexit()/at_quick_exit() handler lists (#738). These hold
    // guest function-pointer values (byte offsets or FFI tokens), NOT raw
    // pointers handed to the host's real atexit()/at_quick_exit() -- a guest
    // value passed straight through would crash the moment the host's libc
    // exit sequence actually invoked it as machine code. Drained in LIFO
    // (reverse registration) order by wrap_exit/wrap_quick_exit (stdlib.c)
    // and by cc_run's normal-return path (vm.c), each via a mechanism
    // appropriate to whether the GIL is held at that point -- see the
    // comment above cccc_call_guest_callback in internal.h.
    long long *atexit_handlers;
    int atexit_count;
    int atexit_cap;
    long long *at_quick_exit_handlers;
    int at_quick_exit_count;
    int at_quick_exit_cap;

    // GIL-backed pthread runtime state. Concrete pthread objects are kept
    // internal so the public CCCC struct does not expose host pthread ABI types.
    void *gil_mutex;
    int gil_initialized;
    ThreadRecord *active_thread;
    ThreadRecord *thread_records;
    PthreadKeyRecord *pthread_keys;
    int pthread_next_key;
    PthreadState *pthread_state;

    // Threading safety diagnostics (CCCC_THREAD_SAFETY)
    // Lock graph for lock-order inversion detection: parallel arrays of
    // (from_lock, to_lock) edges meaning "someone held from_lock when acquiring
    // to_lock". Guarded by the GIL (only one VM thread runs at a time).
    void **lock_graph_from;
    void **lock_graph_to;
    int    lock_graph_size;
    int    lock_graph_cap;
    // Race detection shadow map: address -> last thread that wrote without a lock.
    // Value is ThreadRecord* for worker threads or (void*)1 for main thread.
    HashMap race_shadow;
    // Atomic shadow map: address -> thread id that last accessed it atomically.
    // Used by check_race_access to detect non-atomic access to atomically-tagged
    // addresses (mixed atomic/non-atomic access). Tags set by ALDR/ASTR/AXCHG/ACAS.
    HashMap atomic_shadow;

    // Error handling (setjmp/longjmp for exception-like behavior)
    jmp_buf
        *error_jmp_buf;  // Jump buffer for error handling (NULL = use exit())
    char *error_message; // Last error message (when using longjmp)

    // Error collection (for reporting multiple errors)
    CompileError *errors;      // Linked list of collected errors
    CompileError *errors_tail; // Tail pointer for O(1) append
    int error_count;           // Number of errors collected
    int warning_count;         // Number of warnings collected
    int max_errors;            // Maximum errors before stopping (default: 20)
    bool collect_errors;       // Enable error collection mode
    bool warnings_as_errors;   // Treat warnings as errors (--Werror)
};

/*!
 @function cc_init
 @abstract Initialize an CCCC instance.
 @discussion The caller should allocate an `CCCC` struct (usually on the
             stack) and pass its pointer to this function. This sets up
             memory segments, default include paths, and other runtime
             defaults.
 @param vm Pointer to an uninitialized CCCC struct to initialize.
 @param flags Bitwise combination of CCCCFlags to enable features (0 for none).
*/
void cc_init(VirtualMachine *vm, uint32_t flags);

/*!
 @function cc_destroy
 @abstract Free resources owned by an CCCC instance.
 @discussion Does not free the `CCCC` struct itself; the caller is
             responsible for the memory of the struct if it was
             dynamically allocated.
 @param vm The CCCC instance to destroy.
*/
void cc_destroy(VirtualMachine *vm);

/*!
 @function cc_get_error_count
 @abstract Get the number of errors collected during compilation.
 @param vm The CCCC instance.
 @result The number of errors collected.
*/
int cc_get_error_count(VirtualMachine *vm);

/*!
 @function cc_get_warning_count
 @abstract Get the number of warnings collected during compilation.
 @param vm The CCCC instance.
 @result The number of warnings collected.
*/
int cc_get_warning_count(VirtualMachine *vm);

/*!
 @function cc_has_errors
 @abstract Check if any errors have been collected.
 @param vm The CCCC instance.
 @result True if errors exist, false otherwise.
*/
bool cc_has_errors(VirtualMachine *vm);

/*!
 @function cc_clear_errors
 @abstract Clear all collected errors and warnings.
 @discussion Useful for reusing a VM instance across multiple compilations.
 @param vm The CCCC instance.
*/
void cc_clear_errors(VirtualMachine *vm);

/*!
 @function cc_print_all_errors
 @abstract Print all collected errors and warnings to stderr.
 @param vm The CCCC instance.
*/
void cc_print_all_errors(VirtualMachine *vm);

/*!
 @function cc_print_stack_report
 @abstract Print stack instrumentation statistics and report.
 @discussion Outputs stack usage statistics including high water mark,
             variable access counts, and scope information. Only useful
             when stack instrumentation is enabled.
 @param vm The CCCC instance.
*/
void cc_print_stack_report(VirtualMachine *vm);

/*!
 @function cc_include
 @abstract Add a directory to the compiler's header search paths.
 @discussion This adds the path to the list of directories searched for
             "..." includes (quote includes).
 @param vm The CCCC instance.
 @param path Filesystem path to add to include search.
*/
void cc_include(VirtualMachine *vm, const char *path);

/*!
 @function cc_system_include
 @abstract Add a directory to the compiler's system header search paths.
 @discussion This adds the path to the list of directories searched for
             <...> includes (angle bracket includes). System include paths
             are searched after regular include paths for "..." includes.
 @param vm The CCCC instance.
 @param path Filesystem path to add to system include search.
*/
void cc_system_include(VirtualMachine *vm, const char *path);

/*!
 @function cc_define
 @abstract Define or override a preprocessor macro for the given VM.
 @param vm The CCCC instance.
 @param name Macro identifier (NUL-terminated).
 @param buf Macro replacement text (NUL-terminated).
*/
void cc_define(VirtualMachine *vm, char *name, char *buf);

/*!
 @function cc_undef
 @abstract Remove a preprocessor macro definition from the VM.
 @param vm The CCCC instance.
 @param name Macro identifier to remove.
*/
void cc_undef(VirtualMachine *vm, char *name);

/*!
 @function cc_set_asm_callback
 @abstract Register a callback invoked for `asm("...")` statements.
 @param vm The CCCC instance.
 @param callback Callback function pointer, or NULL to unregister.
 @param user_data Optional user context pointer passed to the callback.
*/
void cc_set_asm_callback(VirtualMachine *vm, AsmCallback callback, void *user_data);

/*!
 @function cc_ffi_allow
 @abstract Add a native function name to the FFI allow list.
 @discussion When the allow list is non-empty, only listed names may be called
             through registered FFI or runtime dynamic symbols.
*/
void cc_ffi_allow(VirtualMachine *vm, const char *name);

/*!
 @function cc_ffi_deny
 @abstract Add a native function name to the FFI deny list.
 @discussion The deny list is checked only when the allow list is empty.
*/
void cc_ffi_deny(VirtualMachine *vm, const char *name);

/*!
 @function cc_ffi_clear_allow_list
 @abstract Remove all names from the FFI allow list.
*/
void cc_ffi_clear_allow_list(VirtualMachine *vm);

/*!
 @function cc_ffi_clear_deny_list
 @abstract Remove all names from the FFI deny list.
*/
void cc_ffi_clear_deny_list(VirtualMachine *vm);

/*!
 @function cc_register_cfunc
 @abstract Register a native C function to be callable from VM code via FFI.
 @param vm The CCCC instance.
 @param name Function name (must match declarations in C source).
 @param func_ptr Pointer to the native C function.
 @param num_args Number of arguments the function expects.
 @param returns_double Return type: 0 = long long, 1 = double, 2 = float (#406).
 @discussion Registered functions can be called from C code compiled to VM
             bytecode. The CALLF instruction handles argument marshalling.
             All integer types are passed/returned as long long, doubles as
 double, and `float`-returning functions use returns_double=2.
*/
void cc_register_cfunc(VirtualMachine *vm, const char *name, void *func_ptr, int num_args,
                       int returns_double);

/*!
 @function cc_register_cfunc_ex
 @abstract Register a C function with detailed argument type information for
 correct FFI calling conventions.
 @param vm The CCCC instance.
 @param name Function name (must match declarations in C source).
 @param func_ptr Pointer to the native C function.
 @param num_args Total number of arguments.
 @param returns_double Return type: 0 = long long, 1 = double, 2 = float (#406).
 @param double_arg_mask Bitmask indicating which arguments are doubles. Bit N
 corresponds to argument N (0-indexed). For example: 0b11 = both args 0 and 1
 are doubles (e.g., pow(double, double)). 0b01 = only arg 0 is double (e.g.,
 ldexp(double, int)).
 @discussion Use this function instead of cc_register_cfunc() for functions that
 take multiple double arguments or mix double and integer arguments. The bitmask
 ensures correct calling conventions on all platforms. For functions with only
 integer arguments or single double arguments, cc_register_cfunc() is
 sufficient. Note: float-typed arguments are tracked separately via
 the per-call-site float_arg_mask computed in codegen; this registration-time
 mask only needs to flag double-typed arguments.
*/
void cc_register_cfunc_ex(VirtualMachine *vm, const char *name, void *func_ptr,
                          int num_args, int returns_double,
                          uint64_t double_arg_mask);

/*!
 @function cc_register_variadic_cfunc
 @abstract Register a variadic native C function to be callable from VM code via
 FFI.
 @param vm The CCCC instance.
 @param name Function name (must match declarations in C source).
 @param func_ptr Pointer to the native C variadic function.
 @param num_fixed_args Number of fixed arguments before the ... (e.g., printf
 has 1: format string).
 @param returns_double Return type: 0 = long long, 1 = double, 2 = float (#406).
 @discussion Variadic functions accept a variable number of arguments after the
 fixed arguments. CCCC uses platform native inline-assembly to call variadic
 functions. Example: printf has 1 fixed arg (format), fprintf has 2 (stream,
 format).
*/
void cc_register_variadic_cfunc(VirtualMachine *vm, const char *name, void *func_ptr,
                                int num_fixed_args, int returns_double);

/*!
 @function cc_load_stdlib
 @abstract Register all standard library functions available via FFI.
 @param vm The CCCC instance.
 @discussion Automatically registers 50+ standard library functions including:
             - Memory: malloc, free, calloc, realloc, memcpy, memmove, memset,
 memcmp
             - String: strlen, strcpy, strncpy, strcat, strcmp, strncmp, strchr,
 strstr
             - I/O: puts, putchar, getchar, fopen, fclose, fread, fwrite, fgetc,
 fputc
             - Math: sin, cos, tan, sqrt, pow, exp, log, floor, ceil, fabs
             - Conversion: atoi, atol, atof, strtol, strtod
             - System: exit, abort, system, open, close, read, write

             This function is automatically called by cc_init(), but can be
 called manually if you want to reset the FFI registry or initialize it
 separately.
*/
void cc_load_stdlib(VirtualMachine *vm);

/*!
 @function cc_dlsym
 @abstract Update an existing registered FFI function's pointer by name.
 @param vm The CCCC instance.
 @param name Function name to update.
 @param func_ptr New function pointer to assign.
 @param num_args Expected number of arguments (must match registered function).
 @param returns_double Expected return type (must match registered function);
 0 = long long, 1 = double, 2 = float (#406).
 @return 0 on success, -1 on error (function not found or signature mismatch).
 @discussion This function is useful for updating function pointers after
 loading a dynamic library, or for redirecting calls to different
 implementations. The function must already be registered via cc_register_cfunc
 or cc_register_variadic_cfunc.
*/
int cc_dlsym(VirtualMachine *vm, const char *name, void *func_ptr, int num_args,
             int returns_double);

/*!
 @function cc_dlopen
 @abstract Load a dynamic library and resolve all registered FFI functions.
 @param vm The CCCC instance.
 @param lib_path Path to the dynamic library (.so, .dylib, .dll) or NULL for
 default libraries.
 @return 0 on success, -1 on error.
 @discussion This function opens a dynamic library and attempts to resolve all
 currently registered FFI functions. Functions that cannot be resolved will
 print warnings but won't fail the entire operation. If lib_path is NULL, the
 function searches in default system libraries.

             Platform-specific behavior:
             - Unix: Uses dlopen/dlsym to load .so/.dylib files
             - Windows: Uses LoadLibrary/GetProcAddress to load .dll files

             The library handle is not closed after loading to keep function
 pointers valid.
*/
int cc_dlopen(VirtualMachine *vm, const char *lib_path);

/*!
 @function cc_load_libc
 @abstract Load the platform's standard C library and resolve FFI functions.
 @param vm The CCCC instance.
 @return 0 on success, -1 on error.
 @discussion This function automatically detects and loads the correct C library
 for the current platform:
             - macOS: /usr/lib/libSystem.dylib
             - Linux: /lib64/libc.so.6 (or /lib/libc.so.6 on 32-bit)
             - FreeBSD: /lib/libc.so.7
             - Windows: msvcrt.dll
             This is useful when you want to load stdlib functions dynamically
 instead of registering them with explicit function pointers.
*/
int cc_load_libc(VirtualMachine *vm);

/*!
 @function cc_preprocess
 @abstract Run the preprocessor on a C source file and return a token stream.
 @param vm The CCCC instance.
 @param path Path to the source file to preprocess.
 @return Head of the token stream (linked Token list). Caller owns tokens.
*/
Token *cc_preprocess(VirtualMachine *vm, const char *path);

/*!
 @function cc_parse
 @abstract Parse a preprocessed token stream into an AST and produce
           a linked list of top-level Obj declarations.
 @param vm The CCCC instance.
 @param tok Head of the preprocessed token stream.
 @return Linked list of top-level Obj representing globals and functions.
*/
Obj *cc_parse(VirtualMachine *vm, Token *tok);

/*!
 @function cc_parse_expr
 @abstract Parse a single C expression from token stream.
 @param vm The CCCC instance.
 @param rest Pointer to receive the remaining tokens after parsing.
 @param tok Head of the token stream to parse.
 @return AST node representing the parsed expression.
*/
Node *cc_parse_expr(VirtualMachine *vm, Token **rest, Token *tok);

/*!
 @function cc_parse_assign
 @abstract Parse an assignment expression from token stream (stops at commas).
 @discussion Used for parsing function arguments and other contexts where commas
             are separators rather than operators.
 @param vm The CCCC instance.
 @param rest Pointer to receive the remaining tokens after parsing.
 @param tok Head of the token stream to parse.
 @return AST node representing the parsed assignment expression.
*/
Node *cc_parse_assign(VirtualMachine *vm, Token **rest, Token *tok);

/*!
 @function cc_parse_stmt
 @abstract Parse a single C statement from token stream.
 @param vm The CCCC instance.
 @param rest Pointer to receive the remaining tokens after parsing.
 @param tok Head of the token stream to parse.
 @return AST node representing the parsed statement.
*/
Node *cc_parse_stmt(VirtualMachine *vm, Token **rest, Token *tok);

/*!
 @function cc_parse_compound_stmt
 @abstract Parse a compound statement (block) from token stream.
 @param vm The CCCC instance.
 @param rest Pointer to receive the remaining tokens after parsing.
 @param tok Head of the token stream to parse (should be opening brace).
 @return AST node representing the parsed compound statement.
*/
Node *cc_parse_compound_stmt(VirtualMachine *vm, Token **rest, Token *tok);
int64_t cc_eval(VirtualMachine *vm, Node *node);
double  cc_eval_double(VirtualMachine *vm, Node *node);
void cc_init_parser(VirtualMachine *vm);

/*!
 @enum ReplUnitKind
 @abstract Classification result from cc_parse_repl_unit (ticket #661).
*/
typedef enum {
    REPL_UNIT_EMPTY, // line was empty/whitespace-only after tokenizing
    REPL_UNIT_DECL,  // one or more declarations were added to vm->compiler.globals
    REPL_UNIT_EXPR,  // *out_expr holds a parsed+typed expression Node
} ReplUnitKind;

/*!
 @function cc_parse_repl_unit
 @abstract Parse and classify one top-level unit typed at the REPL prompt.
 @param vm The CCCC instance. vm->compiler.scope must already hold a
           persistent global scope (see parse()) -- this function does not
           enter/leave scope or reset vm->compiler.globals; declarations
           accumulate across calls, exactly like typing more source into an
           already-open translation unit.
 @param tok Head of the tokenized (and pp-converted) line/block.
 @param out_expr Receives the parsed expression Node when the return value is
           REPL_UNIT_EXPR; set to NULL otherwise.
 @return REPL_UNIT_EMPTY, REPL_UNIT_DECL, or REPL_UNIT_EXPR.
 @discussion Classification peeks the first token with the same predicate
             used to distinguish a block-scope declaration from a statement
             (is_decl_start, which consults the persistent typedef/keyword
             table) so `a * b;` parses as a declaration when `a` is a typedef
             and as an expression otherwise. A parse or type error calls
             error_tok(), which longjmps via vm->error_jmp_buf if the caller
             installed one -- this function does not itself catch errors; the
             caller is responsible for snapshotting/rolling back scope state
             around the call (see cc_run_repl in src/repl.c).
*/
ReplUnitKind cc_parse_repl_unit(VirtualMachine *vm, Token *tok, Node **out_expr);

/*!
 @function cc_execute_inline_macros
 @abstract Execute all inline macros before parsing.
 @discussion Compiles and executes every [[cccc::macro(inline)]] (or
             __attribute__((macro(inline)))) macro. Each inline macro runs
             automatically at its declaration point (no explicit call needed).
             Generated functions are stored in vm->compiler.macro_globals and
             synthetic forward declarations are prepended to every input token
             stream so the parser can resolve calls to generated functions
             without manual forward declarations. Must be called after all
             preprocessing and before cc_parse.
 @param vm The CCCC instance.
 @param input_tokens Array of preprocessed token streams (one per source file).
 @param count Number of token streams in the array.
*/
void cc_execute_inline_macros(VirtualMachine *vm, Token **input_tokens, int count);
void cc_record_emit_source(VirtualMachine *vm, const char *source);
void cc_record_emit_object(VirtualMachine *vm, Obj *obj);

/*!
 @function cc_expand_macros
 @abstract Expand all macro calls in the AST.
 @discussion Walks the AST and replaces ND_MACRO_CALL nodes with the
             generated AST from executing the corresponding macro function.
             Must be called after cc_parse and before cc_compile.
 @param vm The CCCC instance.
 @param prog Linked list of top-level Obj returned by cc_parse.
*/
void cc_expand_macros(VirtualMachine *vm, Obj *prog);

/*!
 @function cc_eager_expand_macro_call
 @abstract Expand a single ND_MACRO_CALL node using the already-compiled macros.
 @discussion Called from cc_finalize_macro_gvar_inits while in_macro_expansion is true.
             Must only be called after compile_all_macros has run (i.e. from within
             cc_expand_macros or cc_finalize_macro_gvar_inits).
 @param vm The CCCC instance.
 @param node The node to expand; must be non-NULL.
 @returns The expanded (replacement) node, or the original node if not ND_MACRO_CALL.
*/
Node *cc_eager_expand_macro_call(VirtualMachine *vm, Node *node);

/*!
 @function cc_finalize_macro_gvar_inits
 @abstract Finalize global variable initializers that were deferred due to macro calls.
 @discussion During cc_parse, gvar initializers containing ND_MACRO_CALL are deferred
             (their has_pending_macro_init flag is set and the Initializer tree stored in
             constexpr_init). This function, called from cc_expand_macros after macros are
             compiled and expanded, processes those deferred initializers: it expands the
             macro calls via transform_node and then serializes the result to .data.
 @param vm The CCCC instance.
 @param prog Linked list of top-level Obj returned by cc_parse.
*/
void cc_finalize_macro_gvar_inits(VirtualMachine *vm, Obj *prog);

/*!
 @function cc_serialize_program
 @abstract Serialize a program AST back to C source code.
 @discussion Used with -m/--dump-expanded and -G flags to output
             macro-expanded source that can be compiled with gcc or other
             C compilers.
 @param f Output file stream.
 @param vm The CCCC instance.
 @param prog Program AST to serialize.
 @param generated_only If true, only serialize objects created by pragma macros
                       (those with is_macro_generated set). Used with -G flag.
*/
void cc_serialize_program(FILE *f, VirtualMachine *vm, Obj *prog, bool generated_only);

/*!
 @function cc_link_progs
 @abstract Link multiple parsed programs (Obj lists) into a single program.
 @discussion Takes an array of Obj* programs and combines them into one
             linked list. This allows multiple source files to be compiled
             together into a single program. The function handles duplicate
             definitions by preferring definitions over declarations.
 @param vm The CCCC instance.
 @param progs Array of Obj* programs (linked lists from cc_parse).
 @param count Number of programs in the array.
 @return A single merged Obj* linked list containing all objects.
*/
Obj *cc_link_progs(VirtualMachine *vm, Obj **progs, int count);

/*!
 @function cc_compile
 @abstract Compile the parsed program (Obj list) into VM bytecode.
 @param vm The CCCC instance.
 @param prog Linked list of top-level Obj returned by cc_parse.
*/
void cc_compile(VirtualMachine *vm, Obj *prog);

/*!
 @function cc_repl_compile_new
 @abstract Incrementally compile only the globals prepended to
           vm->compiler.globals since `old_head` -- for the REPL (ticket #661).
 @param vm The CCCC instance. On first call this lazily allocates VM segments
           the same way cc_compile does.
 @param old_head The value of vm->compiler.globals captured *before* the new
           unit was parsed/synthesized. Everything from vm->compiler.globals
           up to (but not including) old_head is treated as new.
 @discussion Unlike cc_compile/gen(), this never resets or re-lays-out
             already-compiled globals or functions: their code_addr, data
             offsets, and current runtime contents are left untouched, so
             prior REPL evaluations' side effects (mutated globals, pointers
             into the data/text segment) remain valid. New function calls may
             still target any previously-compiled function.
*/
void cc_repl_compile_new(VirtualMachine *vm, Obj *old_head);

/*!
 @struct CcExprSnapshot
 @abstract Captured compiler scope/globals/locals state, taken before parsing
           or synthesizing a one-off expression (REPL line or debugger
           condition) so a failed attempt can be rolled back without
           disturbing earlier, successful state. Implemented in src/repl.c;
           shared by the REPL (src/repl.c) and the debugger's conditional
           breakpoint evaluator (src/debugger.c, ticket 113).
*/
typedef struct {
    HashMap var_map_snap;
    HashMap tag_map_snap;
    VarScopeNode *vars_head;
    TagScopeNode *tags_head;
    Obj *globals_head;
    Obj *locals_head;
} CcExprSnapshot;

/*!
 @function cc_expr_snapshot
 @abstract Capture the current scope/globals/locals state.
 @param vm The CCCC instance. vm->compiler.scope must be non-NULL.
*/
CcExprSnapshot cc_expr_snapshot(VirtualMachine *vm);

/*!
 @function cc_expr_snapshot_discard
 @abstract Free the bucket-array copies taken by cc_expr_snapshot, for a unit
           that succeeded and whose state should be kept as-is.
*/
void cc_expr_snapshot_discard(CcExprSnapshot *snap);

/*!
 @function cc_expr_snapshot_restore
 @abstract Roll vm->compiler.scope/globals/locals back to a prior
           cc_expr_snapshot, discarding anything a failed unit added.
           Obj/Node/scope-entry memory the failed unit allocated is
           arena-allocated and simply becomes unreachable rather than freed.
*/
void cc_expr_snapshot_restore(VirtualMachine *vm, CcExprSnapshot *snap);

/*!
 @function cc_expr_exec_wrapper
 @abstract Run a compiled zero-argument wrapper function once on the
           persistent VM and read its result, mirroring the save/reset/run/
           restore pattern execute_macro_fn (src/macros.c) uses for comptime
           macro bodies -- but reading the *runtime* return registers
           (REG_A0 / FREG_A0) rather than a comptime AST-node result.
 @param vm The CCCC instance.
 @param fn The compiled wrapper function to execute.
 @param out_i Receives REG_A0 (integer/pointer result). May be NULL.
 @param out_f Receives FREG_A0.f64 (float/double result). May be NULL.
*/
void cc_expr_exec_wrapper(VirtualMachine *vm, Obj *fn, long long *out_i,
                          double *out_f);

/*!
 @function cc_run
 @abstract Execute the compiled program within the VM.
 @param vm The CCCC instance containing compiled bytecode.
 @param argc Argument count to pass to the program's main().
 @param argv Argument vector (NUL-terminated array of strings).
 @return Program exit code returned by main().
*/
int cc_run(VirtualMachine *vm, int argc, char **argv);

/*!
 @function cc_run_at
 @abstract Execute the compiled program starting at a specific bytecode address.
 @param vm The CCCC instance containing compiled bytecode.
 @param entry Program counter (instruction index) to start execution from.
 @param argc Argument count to pass to the program.
 @param argv Argument vector.
 @return Program exit code.
*/
int cc_run_at(VirtualMachine *vm, Pc entry, int argc, char **argv);

/*!
 @function cc_print_tokens
 @abstract Print a token stream to stdout (useful for debugging the
           preprocessor and tokenizer).
 @param tok Head of the token stream to print.
*/
void cc_print_tokens(Token *tok);

/*!
 @function cc_disassemble
 @abstract Disassemble the compiled bytecode to stdout.
 @param vm The CCCC instance containing compiled bytecode.
*/
void cc_disassemble(VirtualMachine *vm);

/*!
 @function cc_output_json
 @abstract Output C header declarations as JSON for FFI wrapper generation.
 @discussion Serializes function signatures, struct/union definitions, enum
             declarations, and global variables from the parsed AST to JSON
             format. The output includes full type information with recursive
             expansion of pointers, arrays, and aggregate types. Storage class
             specifiers (static, extern) are included. Function bodies are not
             serialized - only signatures.

             The JSON output format is:
             {
               "functions": [...],
               "structs": [...],
               "unions": [...],
               "enums": [...],
               "variables": [...]
             }
 @param f Output file stream (e.g., stdout or file opened with fopen).
 @param prog Head of the parsed AST object list (from cc_parse).
*/
void cc_output_json(FILE *f, Obj *prog);

/*!
 @function cc_output_source_map_json
 @abstract Output source map as JSON to a file for debugging tools.
 @discussion Outputs source maps in JSON format, mapping bytecode PC offsets
             to source locations with file, line, and column information.
 @param vm The CCCC instance with source map data.
 @param f Output file stream.
*/
void cc_output_source_map_json(VirtualMachine *vm, FILE *f);

/*!
 @function cc_save_bytecode
 @abstract Save compiled bytecode to a file for later execution.
 @discussion Serializes the text segment, data segment, and necessary
             metadata to a binary file. The file can be loaded and executed
             by cc_load_bytecode().
 @param vm The CCCC instance containing compiled bytecode.
 @param path Output file path.
 @return 0 on success, -1 on error.
*/
int cc_save_bytecode(VirtualMachine *vm, const char *path);

/*!
 @function cc_write_bytecode
 @abstract Write compiled bytecode to an open stdio stream.
 @discussion Serializes the text segment, data segment, and necessary
             metadata as a binary blob to @c f. The output is the same
             format as cc_save_bytecode(), so it can be loaded with
             cc_load_bytecode() after being written to a file.
 @param vm The CCCC instance containing compiled bytecode.
 @param f Output stream opened in binary mode (e.g. stdout via
          @c freopen with "wb", or a file returned by fopen("wb")).
 @return 0 on success, -1 on error.
*/
int cc_write_bytecode(VirtualMachine *vm, FILE *f);

/*!
 @function cc_load_bytecode
 @abstract Load compiled bytecode from a file.
 @discussion Deserializes bytecode previously saved with cc_save_bytecode()
             and prepares the VM for execution with cc_run().
 @param vm The CCCC instance to load bytecode into.
 @param path Input file path.
 @return 0 on success, -1 on error.
*/
int cc_load_bytecode(VirtualMachine *vm, const char *path);

/*!
 @function cc_load_module
 @abstract Append a compiled bytecode module (.c4d or .c4a) into a running VM.
 @discussion Merges the module's text and data segments onto the host VM.
             All absolute-PC jump/call operands and text-relative (LTA3)
             immediates in the appended text are patched by the pre-append text
             size. Data pointer slots are re-anchored. FFI, TLS, and return-buffer
             metadata are merged. The module should be compiled with `-c bytecode`
             (no main() required). If the host VM has unresolved text relocations
             (from compiling with external CALL sites), they are patched using the
             module's exported symbol table (#565).
 @param vm   The running CCCC VM instance to load the module into.
 @param path Path to the .c4d (or .c4a) bytecode module file.
 @return 0 on success, -1 on error.
*/
int cc_load_module(VirtualMachine *vm, const char *path);

/*!
 @function cc_link_bytecode
 @abstract Link a compiled bytecode library (.c4a) into a VM at compile time.
 @discussion Appends the library's text and data segments onto the VM (like
             cc_load_module), then resolves the VM's pending text relocations
             using the library's exported symbol table. Used to implement the
             `--link` flag and the build-system bytecode linker pass (#565).
 @param vm   The VM instance to link the library into (must have been compiled).
 @param path Path to the .c4a bytecode library file.
 @return 0 on success, -1 on error.
*/
int cc_link_bytecode(VirtualMachine *vm, const char *path);

/*!
 @function cc_add_breakpoint
 @abstract Add a breakpoint at a specific program counter.
 @param vm The CCCC instance.
 @param pc Instruction-word index where breakpoint should be set.
 @return Breakpoint index, or -1 if failed (too many breakpoints).
*/
int cc_add_breakpoint(VirtualMachine *vm, Pc pc);

/*!
 @function cc_remove_breakpoint
 @abstract Remove a breakpoint by index.
 @param vm The CCCC instance.
 @param index Breakpoint index to remove.
*/
void cc_remove_breakpoint(VirtualMachine *vm, int index);

/*!
 @function cc_debug_repl
 @abstract Enter interactive debugger REPL (Read-Eval-Print Loop).
 @param vm The CCCC instance.
 @discussion Provides an interactive shell for debugging with commands like:
             - break/b: Set breakpoint
             - continue/c: Continue execution
             - step/s: Single step
             - next/n: Step over
             - finish/f: Step out
             - print/p: Print registers
             - stack/st: Print stack
             - help/h: Show help
*/
void cc_debug_repl(VirtualMachine *vm);

/*!
 @function cc_run_repl
 @abstract Run an interactive top-level read-eval-print loop on a VM.
 @param vm The CCCC instance. Must already be initialized via cc_init and
            configured (flags, entry name, FFI policy, etc.) exactly as for
            a normal compile; cc_run_repl does not call cc_init itself.
 @discussion Distinct from cc_debug_repl (src/debugger.c), which is scoped to
             breakpoint-time inspection of an already-running program.
             cc_run_repl instead drives a persistent top-level session: it
             reads C source a line (or multi-line block) at a time, classifies
             each unit as a declaration (persisted into the session's global
             scope) or an expression (compiled into a synthetic wrapper
             function and executed on the VM, with the typed result printed),
             and supports session commands (:help, :quit, :type, :load).
             A failed parse rolls back the session's symbol-table state
             rather than corrupting it. Returns when the session ends
             (:quit or EOF on stdin).
*/
void cc_run_repl(VirtualMachine *vm);

/*!
 @function cc_add_watchpoint
 @abstract Add a watchpoint at a specific memory address.
 @param vm The CCCC instance.
 @param address Memory address to watch.
 @param size Size of memory region to watch (in bytes).
 @param type Watchpoint type flags (WATCH_READ | WATCH_WRITE | WATCH_CHANGE).
 @param expr Original expression string (for display purposes).
 @return Watchpoint index, or -1 if failed (too many watchpoints).
*/
int cc_add_watchpoint(VirtualMachine *vm, void *address, int size, int type,
                      const char *expr);

/*!
 @function cc_remove_watchpoint
 @abstract Remove a watchpoint by index.
 @param vm The CCCC instance.
 @param index Watchpoint index to remove.
*/
void cc_remove_watchpoint(VirtualMachine *vm, int index);

/*!
 @function cc_pc_to_name
 @abstract Map a program counter to the name of the enclosing function.
 @param vm The CCCC instance.
 @param pc Instruction-word index (as returned by __builtin_return_address).
 @return The C function name string if the PC falls within a known function's
         code range, or NULL if not found. The returned pointer is owned by the
         VM and remains valid for the lifetime of the vm. This function does
         NOT require the debugger / -g to be enabled.
*/
const char *cc_pc_to_name(VirtualMachine *vm, Pc pc);

/*!
 @function cc_pc_to_source
 @abstract Map a program counter to a source file name and line number.
 @param vm The CCCC instance.
 @param pc Instruction-word index (as returned by __builtin_return_address).
 @param out_file On success, receives a pointer to the source file name string
                 (owned by the VM). Set to NULL on failure.
 @param out_line On success, receives the 1-based line number. Set to 0 on
                 failure.
 @return 1 if the source location was found, 0 if not. Requires the program to
         have been compiled with -g (debugger/source-map enabled); without -g
         this always returns 0 with out_file=NULL, out_line=0.
*/
int cc_pc_to_source(VirtualMachine *vm, Pc pc, const char **out_file,
                    int *out_line);

/*!
 @function cc_get_source_location
 @abstract Get source file location for a given program counter.
 @param vm The CCCC instance.
 @param pc Instruction-word index.
 @param out_file Pointer to receive the source File pointer (can be NULL).
 @param out_line Pointer to receive the line number (can be NULL).
 @return 1 if location found, 0 if not found.
*/
int cc_get_source_location(VirtualMachine *vm, Pc pc, File **out_file,
                           int *out_line, int *out_col);

/*!
 @function cc_find_pc_for_source
 @abstract Find program counter index for a given source location.
 @param vm The CCCC instance.
 @param file Source file (NULL to search in any file).
 @param line Line number to find.
 @return Program counter index, or CCCC_INVALID_PC if not found.
*/
Pc cc_find_pc_for_source(VirtualMachine *vm, File *file, int line);

/*!
 @function cc_find_function_entry
 @abstract Find program counter index for a function entry point by name.
 @param vm The CCCC instance.
 @param name Function name to find.
 @return Program counter index, or CCCC_INVALID_PC if not found.
*/
Pc cc_find_function_entry(VirtualMachine *vm, const char *name);

/*!
 @function cc_lookup_symbol
 @abstract Look up a debug symbol by name in current scope.
 @param vm The CCCC instance.
 @param name Symbol name to look up.
 @return Pointer to DebugSymbol if found, NULL otherwise.
*/
DebugSymbol *cc_lookup_symbol(VirtualMachine *vm, const char *name);

/*!
 @function cc_dlopen
 @abstract Open a dynamic library.
 @param vm The CCCC instance.
 @param lib_path Path to the dynamic library to open.
 @return 0 on success, -1 on failure.
*/
int cc_dlopen(VirtualMachine *vm, const char *lib_path);

/*!
 @function cc_dlsym
 @abstract Resolve a symbol in a dynamic library.
 @param vm The CCCC instance.
 @param name Name of the symbol to resolve.
 @param func_ptr Pointer to the function to resolve.
 @param num_args Number of arguments the function expects.
 @param returns_double 1 if function returns double, 0 if returns long long.
 @return 0 on success, -1 on failure.
*/
int cc_dlsym(VirtualMachine *vm, const char *name, void *func_ptr, int num_args,
             int returns_double);

#ifdef __cplusplus
}
#endif
#endif
