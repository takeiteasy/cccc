#!/usr/bin/env python3
"""PTY integration tests for the debugger `print`/`p` command (#958).

#666 added a shared recursive value formatter, cc_dump_value/cc_dump_value_reg
(src/dump.c), built for the REPL's expression result printer. #958 is the
follow-up: adopt it in the debugger's own inspection commands, plus fix
cc_is_valid_vm_address's stack-bound check (src/debugger.c), which validated
addresses against [stack_seg, stack_seg + poolsize) -- the low end of the
much larger poolsize_max reservation -- instead of the actual committed
range [stack_base, initial_sp) near the top, so it rejected every real
live-stack local. These tests exercise both: printing a struct/nested
struct/array/pointer/global on a live paused frame only works if the stack
address is accepted.

Modeled on tools/test_debugger_condition.py's PtyProcess harness: the
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
FIXTURE = ROOT / "tools" / "tests" / "debugger_print.c"


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
        # Search cursor for expect(): advances past the END of each match
        # (not just to the current buffer length), so a single os.read()
        # that happens to slurp up several tokens' worth of output at once
        # doesn't cause a later expect() to skip over data it hasn't
        # actually been asked to match yet.
        self.pos = 0

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
        while True:
            idx = self.output.find(token, self.pos)
            if idx != -1:
                self.pos = idx + len(token)
                return
            if self.proc.poll() is not None:
                self.drain()
                if self.output.find(token, self.pos) != -1:
                    continue
                break
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                break
            self._read_once(min(0.1, remaining))
        if self.output.find(token, self.pos) == -1:
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


def at_return_line():
    """Break at the `return ok ? 42 : 1;` line (30), where every local in
    the fixture is fully initialized, and let the program run to
    completion afterward (proving `continue` still works post-inspection)."""
    child = debugger()
    child.expect(PROMPT)
    child.send("break 30")
    child.expect(PROMPT)
    child.send("continue")
    child.expect(b"Breakpoint hit")
    return child


def finish(child):
    child.send("continue")
    rc = child.wait()
    assert rc == 42, (rc, child.text())


def test_print_struct():
    child = at_return_line()
    child.send("print p")
    child.expect(b"p = (struct Point) {")
    child.expect(b"x = 3")
    child.expect(b"y = 4")
    finish(child)


def test_print_nested_struct():
    child = at_return_line()
    child.send("print ln")
    child.expect(b"ln = (struct Line) {")
    child.expect(b"a = {")
    child.expect(b"x = 1")
    child.expect(b"y = 2")
    child.expect(b"b = {")
    child.expect(b"x = 3")
    child.expect(b"y = 4")
    finish(child)


def test_print_array():
    child = at_return_line()
    child.send("print arr")
    child.expect(b"arr = (int[4]) {")
    child.expect(b"[0] = 10")
    child.expect(b"[1] = 20")
    child.expect(b"[2] = 30")
    child.expect(b"[3] = 40")
    finish(child)


def test_print_char_pointer_as_string():
    child = at_return_line()
    child.send("print s")
    child.expect(b'"hi"')
    finish(child)


def test_print_pointer_to_local():
    child = at_return_line()
    child.send("print pl")
    child.expect(PROMPT)
    child.send("print local")
    child.expect(b"local = (int) 42")
    finish(child)


def test_print_scalar_int():
    child = at_return_line()
    child.send("print local")
    child.expect(b"local = (int) 42")
    finish(child)


def test_print_data_segment_global():
    # global_counter lives in the data segment, not the stack -- exercises
    # the other branch of cc_is_valid_vm_address, unaffected by the
    # stack-bound fix but must keep working alongside it.
    child = at_return_line()
    child.send("print global_counter")
    child.expect(b"global_counter = (int) 7")
    finish(child)


def test_print_unknown_symbol_errors_gracefully():
    child = at_return_line()
    child.send("print does_not_exist")
    child.expect(b"Error: Unable to resolve 'does_not_exist'")
    finish(child)


TESTS = [
    test_print_struct,
    test_print_nested_struct,
    test_print_array,
    test_print_char_pointer_as_string,
    test_print_pointer_to_local,
    test_print_scalar_int,
    test_print_data_segment_global,
    test_print_unknown_symbol_errors_gracefully,
]


def main():
    global CCCC

    parser = argparse.ArgumentParser(
        description="Run debugger print-command PTY integration tests (#958)"
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
        print(f"debugger print integration: FAILED ({len(failed)}/{len(TESTS)})")
        return 1

    print(f"debugger print integration: passed ({len(TESTS)}/{len(TESTS)})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
