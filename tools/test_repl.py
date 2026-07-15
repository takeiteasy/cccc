#!/usr/bin/env python3
"""PTY integration tests for the interactive REPL (-r/--repl, ticket #661).

Modeled on tools/test_host_signal_debugger.py's PtyProcess harness: the REPL
is a stdin-driven interactive session (readline echoes/history, multi-line
continuation prompts), so it needs a real pseudo-terminal to exercise
end-to-end rather than the plain-pipe subprocess.run() used by tools/tests.py.
"""

import argparse
import errno
import os
import pty
import select
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CCCC = ROOT / "cccc"
PROMPT = b"cccc> "
CONT_PROMPT = b"  ... "


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

    def send(self, line):
        os.write(self.master, line.encode() + b"\n")

    def drain(self):
        while self._read_once(0):
            pass

    def wait(self, timeout=8):
        # Keep draining while waiting: readline's atexit terminal-restore
        # hook does a blocking tcsetattr(..., TCSADRAIN, ...), which waits
        # for all queued output to be *read* by the far end of the pty
        # before returning. A plain proc.wait() here (with nobody reading)
        # can deadlock the child against a full pty output buffer.
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


def repl():
    return PtyProcess([str(CCCC), "--repl"])


def test_expression_eval():
    child = repl()
    child.expect(PROMPT)
    child.send("40 + 2")
    child.expect(b"(int) 42")
    child.send(":quit")
    rc = child.wait()
    assert rc == 0, (rc, child.text())


def test_decl_persists_across_lines():
    child = repl()
    child.expect(PROMPT)
    child.send("int x = 40;")
    child.expect(PROMPT)
    child.send("x + 2")
    child.expect(b"(int) 42")
    child.send(":quit")
    child.wait()


def test_runtime_mutation_persists():
    # A global mutated by one expression must still hold that value on the
    # next line -- this is the correctness property the incremental
    # compiler (cc_repl_compile_new) exists to preserve (a naive full
    # rebuild-per-eval would reset it back to its initializer).
    child = repl()
    child.expect(PROMPT)
    child.send("int x = 1;")
    child.send("x = x + 1;")
    child.expect(b"(int) 2")
    child.send("x = x + 1;")
    child.expect(b"(int) 3")
    child.send(":quit")
    child.wait()


def test_typedef_makes_next_line_a_declaration():
    child = repl()
    child.expect(PROMPT)
    child.send("typedef int myint;")
    child.send("myint z = 99;")
    child.send("z")
    child.expect(b"(int) 99")
    child.send(":quit")
    child.wait()


def test_function_definition_and_call():
    child = repl()
    child.expect(PROMPT)
    child.send("int square(int n) { return n * n; }")
    child.send("square(7)")
    child.expect(b"(int) 49")
    child.send(":quit")
    child.wait()


def test_float_and_string_formatting():
    child = repl()
    child.expect(PROMPT)
    child.send("float f = 3.5f;")
    child.send("f")
    child.expect(b"(float) 3.5")
    child.send('char *s = "hello";')
    child.send("s")
    child.expect(b'"hello"')
    child.send(":quit")
    child.wait()


def test_type_command():
    child = repl()
    child.expect(PROMPT)
    child.send("int x = 40;")
    child.send(":type x")
    child.expect(b"=> int")
    child.send(":quit")
    child.wait()


def test_error_rolls_back_and_session_survives():
    child = repl()
    child.expect(PROMPT)
    child.send("int y = 10;")
    child.send("this is not valid C")
    child.expect(b"error:")
    child.send("y + 5")
    child.expect(b"(int) 15")
    child.send(":quit")
    rc = child.wait()
    assert rc == 0, (rc, child.text())


def test_multiline_continuation():
    child = repl()
    child.expect(PROMPT)
    child.send("int add(int a,")
    child.expect(CONT_PROMPT)
    child.send(" int b) {")
    child.expect(CONT_PROMPT)
    child.send("  return a + b;")
    child.expect(CONT_PROMPT)
    child.send("}")
    child.expect(PROMPT)
    child.send("add(3, 4)")
    child.expect(b"(int) 7")
    child.send(":quit")
    child.wait()


def test_load_command():
    with tempfile.NamedTemporaryFile(
        mode="w", suffix=".c", delete=False, dir=str(ROOT / "tools")
    ) as f:
        f.write("int loaded_var = 123;\n")
        f.write("int loaded_fn(int n) { return n * 2; }\n")
        path = f.name
    try:
        child = repl()
        child.expect(PROMPT)
        child.send(f":load {path}")
        child.expect(PROMPT)
        child.send("loaded_var")
        child.expect(b"(int) 123")
        child.send("loaded_fn(21)")
        child.expect(b"(int) 42")
        child.send(":quit")
        child.wait()
    finally:
        os.unlink(path)


def test_help_and_quit():
    child = repl()
    child.expect(PROMPT)
    child.send(":help")
    child.expect(b"Session commands:")
    child.send(":quit")
    rc = child.wait()
    assert rc == 0, (rc, child.text())


TESTS = [
    test_expression_eval,
    test_decl_persists_across_lines,
    test_runtime_mutation_persists,
    test_typedef_makes_next_line_a_declaration,
    test_function_definition_and_call,
    test_float_and_string_formatting,
    test_type_command,
    test_error_rolls_back_and_session_survives,
    test_multiline_continuation,
    test_load_command,
    test_help_and_quit,
]


def main():
    global CCCC

    parser = argparse.ArgumentParser(description="Run REPL PTY integration tests")
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
        print(f"repl integration: FAILED ({len(failed)}/{len(TESTS)})")
        return 1

    print(f"repl integration: passed ({len(TESTS)}/{len(TESTS)})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
