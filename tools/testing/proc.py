"""Single chokepoint for spawning per-test subprocesses (#1185).

Every test-execution site in this package used to call
`subprocess.run(cmd, capture_output=True, text=True, cwd=..., timeout=...)`
directly. Three unbounded-wait surfaces fell out of that, all triggered
together in a Rosetta-emulated container run at -j8 (#1185's reported hang --
all 8 worker threads plus main parked on futexes, several exited [cccc]
children never reaped):

  1. No `stdin=` redirect -- every child inherits the harness's own stdin (a
     shared tty in an interactive run, or whatever fd 0 happens to be in
     CI). A child that ever reads stdin blocks forever on input nobody will
     provide.
  2. `communicate()` (which `subprocess.run` uses internally) only returns
     once BOTH pipes hit EOF. Under -c=native, cccc's own compile step is
     the direct child, but the artifact it produces (or the host cc/ld it
     shells out to) is a grandchild that inherits the same stdout/stderr
     write ends. If the direct child exits while a grandchild is still
     running, the direct child becomes a zombie -- reaped only once
     `communicate()` returns -- while the grandchild keeps the pipe open and
     EOF never arrives. This matches #1185's "exited but never reaped [cccc]
     processes" exactly.
  3. `subprocess.run`'s own TimeoutExpired handling calls `.kill()` on the
     direct child ONLY. A grandchild is untouched, keeps holding the pipe
     open, and the hang continues even after the "timeout".

`run_capture()` closes all three: the child (and everything it spawns) gets
its own process group via `start_new_session=True`, stdin is always
`DEVNULL`, and a timeout kills the whole group (`os.killpg`), not just the
direct child. Every subprocess.run call in runner.py/native.py and
run_tests.py's native_skip_audit shell-out goes through this one function.

Known residual gap (#1202): the `timeout=` argument only bounds
`proc.communicate()` below -- `subprocess.Popen(...)` itself, a few lines
above the `try`, is not covered by any timeout here. `start_new_session=
True` (needed for the process-group kill above) rules out CPython's
`posix_spawn` fast path, so this is a real `fork()`+`exec()`, and
`Popen.__init__` blocks on an unbounded read of its errpipe waiting for
`exec()` to either succeed (EOF) or report a failure back to the parent. A
child wedged between `fork` and `exec` -- or a parent wedged inside the
`fork()` call itself, e.g. contending on the libc allocator/malloc-arena
lock another thread holds across the fork -- hangs here, before this
function's own timeout is ever armed. This is not fixable by rewriting
Popen; tools/testing/wedge.py's external deadline watchdog exists
specifically to catch a hang that starts here, not to eliminate it.
"""

import os
import signal
import subprocess


def run_capture(cmd, timeout=None, cwd=None):
    """Run `cmd`, capturing text stdout/stderr, with stdin closed and the
    whole process group killed on timeout -- not just the direct child.

    Returns a subprocess.CompletedProcess-alike (has .returncode, .stdout,
    .stderr) on success. Raises subprocess.TimeoutExpired on timeout, same
    as subprocess.run, so existing `except subprocess.TimeoutExpired:` call
    sites need no other change.
    """
    proc = subprocess.Popen(
        cmd, cwd=cwd, stdin=subprocess.DEVNULL,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
        start_new_session=True,  # own process group -- see module docstring
    )
    try:
        stdout, stderr = proc.communicate(timeout=timeout)
    except subprocess.TimeoutExpired:
        _killpg(proc.pid)
        # Drain what we can (bounded: the group is dead or dying) so the
        # pipes don't leak, then re-raise -- callers already handle this.
        try:
            proc.communicate(timeout=5)
        except subprocess.TimeoutExpired:
            # A grandchild that detached into its own session (rare -- not
            # how a host cc/ld or a compiled test artifact behaves, but not
            # impossible) can still hold the pipe open even after the group
            # kill above. Never fall back to an unbounded communicate() to
            # wait it out -- that would just relocate the wedge rather than
            # fix it. Close our ends directly so nothing can block on them
            # again, and bound the final reap too.
            for stream in (proc.stdout, proc.stderr):
                try:
                    stream.close()
                except Exception:
                    pass
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                pass
        raise
    return subprocess.CompletedProcess(cmd, proc.returncode, stdout, stderr)


def _killpg(pid):
    try:
        os.killpg(pid, signal.SIGKILL)
    except (ProcessLookupError, PermissionError, OSError):
        pass
    # #1202: os.killpg targets the process *group* the direct child was
    # placed in by start_new_session=True above. A grandchild that races to
    # call setsid() before the killpg lands escapes that group entirely (see
    # test_proc_wedge.py's "fully detaches" case) and killpg alone won't
    # reach the direct child either in that split second. Also SIGKILL the
    # direct child by pid directly, belt-and-suspenders: cheap, and it's the
    # one process we always know the exact pid of.
    try:
        os.kill(pid, signal.SIGKILL)
    except (ProcessLookupError, PermissionError, OSError):
        pass
