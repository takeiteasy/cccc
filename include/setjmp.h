/*
 * setjmp.h - Non-local Jumps
 * JCC C Compiler - Standard C Library Header
 *
 * Implements C11 setjmp/longjmp for the JCC VM.
 *
 * VM Context:
 * The JCC VM uses the following registers for execution state:
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
 * [3] - CFI shadow-stack offset: (char*)shadow_sp - (char*)shadow_stack, or -1 if CFI disabled
 * [4] - reserved for future use
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
 * Array of 5 long long values to hold VM state:
 * - Alignment: 8 bytes (natural alignment of long long)
 * - Size: 40 bytes total (5 * 8 bytes)
 */
typedef long long jmp_buf[5];

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
 * - Local variables in the setjmp frame may have undefined values unless volatile
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
