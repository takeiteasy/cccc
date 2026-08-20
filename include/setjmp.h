/*
 * setjmp.h - Non-local Jumps
 * CCCC C Compiler - Standard C Library Header
 *
 * Implements C11 setjmp/longjmp for the CCCC VM.
 *
 * VM Context:
 * The CCCC VM uses the following registers for execution state:
 * - pc (program counter): Points to the current instruction
 * - sp (stack pointer): Points to the top of the stack
 * - bp (base pointer): Points to the current stack frame
 * - ax (accumulator): Holds return values and intermediate results
 *
 * jmp_buf Layout:
 * The jmp_buf type stores the complete VM execution context needed to
 * perform a non-local jump. It contains:
 * [0] - saved pc (program counter)
 * [1] - saved sp (stack pointer)
 * [2] - saved bp (base pointer)
 * [3] - CFI shadow-stack offset: (char*)shadow_sp - (char*)shadow_stack, or -1
 * if CFI disabled [4..] - reserved for future use / native-backend headroom
 * (see below)
 *
 * Sizing under -c=native (#1054/#1030):
 * Under -c=native, setjmp()/longjmp() are emitted as calls to the *real*
 * host libc setjmp()/longjmp() (setjmp.h is on is_compiler_owned_header's
 * list, but there is no VM-equivalent host ABI to translate to -- the VM's
 * own [0..3] layout above has no meaning outside VM bytecode). The host
 * writes/reads its own, differently-shaped jmp_buf into whatever pointer
 * it's given (serialize.c passes `(void *)env`, not a typed jmp_buf*), so
 * this array must be at least as large as every supported host's real
 * sizeof(jmp_buf) or the host call overruns it. Measured directly (not
 * from memory, see feedback_verify_libc_signatures_linux):
 *   - macOS arm64:     int[48]                         = 192 bytes
 *   - macOS x86_64:    int[37]                         = 148 bytes
 *   - glibc x86_64:    long[8] + int + sigset_t(128)    = 200 bytes
 *   - glibc aarch64:   ulonglong[22] + int + sigset_t   = 312 bytes  (max)
 * 40 long longs (320 bytes) covers the measured max with headroom. The VM
 * itself only ever indexes slots [0]-[3] (src/ops.c SETJMP/LONGJMP) --
 * the rest exists solely so a -c=native build's real host setjmp/longjmp
 * has somewhere safe to write.
 *
 * Implementation Strategy:
 * setjmp and longjmp are implemented using dedicated VM instructions:
 * - SETJMP: Saves the current VM state to jmp_buf and returns 0
 * - LONGJMP: Restores VM state from jmp_buf and returns the specified value
 *
 * These are special builtins recognized by the compiler and compiled to
 * VM instructions rather than function calls.
 *
 * Usage Example:
 *   jmp_buf env;
 *
 *   if (setjmp(env) == 0) {
 *       // First time - normal execution path
 *       some_function_that_may_error(env);
 *   } else {
 *       // Returned from longjmp - error handling
 *       printf("An error occurred!\n");
 *   }
 *
 *   void some_function_that_may_error(jmp_buf env) {
 *       if (error_condition) {
 *           longjmp(env, 1);  // Jump back to setjmp with value 1
 *       }
 *   }
 *
 * Notes:
 * - setjmp may only be called from specific contexts (see C11 spec 7.13.2.1)
 * - longjmp must not be called with a zero value (it's converted to 1)
 * - The jmp_buf must remain valid at the time of longjmp
 * - Local variables modified between setjmp and longjmp have undefined values
 *   after longjmp unless declared volatile
 */

#ifndef _SETJMP_H
#define _SETJMP_H

/*
 * jmp_buf type: execution context buffer
 *
 * Array of 40 long long values:
 * - Alignment: 8 bytes (natural alignment of long long)
 * - Size: 320 bytes total (40 * 8 bytes)
 * - Only [0]-[3] are VM state (see the file header comment above); the
 *   rest is headroom for -c=native's real host setjmp()/longjmp().
 */
typedef long long jmp_buf[40];

/*
 * setjmp(env) - Save execution context
 * @env: jmp_buf to save context into
 *
 * Saves the current VM execution state (pc, sp, bp, ax) into env.
 *
 * Returns:
 * - 0 when called directly
 * - Non-zero when returning from a longjmp
 *
 * Implementation:
 * This is a compiler builtin that generates a SETJMP VM instruction.
 * The instruction saves the VM registers to the jmp_buf pointed to by env.
 *
 * The return address is saved so that longjmp can return to the point
 * immediately after the setjmp call.
 *
 * NOTE: setjmp and longjmp are compiler builtins and are automatically
 * declared by the compiler. No function declaration is needed.
 */

/*
 * longjmp(env, val) - Restore execution context (non-local jump)
 * @env: jmp_buf containing saved context
 * @val: value to return from setjmp (converted to 1 if val==0)
 *
 * Restores the VM execution state from env and returns control to
 * the location of the corresponding setjmp call.
 *
 * This function does not return normally. Instead, execution continues
 * as if the setjmp call had returned with value val.
 *
 * Implementation:
 * This is a compiler builtin that generates a LONGJMP VM instruction.
 * The instruction restores pc, sp, bp, and ax from the jmp_buf,
 * effectively unwinding the stack and jumping back to the saved location.
 *
 * Special handling:
 * - If val is 0, it's converted to 1 (so setjmp never "returns" 0 twice)
 * - The stack is unwound to the saved sp, freeing all intervening frames
 * - Local variables in the setjmp frame may have undefined values unless
 * volatile
 *
 * Undefined behavior:
 * - env must have been initialized by a call to setjmp
 * - The function containing the setjmp must not have returned
 * - env must not have been modified since setjmp
 *
 * NOTE: setjmp and longjmp are compiler builtins and are automatically
 * declared by the compiler. No function declaration is needed.
 */

#endif /* _SETJMP_H */
