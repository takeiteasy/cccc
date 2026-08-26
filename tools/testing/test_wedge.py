#!/usr/bin/env python3
"""Unit tests for tools/testing/wedge.py (#1202).

Patterned on test_proc_wedge.py: self-contained, deterministic, fast, no
container needed. wedge.py's deadline watchdog calls os._exit() when it
fires (faulthandler.dump_traceback_later(..., exit=True) -- see wedge.py's
docstring for why that's the correct behavior, not a bug), so the case that
exercises a real firing has to do it in a *child interpreter*: doing it
in-process would kill this very test runner.

Run standalone: `python3 tools/testing/test_wedge.py`.
"""

import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from testing import wedge

CASES = []


def case(name):
    def deco(fn):
        CASES.append((name, fn))
        return fn
    return deco


@case("arm() fires within its deadline, dumps a traceback, and exits nonzero")
def _(fail):
    # Run in a child interpreter (not in-process) since a real firing calls
    # os._exit() -- see module docstring.
    script = (
        "import sys, time\n"
        "sys.path.insert(0, sys.argv[1])\n"
        "from testing import wedge\n"
        "wedge.install(sys.argv[2])\n"
        "wedge.arm('unit-test-phase', 1)\n"
        "time.sleep(30)\n"  # never reached if arm() works
    )
    with tempfile.TemporaryDirectory() as base:
        proc = subprocess.run(
            [sys.executable, "-c", script, str(Path(__file__).resolve().parent.parent), base],
            capture_output=True, text=True, timeout=15,
        )
        if proc.returncode == 0:
            fail("expected a nonzero exit from the armed deadline firing, "
                 f"got 0 (stdout={proc.stdout!r} stderr={proc.stderr!r})")
        dump = Path(base) / "build" / "wedge" / "traceback.log"
        if not dump.exists():
            fail(f"expected {dump} to exist after the deadline fired")
            return
        text = dump.read_text()
        if "Timeout" not in text and "Thread" not in text:
            fail(f"traceback.log doesn't look like a faulthandler dump: {text[:300]!r}")


@case("arm() re-arming cancels the previous deadline (no spurious fire)")
def _(fail):
    script = (
        "import sys, time\n"
        "sys.path.insert(0, sys.argv[1])\n"
        "from testing import wedge\n"
        "wedge.install(sys.argv[2])\n"
        "wedge.arm('phase-a', 1)\n"
        "wedge.arm('phase-b', 5)\n"  # re-arm before phase-a's 1s would fire
        "time.sleep(2)\n"
        "wedge.disarm()\n"
        "print('survived')\n"
    )
    with tempfile.TemporaryDirectory() as base:
        proc = subprocess.run(
            [sys.executable, "-c", script, str(Path(__file__).resolve().parent.parent), base],
            capture_output=True, text=True, timeout=15,
        )
        if "survived" not in proc.stdout:
            fail(f"re-arming did not cancel the earlier deadline -- "
                 f"rc={proc.returncode} stdout={proc.stdout!r} stderr={proc.stderr!r}")


@case("disarm() with nothing armed is a safe no-op")
def _(fail):
    with tempfile.TemporaryDirectory() as base:
        wedge.install(base)
        try:
            wedge.disarm()
            wedge.disarm()
        except Exception as e:
            fail(f"disarm() with nothing armed raised: {e}")


@case("inflight() records start/end to progress.log and clears on exit")
def _(fail):
    with tempfile.TemporaryDirectory() as base:
        wedge.install(base)
        with wedge.inflight("tests/fake_test.c", phase="unit"):
            snap = wedge.snapshot_inflight()
            if "tests/fake_test.c" not in snap:
                fail(f"snapshot_inflight() missing the in-flight test: {snap}")
        # Context manager exited -- must be gone from the live snapshot.
        snap_after = wedge.snapshot_inflight()
        if "tests/fake_test.c" in snap_after:
            fail("inflight() did not clear the test after the with-block exited")

        log = (Path(base) / "build" / "wedge" / "progress.log").read_text()
        lines = log.splitlines()
        starts = [l for l in lines if l.startswith("S ") and "tests/fake_test.c" in l]
        ends = [l for l in lines if l.startswith("E ") and "tests/fake_test.c" in l]
        if not starts:
            fail(f"progress.log missing a start line: {log!r}")
        if not ends:
            fail(f"progress.log missing an end line: {log!r}")


@case("note_start/note_end round-trip without the context manager")
def _(fail):
    with tempfile.TemporaryDirectory() as base:
        wedge.install(base)
        wedge.note_start("tests/other_fake.c")
        if "tests/other_fake.c" not in wedge.snapshot_inflight():
            fail("note_start() did not register the test as in-flight")
        wedge.note_end("tests/other_fake.c")
        if "tests/other_fake.c" in wedge.snapshot_inflight():
            fail("note_end() did not clear the test")


@case("SIGUSR1 dumps all-thread stacks without killing the process")
def _(fail):
    if not hasattr(sys, "platform") or sys.platform == "win32":
        return  # SIGUSR1 is POSIX-only; wedge.install() itself guards this.
    script = (
        "import os, signal, sys, time\n"
        "sys.path.insert(0, sys.argv[1])\n"
        "from testing import wedge\n"
        "wedge.install(sys.argv[2])\n"
        "os.kill(os.getpid(), signal.SIGUSR1)\n"
        "time.sleep(0.3)\n"  # let the handler run before we exit cleanly
        "print('alive')\n"
    )
    with tempfile.TemporaryDirectory() as base:
        proc = subprocess.run(
            [sys.executable, "-c", script, str(Path(__file__).resolve().parent.parent), base],
            capture_output=True, text=True, timeout=15,
        )
        if "alive" not in proc.stdout:
            fail(f"process did not survive a SIGUSR1 dump -- "
                 f"rc={proc.returncode} stdout={proc.stdout!r} stderr={proc.stderr!r}")
            return
        dump = Path(base) / "build" / "wedge" / "traceback.log"
        if not dump.exists() or not dump.read_text().strip():
            fail(f"expected a non-empty {dump} after SIGUSR1")


def main():
    failures = []
    for name, fn in CASES:
        errors = []
        try:
            fn(errors.append)
        except Exception as e:
            errors.append(f"raised {type(e).__name__}: {e}")
        if errors:
            failures.append((name, errors))

    if not failures:
        print(f"test_wedge: {len(CASES)} cases passed")
        return 0

    for name, errors in failures:
        print(f"FAILED: {name}")
        for e in errors:
            print(f"    {e}")
    print(f"\n{len(failures)}/{len(CASES)} cases failed")
    return 1


if __name__ == "__main__":
    sys.exit(main())
