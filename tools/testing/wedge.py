"""Bounded, self-diagnosing wedge detection for the test harness (#1202).

#1202: four consecutive sr.ht CI jobs sat RUNNING on `run_tests.py -j 8` for
hours before being cancelled by hand; a prior job had already TIMEOUT'd, so
this predates any single commit -- a pre-existing, apparently intermittent
parallel-dispatch wedge. It did not reproduce in a full local `-j8` container
run, and gdb proved unusable there (Rosetta translation + a PID-namespace
mismatch garbled every backtrace). This module does not fix that wedge --
nothing here can, without first catching it live -- it makes the *next*
occurrence self-diagnosing instead of just hanging until sr.ht's own
top-level job timeout kills it with no evidence at all.

Why this can't just be `run_capture()`'s existing `--process-timeout`
(#1185): that timeout only wraps `proc.communicate()` inside
`tools/testing/proc.py`'s `run_capture()`. `subprocess.Popen(...)`
construction itself is *outside* that try block. With `start_new_session=
True` (needed for the process-group kill), CPython cannot use `posix_spawn`,
so this is a real `fork()`+`exec()`, and `Popen.__init__` does an unbounded
read on its errpipe waiting for `exec()` to either succeed (EOF) or report a
failure. A child wedged between `fork` and `exec` -- or a parent wedged
inside the `fork()` call itself, which is exactly the shape of the stale
10-day-old specimen found in the verification container (parent blocked in
`futex(..., FUTEX_LOCK_PI_PRIVATE, NULL)`, 7 unreaped zombie children) --
blocks there before any per-subprocess timeout is even armed. Several other
sub-suites in tools/run_tests.py also run in-process via `importlib` with no
timeout at all (`_run_repl_suite`, `_run_sqlite_suite`,
`_run_header_resolution_suite`, `_run_comptime_native_suite`,
`_run_host_attribute_link_smoke_suite`). None of these gaps are reachable
from a per-`subprocess.Popen` fix -- they need a watchdog external to any
single subprocess call, which is what this module provides.

Two independent facilities:

  * A **deadline watchdog** (`arm`/`disarm`), built on
    `faulthandler.dump_traceback_later`, not a hand-rolled Python thread. A
    plain watchdog thread needs the GIL to run and be scheduled; if the
    wedge parks a thread while holding the GIL (a real possibility for a
    fork()-adjacent hang), a Python-level watchdog never gets to run either.
    `dump_traceback_later` arms a timer in a dedicated C thread that does
    not need the GIL, so it still fires. It dumps every thread's Python
    stack to a file and then calls `os._exit()` -- which does NOT flush
    Python's buffered stdout, so the dump must go to its own file (and
    stderr), never stdout; blocked stdout is itself a suspect (see
    tools/run_tests.py's `main()` reconfiguring line-buffering) and relying
    on it here would be catching a wedge by using the same wedge.

  * A **live SIGUSR1 dump** (`install`), so a wedge caught by a human
    watching the job (or a future automated poller) can be inspected with
    `kill -USR1 <pid>` -- getting a real Python stack for every thread
    without needing gdb at all, and thus sidestepping the container's
    Rosetta/PID-namespace gdb problems entirely.

Both write to `build/wedge/traceback.log`. A third, append-only file,
`build/wedge/progress.log`, records `note_start`/`note_end` for every
per-test subprocess as plain `S <mono> <phase> <name>` / `E <mono> <phase>
<name>` lines -- eager file writes, not an in-memory registry, because a
registry is only readable by Python code that may itself never be scheduled
again once the wedge hits. Subtracting `E` lines from `S` lines in the
post-mortem gives exactly the set of tests in flight when the process died:
the one thing a bare thread-stack dump cannot say on its own, and which
#1202 explicitly noted the CI log's printed order cannot reliably say either
(parallel workers finish out of submission order).
"""

import contextlib
import faulthandler
import os
import signal
import sys
import threading
import time
from pathlib import Path

_dump_fh = None
_progress_fh = None
_lock = threading.Lock()
_inflight = {}  # name -> start monotonic, for snapshot_inflight()


def _dump_dir(base_dir):
    d = Path(base_dir) / "build" / "wedge"
    d.mkdir(parents=True, exist_ok=True)
    return d


def install(base_dir):
    """Enable faulthandler's crash/timeout dump and, on POSIX, a SIGUSR1
    live-dump handler. Call once, early in main(). Returns the dump
    directory path (informational, for printing to the user)."""
    global _dump_fh, _progress_fh
    d = _dump_dir(base_dir)
    _dump_fh = open(d / "traceback.log", "a", buffering=1)
    _progress_fh = open(d / "progress.log", "a", buffering=1)
    faulthandler.enable(file=_dump_fh, all_threads=True)
    if hasattr(faulthandler, "register") and hasattr(signal, "SIGUSR1"):
        # chain=False: SIGUSR1's default disposition is to terminate the
        # process. chain=True would re-invoke that default handler right
        # after dumping (there is no other SIGUSR1 handler installed here to
        # preserve), turning every diagnostic dump into a self-inflicted
        # kill -- exactly what this handler exists to let the operator avoid
        # doing with a plain `kill` while still getting a live dump.
        faulthandler.register(signal.SIGUSR1, file=_dump_fh,
                               all_threads=True, chain=False)
    return d


def arm(phase, seconds):
    """(Re-)arm a deadline: if `phase` hasn't finished within `seconds`,
    dump every thread's Python stack to the wedge log and hard-exit. Cancels
    any previously-armed deadline first, so callers can just call arm() at
    the top of every sub-suite/phase without an explicit disarm() between
    them -- the previous phase's deadline is implicitly cleared. No-op if
    install() was never called or seconds <= 0."""
    if _dump_fh is None or not seconds or seconds <= 0:
        return
    if _dump_fh is not None:
        print(f"# --phase-timeout: armed for phase {phase!r}, {seconds}s",
              file=_dump_fh, flush=True)
    faulthandler.dump_traceback_later(seconds, file=_dump_fh, exit=True)


def disarm():
    """Cancel any armed deadline. Safe to call even if none is armed."""
    faulthandler.cancel_dump_traceback_later()


@contextlib.contextmanager
def inflight(name, phase="test"):
    """Context manager wrapping one per-test subprocess invocation. Records
    a start/end line to progress.log (if install() has run) and tracks the
    name in the in-memory snapshot for as long as any code is still around
    to read it. Safe to use even if install() was never called (no-op)."""
    start = time.monotonic()
    with _lock:
        _inflight[name] = start
    if _progress_fh is not None:
        with _lock:
            print(f"S {start:.3f} {phase} {name}", file=_progress_fh)
    try:
        yield
    finally:
        end = time.monotonic()
        with _lock:
            _inflight.pop(name, None)
        if _progress_fh is not None:
            with _lock:
                print(f"E {end:.3f} {phase} {name}", file=_progress_fh)


def note_start(name, phase="test"):
    """Non-context-manager form of inflight()'s start half, for callers
    that can't use a `with` block. Pair with note_end(name)."""
    start = time.monotonic()
    with _lock:
        _inflight[name] = start
    if _progress_fh is not None:
        with _lock:
            print(f"S {start:.3f} {phase} {name}", file=_progress_fh)


def note_end(name, phase="test"):
    end = time.monotonic()
    with _lock:
        _inflight.pop(name, None)
    if _progress_fh is not None:
        with _lock:
            print(f"E {end:.3f} {phase} {name}", file=_progress_fh)


def snapshot_inflight():
    """Return {name: elapsed_seconds} for everything currently in flight,
    per the in-memory registry. Only useful while Python is still being
    scheduled -- see the module docstring for why progress.log (not this)
    is the source of truth once a wedge is suspected."""
    now = time.monotonic()
    with _lock:
        return {name: now - start for name, start in _inflight.items()}
