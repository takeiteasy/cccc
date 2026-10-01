/*
 CCCC: Comprehensiev C Compensation Compiler - OpenCL C Prelude

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

/*!
 * @file opencl.h
 * @brief Maps OpenCL C 1.2 onto the kernel model of <cccc/kernel.h>. Included
 * automatically ahead of every input read as OpenCL C (`-x cl`, `.cl`). See
 * man/OPENCL.md.
 */

#ifndef CCCC_OPENCL_H
#define CCCC_OPENCL_H

#include <cccc/kernel.h>

#define __OPENCL_C_VERSION__ 120
#define __OPENCL_VERSION__   120
#define CL_VERSION_1_0       100
#define CL_VERSION_1_1       110
#define CL_VERSION_1_2       120
#define __ENDIAN_LITTLE__    1

// Qualifiers. The bare spellings are reserved words in OpenCL C, so they are
// macros here and cannot be used as identifiers. The bodies use the
// underscored attribute names so that the macros do not expand themselves.
#define __kernel   [[cccc::__kernel__]]
#define kernel     [[cccc::__kernel__]]
#define __global   [[cccc::__global__]]
#define global     [[cccc::__global__]]
#define __local    [[cccc::__local__]]
#define local      [[cccc::__local__]]
#define __constant [[cccc::__constant__]]
#define constant   [[cccc::__constant__]]
#define __private  [[cccc::__private__]]
#define private    [[cccc::__private__]]

typedef unsigned char  uchar;
typedef unsigned short ushort;
typedef unsigned int   uint;
typedef unsigned long  ulong;

// A cast's type-name cannot start with an address-space attribute
typedef [[cccc::__generic__]] unsigned __cl_generic_uint;

// Vector types of 16, 32 and 64 bytes. A 3-lane type is stored as four lanes
// and exposes three. Narrower ones (float2, int2, char4, ...) do not exist
// yet. char is signed here whatever the target's char is.
#define __CL_VECTOR(base, name, bytes)                                         \
    typedef base name __attribute__((vector_size(bytes)))
#define __CL_VECTOR3(base, name, bytes)                                        \
    typedef base name __attribute__((vector_size(bytes), cccc_visible_lanes(3)))

__CL_VECTOR(signed char, char16, 16);
__CL_VECTOR(unsigned char, uchar16, 16);
__CL_VECTOR(short, short8, 16);
__CL_VECTOR(short, short16, 32);
__CL_VECTOR(unsigned short, ushort8, 16);
__CL_VECTOR(unsigned short, ushort16, 32);
__CL_VECTOR3(int, int3, 16);
__CL_VECTOR(int, int4, 16);
__CL_VECTOR(int, int8, 32);
__CL_VECTOR(int, int16, 64);
__CL_VECTOR3(unsigned int, uint3, 16);
__CL_VECTOR(unsigned int, uint4, 16);
__CL_VECTOR(unsigned int, uint8, 32);
__CL_VECTOR(unsigned int, uint16, 64);
__CL_VECTOR(long, long2, 16);
__CL_VECTOR3(long, long3, 32);
__CL_VECTOR(long, long4, 32);
__CL_VECTOR(long, long8, 64);
__CL_VECTOR(unsigned long, ulong2, 16);
__CL_VECTOR3(unsigned long, ulong3, 32);
__CL_VECTOR(unsigned long, ulong4, 32);
__CL_VECTOR(unsigned long, ulong8, 64);
__CL_VECTOR3(float, float3, 16);
__CL_VECTOR(float, float4, 16);
__CL_VECTOR(float, float8, 32);
__CL_VECTOR(float, float16, 64);

#define get_work_dim()     cccc_work_dim()
#define get_global_id(d)   cccc_global_id(d)
#define get_local_id(d)    cccc_local_id(d)
#define get_group_id(d)    cccc_group_id(d)
#define get_global_size(d) cccc_global_size(d)
#define get_local_size(d)  cccc_local_size(d)
#define get_num_groups(d)  cccc_num_groups(d)
// TODO: cccc_launch has no offset, so this is always 0; #1413
#define get_global_offset(d)   ((size_t)0)

#define CLK_LOCAL_MEM_FENCE    CCCC_LOCAL_FENCE
#define CLK_GLOBAL_MEM_FENCE   CCCC_GLOBAL_FENCE

#define barrier(flags)         cccc_barrier(flags)
#define mem_fence(flags)       __atomic_thread_fence(__ATOMIC_SEQ_CST)
#define read_mem_fence(flags)  __atomic_thread_fence(__ATOMIC_SEQ_CST)
#define write_mem_fence(flags) __atomic_thread_fence(__ATOMIC_SEQ_CST)

#define atomic_add(p, v)       __atomic_fetch_add(p, v, __ATOMIC_SEQ_CST)
#define atomic_sub(p, v)       __atomic_fetch_sub(p, v, __ATOMIC_SEQ_CST)
#define atomic_and(p, v)       __atomic_fetch_and(p, v, __ATOMIC_SEQ_CST)
#define atomic_or(p, v)        __atomic_fetch_or(p, v, __ATOMIC_SEQ_CST)
#define atomic_xor(p, v)       __atomic_fetch_xor(p, v, __ATOMIC_SEQ_CST)
#define atomic_inc(p)          __atomic_fetch_add(p, 1, __ATOMIC_SEQ_CST)
#define atomic_dec(p)          __atomic_fetch_sub(p, 1, __ATOMIC_SEQ_CST)

// Return the old value. typeof_unqual keeps the temporary out of the pointee's
// address space.
//
// atomic_xchg also takes a float, which no atomic builtin does: the float case
// exchanges its bits through an unsigned view of the pointer. Only the
// selected _Generic branch is compiled.
#define __CL_XCHG_F32(p, val)                                                  \
    ({                                                                         \
        union {                                                                \
            float    f;                                                        \
            unsigned u;                                                        \
        } __cl_in  = {.f = (val)}, __cl_out;                                   \
        __cl_out.u = __atomic_exchange_n((__cl_generic_uint *)(p), __cl_in.u,  \
                                         __ATOMIC_SEQ_CST);                    \
        __cl_out.f;                                                            \
    })
#define atomic_xchg(p, val)                                                    \
    _Generic(*(p),                                                             \
        float: __CL_XCHG_F32(p, val),                                          \
        default: __atomic_exchange_n(p, val, __ATOMIC_SEQ_CST))
#define atomic_cmpxchg(p, cmp, val)                                            \
    ({                                                                         \
        auto __cl_p                   = (p);                                   \
        typeof_unqual(*__cl_p) __cl_e = (cmp);                                 \
        __atomic_compare_exchange_n(__cl_p, &__cl_e, (val), 0,                 \
                                    __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);       \
        __cl_e;                                                                \
    })
#define __CL_ATOMIC_EXTREMUM(p, val, stops)                                    \
    ({                                                                         \
        auto __cl_p                   = (p);                                   \
        typeof_unqual(*__cl_p) __cl_v = (val);                                 \
        typeof_unqual(*__cl_p) __cl_o =                                        \
            __atomic_load_n(__cl_p, __ATOMIC_SEQ_CST);                         \
        while (stops && !__atomic_compare_exchange_n(__cl_p, &__cl_o, __cl_v,  \
                                                     0, __ATOMIC_SEQ_CST,      \
                                                     __ATOMIC_SEQ_CST)) {      \
        }                                                                      \
        __cl_o;                                                                \
    })
#define atomic_min(p, val) __CL_ATOMIC_EXTREMUM(p, val, __cl_v < __cl_o)
#define atomic_max(p, val) __CL_ATOMIC_EXTREMUM(p, val, __cl_v > __cl_o)

// OpenCL 1.0 extension spellings
#define atom_add     atomic_add
#define atom_sub     atomic_sub
#define atom_and     atomic_and
#define atom_or      atomic_or
#define atom_xor     atomic_xor
#define atom_xchg    atomic_xchg
#define atom_inc     atomic_inc
#define atom_dec     atomic_dec
#define atom_cmpxchg atomic_cmpxchg
#define atom_min     atomic_min
#define atom_max     atomic_max

#endif
