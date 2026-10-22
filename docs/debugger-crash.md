# Break-In and Crash Handling

Stop at a chosen point, debug on crash, and read host C backtraces.

## Programmatic break-in

With `-g`, these drop into the debugger from inside the program:

| Call | Under `-g` | Without `-g` |
|---|---|---|
| `__builtin_debugtrap()` | Enters the debugger; `continue` resumes | Terminates |
| `__builtin_trap()` | Same, but meant to be unconditional: guard unreachable paths | Aborts |
| `raise(SIGTRAP)` with a VM handler registered | Enters the debugger (as native trap instructions do on AArch64 Apple) | |

```c
#include <stdio.h>

int main(void) {
    int x = compute();
    __builtin_debugtrap();   // pauses here under -g
    printf("x = %d\n", x);
    return 0;
}
```

## Auto-debug-on-crash

CCCC drops into the debugger when a program hits a fatal VM error or a host `SIGSEGV`, `SIGBUS`, `SIGFPE`, `SIGILL` or `SIGABRT`: unchecked NULL or out-of-bounds accesses, checked memory errors, stack overflow, invalid control-flow targets and unknown instructions. Runtime and comptime execution use the same path. You do not need `-g`.

| Cause | What the debugger offers |
|---|---|
| VM-detected error | Stops at the faulting instruction; `continue` retries it after you inspect or change state |
| Host signal | Inspection only: registers, stack, source, disassembly and valid memory. `continue`, `step`, `next` and `finish` are rejected because the native frame cannot resume. `quit` or EOF exits with `128 + signal` |

It activates **only when stdin and stdout are both TTYs**; under a pipe, redirect or batch harness the error is printed and the process exits as before.

| Setting | Effect |
|---|---|
| `--no-debug-on-crash` | Never trap. Use it from a harness that happens to run on a TTY (a `script` or `tmux` session) |
| `-g` | Also stops at the entry of `main` |
| Guest `signal()` handlers, `SIG_IGN` | Stay authoritative: delivered at the normal safe point, never replaced. Restoring `SIG_DFL` restores the debugger trap |

`--testing`, `--test` and `--test-suite` disable the auto-detection themselves, since their child processes would inherit the TTY and hang.

## Host C backtrace on crash

If CCCC itself crashes in any phase (parsing, codegen, VM dispatch) it prints a symbolic host C stack to stderr, then dies with the original signal, so exit-code semantics and negative tests are unaffected.

```
Host C crash (SIGSEGV):
  #0   <0x191c256a3>
  #1   op_LDR_W_fn (src/ops.c:1130)
  #2   cccc_vm_eval_dispatch (/src/vm.c:617)
  #3   vm_eval (/src/host_signal.c:183)
  #4   cc_run_at (/src/vm.c:1873)
  #5   cc_run (/src/vm.c:1881)
  #6   main (/src/main.c:2215)
```

The handler covers `SIGSEGV`, `SIGBUS`, `SIGFPE` and `SIGILL`, uses [libbacktrace](https://github.com/ianlancetaylor/libbacktrace) and is on by default. In an interactive debug session the trace is also printed before the native frame is unwound, so you see both the host call chain and the guest VM state.

| Platform | `file:line` |
|---|---|
| Linux | Works: the `-g` build embeds DWARF |
| macOS | Function names only, because the binary carries a debug map rather than DWARF. Run `make dsym` (produces the gitignored `cccc.dSYM`) for full `file:line` |

Build without it using `make CCCC_HAS_BACKTRACE=0`.
