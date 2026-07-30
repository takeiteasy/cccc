#!/usr/bin/env python3
"""macOS PTY integration tests for host-signal crash debugging."""

import argparse
import errno
import os
import pty
import select
import signal
import subprocess
import sys
import time
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
CCCC = ROOT / "cccc"
PROMPT = b"(cccc-dbg) "
TESTS = ROOT / "tools" / "tests"


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
        # Keep draining while waiting (ticket #856): the crash-handler paths
        # (src/host_backtrace.c's fatal-signal handler, installed
        # unconditionally regardless of --no-debug-on-crash) can write over
        # 1KB to stderr -- e.g. a full libbacktrace frame dump. macOS's pty
        # output buffer blocks the writer well under that (measured: fine at
        # 1KB, blocks by 2KB), so a plain proc.wait() with nobody reading the
        # master can deadlock the child forever against a full buffer. Model:
        # tools/test_repl.py's PtyProcess.wait(), which hit the same class of
        # bug via readline's atexit tcsetattr(TCSADRAIN, ...).
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


def cccc_args(source, *extra):
    return [str(CCCC), "-I./include", *extra, str(source)]


def assert_host_fault(source, expected_signal, commands=()):
    child = PtyProcess(cccc_args(source))
    child.expect(PROMPT)
    for command, expected in commands:
        child.send(command)
        child.expect(expected)
    child.send("q")
    rc = child.wait()
    output = child.text()
    assert f"Host fault: {expected_signal}" in output, output
    signum = getattr(signal, expected_signal)
    assert rc == 128 + signum, (rc, output)
    return output


def test_wait_drains_large_output():
    """Regression guard for #856: PtyProcess.wait() must not deadlock against
    a child that fills the pty output buffer before exiting. Doesn't need a
    cccc binary, so it runs unconditionally (even on non-macOS/no-binary
    hosts) as a fast, deterministic check on the harness itself."""
    payload_size = 8192  # comfortably above the ~1-2KB macOS pty block point
    child = PtyProcess([
        sys.executable, "-c",
        f"import sys; sys.stderr.write('x' * {payload_size}); sys.stderr.flush()",
    ])
    rc = child.wait(timeout=8)
    assert rc == 0, (rc, child.text())
    assert len(child.output) >= payload_size, len(child.output)


def main():
    global CCCC

    parser = argparse.ArgumentParser(
        description="Run macOS host-signal debugger integration tests"
    )
    parser.add_argument(
        "--binary",
        default=str(CCCC),
        help="Path to the CCCC binary (default: repository cccc binary)",
    )
    args = parser.parse_args()
    CCCC = Path(args.binary).resolve()

    # Harness self-test (#856): needs no cccc binary and no macOS, so it runs
    # unconditionally, ahead of the platform/binary gating below.
    test_wait_drains_large_output()

    if not CCCC.exists():
        print(f"error: CCCC binary not found: {CCCC}", file=sys.stderr)
        return 1

    if sys.platform != "darwin":
        print("host-signal debugger integration: skipped (macOS-only coverage)")
        return 0

    raw_fault = TESTS / "raw_fault.c"
    output = assert_host_fault(
        raw_fault,
        "SIGSEGV",
        commands=[
            ("c", b"Cannot resume after a host fault"),
            ("registers", b"=== Registers ==="),
        ],
    )
    assert "raw_fault.c:3" in output, output
    assert "return *p;" in output, output

    signal_numbers = {
        "SIGSEGV": signal.SIGSEGV,
        "SIGBUS": signal.SIGBUS,
        "SIGFPE": signal.SIGFPE,
        "SIGILL": signal.SIGILL,
        "SIGABRT": signal.SIGABRT,
    }
    for name, signum in signal_numbers.items():
        raised = TESTS / f"raise_{name.lower()}.c"
        child = PtyProcess(cccc_args(raised))
        child.expect(PROMPT)
        child.send("q")
        rc = child.wait()
        assert rc == 128 + signum, (name, rc, child.text())
        assert f"Host fault: {name}" in child.text(), child.text()

    no_debug = PtyProcess(cccc_args(raw_fault, "--no-debug-on-crash"))
    rc = no_debug.wait()
    assert rc == -signal.SIGSEGV, (rc, no_debug.text())
    assert "CCCC Debugger" not in no_debug.text(), no_debug.text()

    batch = subprocess.run(
        cccc_args(raw_fault, "-g"),
        cwd=ROOT,
        stdin=subprocess.DEVNULL,
        capture_output=True,
        text=True,
        timeout=8,
    )
    assert batch.returncode == -signal.SIGSEGV, (batch.returncode, batch.stdout, batch.stderr)
    assert "Host fault:" not in batch.stdout + batch.stderr

    testing = PtyProcess(
        [str(CCCC), "-I./include", "--testing", str(ROOT / "tests/test_exit_code.c")]
    )
    rc = testing.wait()
    assert rc == 0, (rc, testing.text())
    assert "Host fault:" not in testing.text(), testing.text()

    guest_handler = TESTS / "guest_handler.c"
    child = PtyProcess(cccc_args(guest_handler))
    rc = child.wait()
    assert rc == 42, (rc, child.text())
    assert "Host fault:" not in child.text(), child.text()

    guest_ignore = TESTS / "guest_ignore.c"
    child = PtyProcess(cccc_args(guest_ignore))
    rc = child.wait()
    assert rc == 42, (rc, child.text())
    assert "Host fault:" not in child.text(), child.text()

    restore_default = TESTS / "restore_default.c"
    assert_host_fault(restore_default, "SIGSEGV")

    comptime_fault = TESTS / "comptime_fault.c"
    assert_host_fault(comptime_fault, "SIGSEGV")

    thread_fault = TESTS / "thread_fault.c"
    assert_host_fault(thread_fault, "SIGSEGV")

    print("host-signal debugger integration: passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
