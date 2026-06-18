#!/usr/bin/env python3
"""macOS PTY integration tests for host-signal crash debugging."""

import errno
import os
import pty
import select
import signal
import subprocess
import sys
import tempfile
import time
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
CCCC = ROOT / "cccc"
PROMPT = b"(cccc-dbg) "


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
        rc = self.proc.wait(timeout=timeout)
        self.drain()
        os.close(self.master)
        return rc

    def text(self):
        return self.output.decode(errors="replace")


def write_source(directory, name, source):
    path = Path(directory) / name
    path.write_text(source)
    return path


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


def main():
    if sys.platform != "darwin":
        print("host-signal debugger integration: skipped (macOS-only coverage)")
        return 0

    with tempfile.TemporaryDirectory(prefix="cccc-host-signal-") as tmp:
        raw_fault = write_source(
            tmp,
            "raw_fault.c",
            "int main(void) {\n"
            "  volatile int *p = (volatile int *)0;\n"
            "  return *p;\n"
            "}\n",
        )
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
            raised = write_source(
                tmp,
                f"raise_{name.lower()}.c",
                f"#include <signal.h>\nint main(void) {{ raise({name}); return 42; }}\n",
            )
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

        guest_handler = write_source(
            tmp,
            "guest_handler.c",
            "#include <signal.h>\n"
            "static volatile int handled;\n"
            "static void handler(int sig) { handled = sig == SIGSEGV; }\n"
            "int main(void) {\n"
            "  signal(SIGSEGV, handler);\n"
            "  volatile int *p = (volatile int *)0;\n"
            "  volatile int value = *p;\n"
            "  (void)value;\n"
            "  return handled ? 42 : 1;\n"
            "}\n",
        )
        child = PtyProcess(cccc_args(guest_handler))
        rc = child.wait()
        assert rc == 42, (rc, child.text())
        assert "Host fault:" not in child.text(), child.text()

        guest_ignore = write_source(
            tmp,
            "guest_ignore.c",
            "#include <signal.h>\n"
            "int main(void) {\n"
            "  signal(SIGSEGV, SIG_IGN);\n"
            "  volatile int *p = (volatile int *)0;\n"
            "  volatile int value = *p;\n"
            "  (void)value;\n"
            "  return 42;\n"
            "}\n",
        )
        child = PtyProcess(cccc_args(guest_ignore))
        rc = child.wait()
        assert rc == 42, (rc, child.text())
        assert "Host fault:" not in child.text(), child.text()

        restore_default = write_source(
            tmp,
            "restore_default.c",
            "#include <signal.h>\n"
            "static void handler(int sig) { (void)sig; }\n"
            "int main(void) {\n"
            "  signal(SIGSEGV, handler);\n"
            "  signal(SIGSEGV, SIG_DFL);\n"
            "  volatile int *p = (volatile int *)0;\n"
            "  return *p;\n"
            "}\n",
        )
        assert_host_fault(restore_default, "SIGSEGV")

        comptime_fault = write_source(
            tmp,
            "comptime_fault.c",
            "[[cccc::comptime(inline)]]\n"
            "$node_t *crash(void) {\n"
            "  volatile int *p = (volatile int *)0;\n"
            "  int value = *p;\n"
            "  return $int_literal(value);\n"
            "}\n"
            "int main(void) { return crash(); }\n",
        )
        assert_host_fault(comptime_fault, "SIGSEGV")

        thread_fault = write_source(
            tmp,
            "thread_fault.c",
            "#include <pthread.h>\n"
            "static void *worker(void *arg) {\n"
            "  (void)arg;\n"
            "  volatile int *p = (volatile int *)0;\n"
            "  return (void *)(long)*p;\n"
            "}\n"
            "int main(void) {\n"
            "  pthread_t thread;\n"
            "  if (pthread_create(&thread, 0, worker, 0)) return 1;\n"
            "  pthread_join(thread, 0);\n"
            "  return 42;\n"
            "}\n",
        )
        assert_host_fault(thread_fault, "SIGSEGV")

    print("host-signal debugger integration: passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
