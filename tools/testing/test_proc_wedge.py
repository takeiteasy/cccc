#!/usr/bin/env python3
"""Unit tests for tools/testing/proc.py's run_capture() (#1185).

#1185 reported tools/run_tests.py -j8 wedging indefinitely inside a Rosetta-
emulated container: all 8 worker threads plus main parked on futexes,
several exited [cccc] child processes never reaped. The single event was
never root-caused, but the mechanism is reproducible in isolation: a direct
child that exits while a grandchild it spawned still holds the inherited
stdout/stderr pipes open leaves subprocess.run's communicate() waiting for
an EOF that never arrives, since the grandchild's process is not touched by
subprocess.run's own timeout-kill path (which only signals the direct
child).

These tests spawn exactly that shape (direct child exits immediately after
forking a grandchild that sleeps with the inherited pipe fds still open) and
assert run_capture() does not hang past its timeout and the grandchild is
gone afterwards -- no Rosetta or container needed, deterministic and fast.

Run standalone: `python3 tools/testing/test_proc_wedge.py`.
"""

import os
import subprocess
import sys
import tempfile
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from testing.proc import run_capture

CASES = []


def case(name):
    def deco(fn):
        CASES.append((name, fn))
        return fn
    return deco


@case("run_capture times out boundedly, and kills the grandchild too")
def _(fail):
    # The direct child exits almost immediately after forking a grandchild
    # that keeps the inherited stdout/stderr pipes open for far longer than
    # our timeout -- exactly #1185's shape ("already-exited [cccc] child
    # processes never reaped"). A bare subprocess.run(timeout=...) would
    # raise TimeoutExpired around `timeout` seconds too, but its own kill
    # path only signals the *direct* child -- the grandchild, still in the
    # same process group (no setsid here, matching a real host cc/ld/
    # compiled-artifact child), would be left running and holding the pipe.
    # run_capture's os.killpg must reach it as well.
    with tempfile.NamedTemporaryFile(mode="r", delete=False) as f:
        pid_file = f.name
    script = (
        f"import os, sys, time\n"
        f"pid = os.fork()\n"
        f"if pid == 0:\n"
        f"    with open({pid_file!r}, 'w') as f:\n"
        f"        f.write(str(os.getpid()))\n"
        f"    time.sleep(30)\n"
        f"    os._exit(0)\n"
        f"sys.exit(0)\n"
    )
    start = time.perf_counter()
    try:
        run_capture([sys.executable, "-c", script], timeout=2)
        fail("expected TimeoutExpired: the grandchild holds the pipe open "
             "well past the timeout")
        return
    except subprocess.TimeoutExpired:
        pass
    elapsed = time.perf_counter() - start
    if elapsed > 15.0:
        fail(f"run_capture took {elapsed:.1f}s to raise TimeoutExpired for a "
             f"2s timeout -- the group kill + bounded drain should not "
             f"approach the grandchild's own 30s sleep")

    # Give the grandchild a moment to have written its pid, then confirm
    # os.killpg actually reached it -- this is the property #1185's
    # unreaped-child observation depends on.
    for _ in range(20):
        if os.path.getsize(pid_file) > 0:
            break
        time.sleep(0.1)
    grandchild_pid = int(Path(pid_file).read_text())
    os.unlink(pid_file)
    time.sleep(0.2)  # let SIGKILL take effect
    try:
        os.kill(grandchild_pid, 0)
        fail(f"grandchild pid {grandchild_pid} is still alive after "
             f"run_capture's timeout handling -- os.killpg did not reach it")
    except ProcessLookupError:
        pass  # gone, as expected


@case("run_capture never blocks unboundedly even if a grandchild fully detaches")
def _(fail):
    # The pathological case run_capture can't structurally prevent (a
    # grandchild that calls os.setsid(), escaping the process group killpg
    # targets) -- not how cc/ld/a compiled test artifact behave, but the
    # fallback path (closing our pipe ends directly rather than a second
    # unbounded communicate()) must still return in bounded time rather
    # than merely relocating the wedge.
    script = (
        "import os, time\n"
        "pid = os.fork()\n"
        "if pid == 0:\n"
        "    os.setsid()\n"
        "    time.sleep(30)\n"
        "    os._exit(0)\n"
        "os._exit(0)\n"
    )
    start = time.perf_counter()
    try:
        run_capture([sys.executable, "-c", script], timeout=1)
        fail("expected TimeoutExpired: a detached grandchild still holds "
             "the pipe open")
    except subprocess.TimeoutExpired:
        pass
    elapsed = time.perf_counter() - start
    if elapsed > 15.0:
        fail(f"run_capture took {elapsed:.1f}s to return even in the fully-"
             f"detached-grandchild case -- it must never fall back to an "
             f"unbounded wait")


@case("run_capture closes stdin (no hang on a test that reads it)")
def _(fail):
    # sys.stdin.read() would hang forever on an inherited tty/pipe with no
    # writer; with stdin=DEVNULL it sees immediate EOF.
    start = time.perf_counter()
    result = run_capture(
        [sys.executable, "-c", "import sys; sys.stdin.read(); print('done')"],
        timeout=5,
    )
    elapsed = time.perf_counter() - start
    if elapsed > 4.0:
        fail(f"run_capture took {elapsed:.1f}s -- stdin should be DEVNULL, "
             f"giving immediate EOF, not left inherited")
    if "done" not in result.stdout:
        fail(f"expected 'done' on stdout, got {result.stdout!r}")


def main():
    failures = []
    for name, fn in CASES:
        errors = []
        fn(errors.append)
        if errors:
            failures.append((name, errors))

    if not failures:
        print(f"test_proc_wedge: {len(CASES)} cases passed")
        return 0

    for name, errors in failures:
        print(f"FAILED: {name}")
        for e in errors:
            print(f"    {e}")
    print(f"\n{len(failures)}/{len(CASES)} cases failed")
    return 1


if __name__ == "__main__":
    sys.exit(main())
