#!/usr/bin/env python3
"""PTY integration tests for conditional breakpoints (ticket 113).

The conditional breakpoint evaluator compiles each condition into a real
wrapper function (see debugger_compile_condition_once, src/debugger.c)
instead of tree-walking it, so it supports the full call ABI: float
args/returns, struct-by-value arguments, pointer-to-local arguments,
indirect calls, nested-function static links, and stack-passed (>8)
arguments -- plus the regressions the old scalar-only evaluator already
covered (integer locals, assignment/comma side effects, member access) and
one it could never do at all (float arithmetic in the condition itself).

Modeled on tools/test_host_signal_debugger.py's PtyProcess harness: the
debugger is a stdin-driven interactive session, so it needs a real
pseudo-terminal rather than the plain-pipe subprocess.run() tools/tests.py
uses.
"""

import argparse
import errno
import os
import pty
import select
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CCCC = ROOT / "cccc"
PROMPT = b"(cccc-dbg) "
FIXTURE = ROOT / "tools" / "tests" / "debugger_condition.c"


class PtyProcess:
    def __init__(self, args):
        self.master, slave = pty.openpty()
        self.proc = subprocess.Popen(
            args,
            cwd=ROOT,
            stdin=slave,
            stdout=slave,
            stderr=slave,
            close_fds=True,
        )
        os.close(slave)
        self.output = bytearray()

    def _read_once(self, timeout):
        ready, _, _ = select.select([self.master], [], [], timeout)
        if not ready:
            return b""
        try:
            data = os.read(self.master, 4096)
        except OSError as exc:
            if exc.errno == errno.EIO:
                return b""
            raise
        self.output.extend(data)
        return data

    def expect(self, token, timeout=8):
        deadline = time.monotonic() + timeout
        start = len(self.output)
        while token not in self.output[start:]:
            if self.proc.poll() is not None:
                self.drain()
                break
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                break
            self._read_once(min(0.1, remaining))
        if token not in self.output[start:]:
            raise AssertionError(
                f"did not receive {token!r}; output:\n{self.text()}"
            )

    def send(self, command):
        os.write(self.master, command.encode() + b"\n")

    def drain(self):
        while self._read_once(0):
            pass

    def wait(self, timeout=8):
        deadline = time.monotonic() + timeout
        while True:
            self._read_once(0.1)
            if self.proc.poll() is not None:
                break
            if time.monotonic() >= deadline:
                self.proc.kill()
                self.proc.wait(timeout=2)
                raise AssertionError(
                    f"process did not exit within {timeout}s; output:\n{self.text()}"
                )
        rc = self.proc.wait(timeout=2)
        self.drain()
        os.close(self.master)
        return rc

    def text(self):
        return self.output.decode(errors="replace")


def debugger():
    return PtyProcess([str(CCCC), "-g", str(FIXTURE)])


def assert_condition_fires(line, condition):
    """Set a conditional breakpoint that must fire exactly once, then let
    the program run to completion (exit 42) -- confirming both that the
    condition correctly evaluated true *and* that resuming afterward still
    works (the compiled wrapper's save/restore of VM state is intact)."""
    child = debugger()
    child.expect(PROMPT)
    child.send(f"break {line} if {condition}")
    child.expect(PROMPT)
    child.send("continue")
    child.expect(b"Breakpoint hit")
    child.send("continue")
    rc = child.wait()
    assert rc == 42, (rc, child.text())


def assert_condition_never_fires(line, condition):
    """A condition that's never true must not stop the program at all --
    it should run straight through to exit 42."""
    child = debugger()
    child.expect(PROMPT)
    child.send(f"break {line} if {condition}")
    child.expect(PROMPT)
    child.send("continue")
    rc = child.wait()
    assert "Breakpoint hit" not in child.text(), child.text()
    assert rc == 42, (rc, child.text())


def test_integer_local_regression():
    # Loop increments x from 10 to 15; must stop on exactly the x == 12 hit.
    assert_condition_fires(34, "x == 12")


def test_assignment_and_comma_regression():
    # Assignment/comma side effects (supported since the #47 fix) must still
    # write into the *live* paused frame, not a throwaway copy. The
    # assignment runs on every hit (proving it doesn't corrupt the loop --
    # i is untouched so the loop still terminates normally), but the
    # trailing i == 2 comma operand only turns the whole condition true on
    # one specific iteration, matching assert_condition_fires' single-hit
    # assumption.
    assert_condition_fires(34, "(x = 999, i == 2)")


def test_float_arithmetic_was_previously_unsupported():
    # The old evaluator was integer-only end to end; a bare float comparison
    # in a condition always errored out. It must now work like any other
    # scalar condition.
    assert_condition_never_fires(37, "fx > 4.0")  # fx is still 3.0 here
    assert_condition_fires(38, "fx > 4.0")         # fx is 4.5 by here


def test_integer_call_regression():
    assert_condition_fires(38, "add(x, 100) == 115")


def test_float_call_args_and_return():
    assert_condition_fires(39, "half(fx) > 2.0")


def test_pointer_to_local_argument():
    assert_condition_fires(40, "add_ptr(&x, 1) == 16")


def test_struct_by_value_argument_and_member_regression():
    assert_condition_fires(41, "sum_point(pt) == 7 && pt.x == 3")


def test_indirect_call():
    assert_condition_fires(42, "fp(3, 4) == 7")


def test_stack_passed_arguments():
    assert_condition_fires(43, "sum10(1,2,3,4,5,6,7,8,9,10) == 55")


def test_nested_function_static_link():
    assert_condition_fires(44, "outer(2) == 21")


def test_non_scalar_condition_is_rejected():
    # The scalar-type check only runs when the condition is first evaluated
    # (compiled on the breakpoint's first hit), not when it's set.
    child = debugger()
    child.expect(PROMPT)
    child.send("break 41 if pt")
    child.expect(PROMPT)
    child.send("continue")
    child.expect(b"condition must have scalar type")
    rc = child.wait()
    assert rc == 42, (rc, child.text())


TESTS = [
    test_integer_local_regression,
    test_assignment_and_comma_regression,
    test_float_arithmetic_was_previously_unsupported,
    test_integer_call_regression,
    test_float_call_args_and_return,
    test_pointer_to_local_argument,
    test_struct_by_value_argument_and_member_regression,
    test_indirect_call,
    test_stack_passed_arguments,
    test_nested_function_static_link,
    test_non_scalar_condition_is_rejected,
]


def main():
    global CCCC

    parser = argparse.ArgumentParser(
        description="Run conditional-breakpoint PTY integration tests"
    )
    parser.add_argument(
        "--binary",
        default=str(CCCC),
        help="Path to the CCCC binary (default: repository cccc binary)",
    )
    args = parser.parse_args()
    CCCC = Path(args.binary).resolve()

    if not CCCC.exists():
        print(f"error: CCCC binary not found: {CCCC}", file=sys.stderr)
        return 1

    failed = []
    for t in TESTS:
        name = t.__name__
        try:
            t()
            print(f"  ✓ {name}")
        except Exception as e:
            print(f"  ✗ {name}: {e}")
            failed.append(name)

    if failed:
        print(f"debugger condition integration: FAILED ({len(failed)}/{len(TESTS)})")
        return 1

    print(f"debugger condition integration: passed ({len(TESTS)}/{len(TESTS)})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
