/*
 CCCC: Comprehensiev C Compensation Compiler - Kernel Execution Model

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
 * @file kernel.h
 * @brief Work-item builtins and the launch builtin for [[cccc::kernel]]
 * functions. See docs/KERNELS.md.
 */

#ifndef CCCC_KERNEL_H
#define CCCC_KERNEL_H

#include <stddef.h>

/*! @brief Memory fence flags for cccc_barrier(). Every flag orders the same in
 * the VM. */
#define CCCC_LOCAL_FENCE  1
#define CCCC_GLOBAL_FENCE 2

/*! @brief An NDRange extent of one to three dimensions. Build one with
 * CCCC_RANGE. */
typedef struct {
    unsigned dims;
    size_t   size[3];
} cccc_range;

#define CCCC_RANGE_DIMS_(_1, _2, _3, N, ...) N

/*! @brief `CCCC_RANGE(x)`, `CCCC_RANGE(x, y)` or `CCCC_RANGE(x, y, z)`. */
#define CCCC_RANGE(...)                                                        \
    ((cccc_range){CCCC_RANGE_DIMS_(__VA_ARGS__, 3, 2, 1), {__VA_ARGS__}})

/*! @brief Launches a kernel over `global` work-items in work-groups of `local`.
 * @param k Kernel function (or pointer to one) marked [[cccc::kernel]].
 * @param g Global range, a cccc_range.
 * @param l Work-group range, a cccc_range of the same dimensions.
 * @note The remaining arguments are checked against the kernel's parameters
 * like an ordinary call. A [[cccc::local]] pointer parameter takes
 * CCCC_LOCAL(bytes). Returns when every work-item has finished.
 */
#define cccc_launch(k, g, l, ...)                                              \
    __builtin_kernel_launch(k, g, l, ##__VA_ARGS__)

/*! @brief Argument for a [[cccc::local]] pointer parameter: `bytes` of
 * work-group memory. */
#define CCCC_LOCAL(bytes) __builtin_kernel_local(bytes)

/*! @brief Index of the calling work-item in the whole range, per dimension. */
size_t cccc_global_id(unsigned dim);
/*! @brief Index of the calling work-item within its work-group. */
size_t cccc_local_id(unsigned dim);
/*! @brief Index of the calling work-item's work-group. */
size_t cccc_group_id(unsigned dim);
/*! @brief Size of the whole range. */
size_t cccc_global_size(unsigned dim);
/*! @brief Size of a work-group. */
size_t cccc_local_size(unsigned dim);
/*! @brief Number of work-groups. */
size_t cccc_num_groups(unsigned dim);
/*! @brief Dimensions of the launch (1 outside a launch). */
unsigned cccc_work_dim(void);

/*! @brief Waits until every work-item of the group has reached the barrier. */
void cccc_barrier(unsigned fences);

#endif
