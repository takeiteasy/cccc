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

// build_shell.c — implementation TU for the vendored paul_shell.h.
//
// paul_shell.h is a single-header library; defining PAUL_SHELL_IMPLEMENTATION
// here pulls in the implementation in a dedicated translation unit, keeping
// its file-static helpers (die, xmalloc, eprintf, peek, advance, …) isolated
// from build.c.

#define PAUL_SHELL_IMPLEMENTATION
#include <stdarg.h>
#include <sys/wait.h>
#include "vendor/paul_shell.h"
