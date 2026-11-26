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

// The builtin registry: every `__builtin_*`, `__atomic_*`, `__sync_*` and
// `__c11_atomic_*` name the parser accepts by itself, read by __has_builtin and
// by --output-compiler. The libm and `_chk` families are answered by their own
// tables in parse_decl.c.

#include "./internal.h"

// `cc` is the compilers whose __has_builtin reports the name (ATTR_CC_*), 0
// for a builtin only cccc has. Sorted by strcmp; tests/host/test_builtin_list.c
// checks it against the parser, tests/smoke/output_compiler.sh against gcc and
// clang.
static const BuiltinDef builtins[] = {
    {"__atomic_add_fetch", ATTR_CC_BOTH},
    {"__atomic_always_lock_free", ATTR_CC_BOTH},
    {"__atomic_and_fetch", ATTR_CC_BOTH},
    {"__atomic_clear", ATTR_CC_BOTH},
    {"__atomic_compare_exchange", ATTR_CC_BOTH},
    {"__atomic_compare_exchange_n", ATTR_CC_BOTH},
    {"__atomic_exchange", ATTR_CC_BOTH},
    {"__atomic_exchange_n", ATTR_CC_BOTH},
    {"__atomic_fetch_add", ATTR_CC_BOTH},
    {"__atomic_fetch_and", ATTR_CC_BOTH},
    {"__atomic_fetch_nand", ATTR_CC_BOTH},
    {"__atomic_fetch_or", ATTR_CC_BOTH},
    {"__atomic_fetch_sub", ATTR_CC_BOTH},
    {"__atomic_fetch_xor", ATTR_CC_BOTH},
    {"__atomic_is_lock_free", ATTR_CC_BOTH},
    {"__atomic_load", ATTR_CC_BOTH},
    {"__atomic_load_n", ATTR_CC_BOTH},
    {"__atomic_nand_fetch", ATTR_CC_BOTH},
    {"__atomic_or_fetch", ATTR_CC_BOTH},
    {"__atomic_signal_fence", ATTR_CC_BOTH},
    {"__atomic_store", ATTR_CC_BOTH},
    {"__atomic_store_n", ATTR_CC_BOTH},
    {"__atomic_sub_fetch", ATTR_CC_BOTH},
    {"__atomic_test_and_set", ATTR_CC_BOTH},
    {"__atomic_thread_fence", ATTR_CC_BOTH},
    {"__atomic_xor_fetch", ATTR_CC_BOTH},
    {"__builtin_add_overflow", ATTR_CC_BOTH},
    {"__builtin_alloca", ATTR_CC_BOTH},
    {"__builtin_alloca_with_align", ATTR_CC_BOTH},
    {"__builtin_assume", ATTR_CC_CLANG},
    {"__builtin_atomic_exchange", 0},
    {"__builtin_atomic_load", 0},
    {"__builtin_atomic_signal_fence", 0},
    {"__builtin_atomic_store", 0},
    {"__builtin_atomic_thread_fence", 0},
    {"__builtin_bswap16", ATTR_CC_BOTH},
    {"__builtin_bswap32", ATTR_CC_BOTH},
    {"__builtin_bswap64", ATTR_CC_BOTH},
    {"__builtin_bzero", ATTR_CC_BOTH},
    {"__builtin_cccc_dynamic_check", 0},
    {"__builtin_choose_expr", ATTR_CC_BOTH},
    {"__builtin_classify_type", ATTR_CC_BOTH},
    {"__builtin_clz", ATTR_CC_BOTH},
    {"__builtin_clzll", ATTR_CC_BOTH},
    {"__builtin_compare_and_swap", 0},
    {"__builtin_constant_p", ATTR_CC_BOTH},
    {"__builtin_convertvector", ATTR_CC_BOTH},
    {"__builtin_ctz", ATTR_CC_BOTH},
    {"__builtin_ctzll", ATTR_CC_BOTH},
    {"__builtin_debugtrap", ATTR_CC_CLANG},
    {"__builtin_decimal_to_chars", 0},
    {"__builtin_dynamic_object_size", ATTR_CC_BOTH},
    {"__builtin_expect", ATTR_CC_BOTH},
    {"__builtin_expect_with_probability", ATTR_CC_BOTH},
    {"__builtin_ffs", ATTR_CC_BOTH},
    {"__builtin_ffsll", ATTR_CC_BOTH},
    {"__builtin_frame_address", ATTR_CC_BOTH},
    {"__builtin_free", ATTR_CC_BOTH},
    {"__builtin_huge_val", ATTR_CC_BOTH},
    {"__builtin_huge_valf", ATTR_CC_BOTH},
    {"__builtin_huge_vall", ATTR_CC_BOTH},
    {"__builtin_inf", ATTR_CC_BOTH},
    {"__builtin_infd128", ATTR_CC_GCC},
    {"__builtin_infd32", ATTR_CC_GCC},
    {"__builtin_infd64", ATTR_CC_GCC},
    {"__builtin_inff", ATTR_CC_BOTH},
    {"__builtin_infl", ATTR_CC_BOTH},
    {"__builtin_isfinite", ATTR_CC_BOTH},
    {"__builtin_isinf", ATTR_CC_BOTH},
    {"__builtin_isnan", ATTR_CC_BOTH},
    {"__builtin_kernel_launch", 0},
    {"__builtin_kernel_local", 0},
    {"__builtin_malloc", ATTR_CC_BOTH},
    {"__builtin_memcmp", ATTR_CC_BOTH},
    {"__builtin_memcpy", ATTR_CC_BOTH},
    {"__builtin_memmove", ATTR_CC_BOTH},
    {"__builtin_memset", ATTR_CC_BOTH},
    {"__builtin_mul_overflow", ATTR_CC_BOTH},
    {"__builtin_nan", ATTR_CC_BOTH},
    {"__builtin_nand128", ATTR_CC_GCC},
    {"__builtin_nand32", ATTR_CC_GCC},
    {"__builtin_nand64", ATTR_CC_GCC},
    {"__builtin_nanf", ATTR_CC_BOTH},
    {"__builtin_nanl", ATTR_CC_BOTH},
    {"__builtin_nans", ATTR_CC_BOTH},
    {"__builtin_nansf", ATTR_CC_BOTH},
    {"__builtin_nansl", ATTR_CC_BOTH},
    {"__builtin_object_size", ATTR_CC_BOTH},
    {"__builtin_os_log_format", ATTR_CC_CLANG},
    {"__builtin_os_log_format_buffer_size", ATTR_CC_CLANG},
    {"__builtin_parity", ATTR_CC_BOTH},
    {"__builtin_parityll", ATTR_CC_BOTH},
    {"__builtin_pc_function_name", 0},
    {"__builtin_pc_source_location", 0},
    {"__builtin_popcount", ATTR_CC_BOTH},
    {"__builtin_popcountll", ATTR_CC_BOTH},
    {"__builtin_prefetch", ATTR_CC_BOTH},
    {"__builtin_quote", 0},
    {"__builtin_reg_class", 0},
    {"__builtin_return_address", ATTR_CC_BOTH},
    {"__builtin_setjmp", ATTR_CC_BOTH},
    {"__builtin_shuffle", ATTR_CC_GCC},
    {"__builtin_signbit", ATTR_CC_BOTH},
    {"__builtin_strcmp", ATTR_CC_BOTH},
    {"__builtin_strlen", ATTR_CC_BOTH},
    {"__builtin_sub_overflow", ATTR_CC_BOTH},
    {"__builtin_trap", ATTR_CC_BOTH},
    {"__builtin_types_compatible_p", ATTR_CC_BOTH},
    {"__builtin_unreachable", ATTR_CC_BOTH},
    {"__c11_atomic_fetch_add", ATTR_CC_CLANG},
    {"__c11_atomic_fetch_and", ATTR_CC_CLANG},
    {"__c11_atomic_fetch_or", ATTR_CC_CLANG},
    {"__c11_atomic_fetch_sub", ATTR_CC_CLANG},
    {"__c11_atomic_fetch_xor", ATTR_CC_CLANG},
    {"__sync_add_and_fetch", ATTR_CC_BOTH},
    {"__sync_and_and_fetch", ATTR_CC_BOTH},
    {"__sync_bool_compare_and_swap", ATTR_CC_BOTH},
    {"__sync_fetch_and_add", ATTR_CC_BOTH},
    {"__sync_fetch_and_and", ATTR_CC_BOTH},
    {"__sync_fetch_and_nand", ATTR_CC_BOTH},
    {"__sync_fetch_and_or", ATTR_CC_BOTH},
    {"__sync_fetch_and_sub", ATTR_CC_BOTH},
    {"__sync_fetch_and_xor", ATTR_CC_BOTH},
    {"__sync_lock_release", ATTR_CC_BOTH},
    {"__sync_lock_test_and_set", ATTR_CC_BOTH},
    {"__sync_nand_and_fetch", ATTR_CC_BOTH},
    {"__sync_or_and_fetch", ATTR_CC_BOTH},
    {"__sync_sub_and_fetch", ATTR_CC_BOTH},
    {"__sync_synchronize", ATTR_CC_BOTH},
    {"__sync_val_compare_and_swap", ATTR_CC_BOTH},
    {"__sync_xor_and_fetch", ATTR_CC_BOTH},
};

#define NBUILTINS (sizeof(builtins) / sizeof(*builtins))

const BuiltinDef *builtin_table(size_t *n) {
    *n = NBUILTINS;
    return builtins;
}

static const BuiltinDef *lookup_builtin(const char *name, size_t len) {
    size_t lo = 0, hi = NBUILTINS;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        int    c   = strncmp(builtins[mid].name, name, len);
        if (!c && builtins[mid].name[len])
            c = 1;
        if (!c)
            return &builtins[mid];
        if (c < 0)
            lo = mid + 1;
        else
            hi = mid;
    }
    return NULL;
}

bool cc_has_builtin(const char *name, size_t len) {
    return lookup_builtin(name, len) ||
           libm_builtin_lookup(name, (int)len, &(int){0}, &(int){0}) ||
           chk_builtin_known(name, (int)len);
}

bool builtin_on_output_compiler(const VirtualMachine *vm, const char *name,
                                size_t len) {
    unsigned want = vm->compiler.output_compiler == CCCC_OUTPUT_COMPILER_GCC
                        ? ATTR_CC_GCC
                    : vm->compiler.output_compiler == CCCC_OUTPUT_COMPILER_CLANG
                        ? ATTR_CC_CLANG
                        : 0;
    if (!want || len < 3 || name[0] != '_' || name[1] != '_')
        return true;
    const BuiltinDef *b = lookup_builtin(name, len);
    return !b || (b->cc & want);
}
