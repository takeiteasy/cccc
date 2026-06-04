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
*/

#include "./internal.h"

#ifdef JCC_HAS_LLVM
#include <llvm/Config/llvm-config.h>
#endif

bool cc_llvm_backend_enabled(void) {
#ifdef JCC_HAS_LLVM
    return true;
#else
    return false;
#endif
}

const char *cc_llvm_backend_version(void) {
#ifdef JCC_HAS_LLVM
    return LLVM_VERSION_STRING;
#else
    return "disabled";
#endif
}
