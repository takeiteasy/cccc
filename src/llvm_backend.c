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
#include <llvm-c/Core.h>
#include <llvm-c/Target.h>
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

int cc_llvm_backend_smoke_test(void) {
#ifdef JCC_HAS_LLVM
    if (LLVMInitializeNativeTarget() != 0)
        return -1;
    if (LLVMInitializeNativeAsmPrinter() != 0)
        return -1;

    LLVMContextRef context = LLVMContextCreate();
    if (!context)
        return -1;

    LLVMModuleRef module =
        LLVMModuleCreateWithNameInContext("jcc-llvm-smoke", context);
    if (!module) {
        LLVMContextDispose(context);
        return -1;
    }

    LLVMBuilderRef builder = LLVMCreateBuilderInContext(context);
    if (!builder) {
        LLVMDisposeModule(module);
        LLVMContextDispose(context);
        return -1;
    }

    LLVMDisposeBuilder(builder);
    LLVMDisposeModule(module);
    LLVMContextDispose(context);
    return 0;
#else
    return -1;
#endif
}
