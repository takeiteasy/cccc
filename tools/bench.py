#!/usr/bin/env python3
"""Cross-compiler benchmark runner for CCCC.

Compiles and runs a set of portable C99/C11 benchmark programs under CCCC
(the VM has no optimiser -- one configuration) and under GCC (-O0 and -O2),
runs the Lua ports in tests/benchmarks/lua/ as a reference interpreter,
verifies that every configuration produces identical output, and reports wall
clock timings and peak resident memory as human-readable tables and as a
machine-readable JSON file. The cccc column covers compiling and running in one
process; the gcc columns cover the prebuilt binary alone (its compile peak is
in the JSON as compile_peak_rss_mb) and lua covers the interpreter.

Usage:
    python3 bench.py [options]

Options:
    --benchmarks DIR    benchmark source directory (default: tests/benchmarks)
    --runs N            timed iterations per (bench, config) (default: 3)
    --warmup N          warmup iterations discarded (default: 1)
    --cccc PATH          path to cccc binary (default: ./cccc)
    --gcc PATH          path to gcc binary (default: gcc)
    --lua PATH          lua interpreter for the reference column (default: lua;
                        the column is skipped when it is not installed)
    --include PATH      include flag for cccc (default: -I./include)
    --format FMT        output: table, json, both (default: both)
    --json-out PATH     json output file (default: profile/bench-results/run-<utc>.json)
    --filter PATTERN    glob filter on benchmark source names
    --no-correctness    skip the stdout-equality check
    --vm-profile        collect VM opcode profile JSON for CCCC configs
    --selfhost          also measure peak memory compiling cccc's own sources
                        (cccc --ast and -m versus gcc -c per file)
    --keep-builds       keep compiled gcc binaries in build/ (default: clean)
    --quiet             suppress per-benchmark progress output
"""

import argparse
import datetime
import fnmatch
import json
import os
import platform
import shutil
import statistics
import subprocess
import sys
import tempfile
import time
from types import SimpleNamespace
from pathlib import Path

CCCC_CONFIGS = [
    ("cccc", []),
]

GCC_CONFIGS = [
    ("gcc-O0", "O0"),
    ("gcc-O2", "O2"),
]

LUA_LABEL = "lua"

GCC_EXTRA_FLAGS = ["-ffp-contract=off", "-std=c11"]


def run_cmd(cmd, cwd, env=None):
    """Runs cmd; returns (result, elapsed seconds) with result.peak_rss_mb."""
    with tempfile.TemporaryFile() as out, tempfile.TemporaryFile() as err:
        start = time.perf_counter()
        proc = subprocess.Popen(cmd, stdout=out, stderr=err, cwd=cwd, env=env)
        _, status, usage = os.wait4(proc.pid, 0)
        elapsed = time.perf_counter() - start
        out.seek(0)
        err.seek(0)
        result = SimpleNamespace(
            stdout=out.read().decode(errors="replace"),
            stderr=err.read().decode(errors="replace"),
            returncode=os.waitstatus_to_exitcode(status),
            # ru_maxrss is bytes on macOS and kilobytes elsewhere.
            peak_rss_mb=usage.ru_maxrss
            / (1024 * 1024 if sys.platform == "darwin" else 1024),
        )
    proc.returncode = result.returncode
    return result, elapsed


def compiler_version(cmd):
    try:
        r = subprocess.run(
            [cmd, "--version"], capture_output=True, text=True, timeout=10
        )
        return r.stdout.splitlines()[0].strip() if r.stdout else "unknown"
    except Exception:
        return "unknown"


def is_clang_disguised_as_gcc(gcc_path):
    try:
        r = subprocess.run(
            [gcc_path, "--version"], capture_output=True, text=True, timeout=10
        )
        first = (r.stdout.splitlines() or [""])[0].lower()
        return "clang" in first
    except Exception:
        return False


def find_real_gcc():
    for cand in ("gcc-16", "gcc-15", "gcc-14", "gcc-13", "gcc-12", "gcc-11"):
        for prefix in ("/opt/homebrew/bin", "/usr/local/bin", "/usr/bin"):
            p = Path(prefix) / cand
            if p.exists():
                return str(p)
    return None


def time_runs(runs_fn, n_runs, n_warmup):
    times = []
    peaks = []
    stdout = None
    exit_code = None
    stable = True
    for i in range(n_runs + n_warmup):
        r, t = runs_fn()
        times.append(t)
        if i >= n_warmup:
            peaks.append(r.peak_rss_mb)
            if stdout is None:
                stdout = r.stdout
                exit_code = r.returncode
            elif r.stdout != stdout:
                stable = False
            if r.returncode != exit_code:
                stable = False
    timed = [t * 1000.0 for t in times[n_warmup:]]
    return {
        "runs_ms": timed,
        "min_ms": min(timed),
        "median_ms": statistics.median(timed),
        "mean_ms": statistics.mean(timed),
        "peak_rss_mb": statistics.median(peaks),
        "stable": stable,
        "exit_code": exit_code,
        "stdout": stdout or "",
    }


def build_gcc(src, out, gcc, opt, root, log):
    """Returns the compile's peak RSS in MB, or None when the build fails."""
    cmd = [gcc, f"-{opt}", *GCC_EXTRA_FLAGS, "-o", str(out), str(src)]
    r, _ = run_cmd(cmd, root)
    if r.returncode != 0:
        log(f"  gcc -O{opt[-1]} build failed for {src.name}:\n{r.stderr.strip()}")
        return None
    return r.peak_rss_mb


def run_benchmark(src, args, root, build_dir, log):
    log(f"\n=== {src.name} ===")
    log("  compiling gcc variants...")
    binaries = {}
    compile_peaks = {}
    for label, opt in GCC_CONFIGS:
        out = build_dir / f"{src.stem}-{opt}"
        compile_peak = build_gcc(src, out, args.gcc, opt, root, log)
        if compile_peak is None:
            continue
        binaries[label] = out
        compile_peaks[label] = compile_peak

    results = {}

    log("  running cccc variants...")
    for label, extra in CCCC_CONFIGS:
        profile_path = None
        if args.vm_profile:
            profile_path = args.vm_profile_dir / f"{src.stem}-{label}.json"

        def make_cccc():
            profile_args = ["--vm-profile", "--json"] if profile_path else []
            cmd = [args.cccc, args.include, *extra, *profile_args, str(src)]
            return run_cmd(cmd, root)

        r = time_runs(make_cccc, args.runs, args.warmup)
        if profile_path and r.get("stdout"):
            profile_path.write_text(r["stdout"])
            r["vm_profile_json"] = str(profile_path)
        results[label] = r
        log(
            f"    {label:<12} median={r['median_ms']:>10.1f}ms  min={r['min_ms']:>10.1f}ms"
            f"  peak={r['peak_rss_mb']:>7.1f}MB"
        )

    log("  running gcc variants...")
    for label, opt in GCC_CONFIGS:
        if label not in binaries:
            continue
        binp = binaries[label]

        def make_gcc():
            return run_cmd([str(binp)], root)

        r = time_runs(make_gcc, args.runs, args.warmup)
        r["compile_peak_rss_mb"] = compile_peaks[label]
        results[label] = r
        log(
            f"    {label:<12} median={r['median_ms']:>10.1f}ms  min={r['min_ms']:>10.1f}ms"
            f"  peak={r['peak_rss_mb']:>7.1f}MB"
        )

    lua_src = args.lua_dir / f"{src.stem}.lua"
    if args.lua and lua_src.exists():
        log("  running lua...")

        def make_lua():
            return run_cmd([args.lua, str(lua_src)], root)

        r = time_runs(make_lua, args.runs, args.warmup)
        results[LUA_LABEL] = r
        log(
            f"    {LUA_LABEL:<12} median={r['median_ms']:>10.1f}ms  min={r['min_ms']:>10.1f}ms"
            f"  peak={r['peak_rss_mb']:>7.1f}MB"
        )
    elif args.lua:
        log(f"  no lua port: {lua_src.relative_to(root)}")

    correctness = {"status": "ok", "matches": {}, "ref": "cccc"}
    if not args.no_correctness and "cccc" in results:
        ref = results["cccc"]["stdout"]
        for label, r in results.items():
            if label == "cccc":
                continue
            match = r["stdout"] == ref
            correctness["matches"][label] = match
            if not match:
                correctness["status"] = "mismatch"
    if not args.no_correctness and "cccc" not in results:
        correctness["status"] = "no_reference"

    if correctness["status"] == "ok":
        log("  correctness: all configs match cccc")
    elif correctness["status"] == "mismatch":
        bad = [l for l, m in correctness["matches"].items() if not m]
        log(f"  correctness: MISMATCH with {bad}")
    else:
        log(f"  correctness: {correctness['status']}")

    return {
        "name": src.name,
        "stem": src.stem,
        "configs": results,
        "correctness": correctness,
    }


def detect_platform_info():
    info = {
        "system": platform.system(),
        "release": platform.release(),
        "machine": platform.machine(),
        "python": platform.python_version(),
    }
    try:
        with open("/proc/cpuinfo") as f:
            for line in f:
                if "model name" in line:
                    info["cpu"] = line.split(":", 1)[1].strip()
                    break
    except Exception:
        info["cpu"] = platform.processor() or "unknown"
    try:
        info["n_cpu"] = os.cpu_count()
    except Exception:
        pass
    return info


def config_columns(records, cccc_configs, gcc_configs):
    """cccc, then lua when any benchmark ran it, then gcc."""
    has_lua = any(LUA_LABEL in rec["configs"] for rec in records)
    return (
        [c[0] for c in cccc_configs]
        + ([LUA_LABEL] if has_lua else [])
        + [c[0] for c in gcc_configs]
    )


def render_table(records, cccc_configs, gcc_configs):
    all_configs = config_columns(records, cccc_configs, gcc_configs)
    headers = ["benchmark"] + all_configs
    rows = []
    for rec in records:
        row = [rec["stem"]]
        for cfg in all_configs:
            r = rec["configs"].get(cfg)
            if r is None:
                row.append("-")
            else:
                row.append(f"{r['median_ms']:.1f}")
        status = rec["correctness"]["status"]
        if status == "ok":
            row.append("ok")
        elif status == "mismatch":
            bad = [l for l, m in rec["correctness"]["matches"].items() if not m]
            row.append(f"MISMATCH:{','.join(bad)}")
        else:
            row.append(status)
        rows.append(row)

    widths = [
        max(len(h), max((len(r[i]) for r in rows), default=0))
        for i, h in enumerate(headers)
    ]
    lines = []
    sep = "  "
    lines.append(sep.join(h.ljust(widths[i]) for i, h in enumerate(headers)))
    lines.append(sep.join("-" * widths[i] for i in range(len(headers))))
    for r in rows:
        lines.append(sep.join(r[i].ljust(widths[i]) for i in range(len(headers))))
    return "\n".join(lines)


def render_memory_table(records, cccc_configs, gcc_configs):
    all_configs = config_columns(records, cccc_configs, gcc_configs)
    headers = ["benchmark"] + all_configs
    rows = []
    for rec in records:
        row = [rec["stem"]]
        for cfg in all_configs:
            r = rec["configs"].get(cfg)
            row.append("-" if r is None else f"{r['peak_rss_mb']:.1f}")
        rows.append(row)
    widths = [
        max(len(h), max((len(r[i]) for r in rows), default=0))
        for i, h in enumerate(headers)
    ]
    sep = "  "
    lines = [sep.join(h.ljust(widths[i]) for i, h in enumerate(headers))]
    lines.append(sep.join("-" * widths[i] for i in range(len(headers))))
    for r in rows:
        lines.append(sep.join(r[i].ljust(widths[i]) for i in range(len(headers))))
    lines.append(
        "cccc = compile and run in one process; gcc = the prebuilt binary alone;"
        " lua = the interpreter"
    )
    return "\n".join(lines)


def render_speedup_table(records, cccc_configs, gcc_configs):
    lines = []
    all_configs = config_columns(records, cccc_configs, gcc_configs)
    header = ["benchmark"] + all_configs
    rows = []
    speedups_by_cfg = {cfg: [] for cfg in all_configs}

    for rec in records:
        gcc_o2 = rec["configs"].get("gcc-O2", {}).get("median_ms")
        if not gcc_o2 or gcc_o2 <= 0:
            continue
        row = [rec["stem"]]
        for cfg in all_configs:
            r = rec["configs"].get(cfg, {}).get("median_ms")
            if r is None or r <= 0:
                row.append("-")
                continue
            ratio = r / gcc_o2
            speedups_by_cfg[cfg].append(ratio)
            if ratio < 1.0:
                row.append(f"{ratio:.2f}x")
            else:
                row.append(f"{ratio:.1f}x")
        rows.append(row)

    if not rows:
        return "(no comparable results)"

    geomeans = {}
    for cfg, ratios in speedups_by_cfg.items():
        if ratios:
            gm = 1.0
            for r in ratios:
                gm *= r
            geomeans[cfg] = gm ** (1.0 / len(ratios))

    widths = [
        max(len(h), max((len(r[i]) for r in rows), default=0))
        for i, h in enumerate(header)
    ]
    geo_row = ["geomean"] + [f"{geomeans.get(c, 0):.2f}x" for c in all_configs]
    widths = [max(widths[i], len(geo_row[i])) for i in range(len(header))]

    sep = "  "
    out = ["Speedup vs gcc -O2 (>1.0x = slower than gcc -O2):"]
    out.append(sep.join(h.ljust(widths[i]) for i, h in enumerate(header)))
    out.append(sep.join("-" * widths[i] for i in range(len(header))))
    for r in rows:
        out.append(sep.join(r[i].ljust(widths[i]) for i in range(len(header))))
    out.append(sep.join(geo_row[i].ljust(widths[i]) for i in range(len(header))))
    return "\n".join(out)


def selfhost_flags(root):
    """The cccc flags and gcc include flags the ouroboros target uses."""
    cccc = ["--use-system-headers", "-I", "include", "-I", "src"]
    gcc = ["-I", "include", "-I", "src"]
    if sys.platform == "darwin":
        sdk = subprocess.run(
            ["xcrun", "--show-sdk-path"], capture_output=True, text=True
        ).stdout.strip()
        cccc += [
            "--isystem", f"{sdk}/usr/include", "-I", f"{sdk}/usr/include/ffi",
            "-F", f"{sdk}/System/Library/Frameworks",
        ]
        gcc += ["-I", f"{sdk}/usr/include/ffi"]
    else:
        ffi = sorted(Path("/usr/include").glob("*/ffi.h"))
        if ffi:
            cccc += ["--isystem", str(ffi[0].parent)]
            gcc += ["-I", str(ffi[0].parent)]
        cccc += ["--isystem", "/usr/include"]
    return cccc, gcc


def peak_footprint_mb(cmd, cwd):
    """Peak memory of cmd in MB: macOS reports the footprint (ru_maxrss leaves
    out compressed pages), elsewhere the peak RSS. Returns (mb, returncode)."""
    if sys.platform != "darwin":
        r, _ = run_cmd(cmd, cwd)
        return r.peak_rss_mb, r.returncode
    r, _ = run_cmd(["/usr/bin/time", "-l", *cmd], cwd)
    for line in r.stderr.splitlines():
        if line.strip().endswith("peak memory footprint"):
            return int(line.split()[0]) / (1024 * 1024), r.returncode
    return r.peak_rss_mb, r.returncode


def run_selfhost(args, root, log):
    """Peak memory compiling cccc's own sources: the whole-program front end
    (--ast) and front end plus serializer (-m) of cccc, and the largest single
    translation unit of gcc -c."""
    srcs = sorted(
        str(p.relative_to(root))
        for p in [*root.glob("src/*.c"), *root.glob("src/stdlib/*.c")]
        if p.name != "ops.c"
    )
    cccc_flags, gcc_flags = selfhost_flags(root)
    out = {"sources": len(srcs)}
    for label, mode in (("cccc --ast", "--ast"), ("cccc -m", "-m")):
        log(f"  {label} over {len(srcs)} sources...")
        mb, code = peak_footprint_mb([args.cccc, *cccc_flags, mode, *srcs], root)
        out[label] = {"peak_mb": mb, "exit_code": code}
    log("  gcc -c per source...")
    worst = (0.0, "")
    for src in srcs:
        # wait4's peak includes the driver's cc1 child; the footprint would not.
        r, _ = run_cmd(
            [args.gcc, "-c", "-w", *gcc_flags, "-o", os.devnull, src], root
        )
        if r.returncode == 0 and r.peak_rss_mb > worst[0]:
            worst = (r.peak_rss_mb, src)
    out["gcc -c"] = {"peak_mb": worst[0], "largest": worst[1]}
    return out


def render_selfhost(res):
    lines = [f"Compiling cccc's {res['sources']} sources, peak memory in MB:"]
    for label in ("cccc --ast", "cccc -m", "gcc -c"):
        r = res[label]
        note = ""
        if label == "gcc -c":
            note = f"  (largest single file: {r['largest']})"
        elif r["exit_code"] != 0:
            note = f"  (exit {r['exit_code']})"
        lines.append(f"  {label:<12} {r['peak_mb']:>9.0f}{note}")
    lines.append(
        "cccc holds every file in one program; gcc compiles one file per process."
    )
    return "\n".join(lines)


def make_run_id():
    return datetime.datetime.now(datetime.UTC).strftime("%Y%m%dT%H%M%SZ")


def main():
    p = argparse.ArgumentParser(description="Cross-compiler benchmark runner for CCCC")
    p.add_argument("--benchmarks", default="tests/benchmarks")
    p.add_argument("--runs", type=int, default=3)
    p.add_argument("--warmup", type=int, default=1)
    p.add_argument("--cccc", default="./cccc")
    p.add_argument("--gcc", default="gcc")
    p.add_argument("--lua", default="lua")
    p.add_argument("--include", default="-I./include")
    p.add_argument("--format", choices=["table", "json", "both"], default="both")
    p.add_argument("--json-out", default=None)
    p.add_argument("--filter", default=None)
    p.add_argument("--no-correctness", action="store_true")
    p.add_argument(
        "--vm-profile",
        action="store_true",
        help="write VM opcode profile JSON for CCCC configs",
    )
    p.add_argument("--selfhost", action="store_true")
    p.add_argument("--keep-builds", action="store_true")
    p.add_argument("--quiet", action="store_true")
    args = p.parse_args()

    root = Path(__file__).parent.parent.resolve()
    bench_dir = root / args.benchmarks
    if not bench_dir.exists():
        print(f"error: benchmark directory not found: {bench_dir}", file=sys.stderr)
        sys.exit(1)
    build_dir = root / "build"
    build_dir.mkdir(exist_ok=True)
    args.run_id = make_run_id()
    results_root = root / "profile" / "bench-results"
    if args.vm_profile:
        args.vm_profile_dir = results_root / f"vm-profile-{args.run_id}"
        args.vm_profile_dir.mkdir(parents=True, exist_ok=True)
    else:
        args.vm_profile_dir = None

    cccc_path = (
        (root / args.cccc).resolve()
        if not os.path.isabs(args.cccc)
        else Path(args.cccc)
    )
    if not cccc_path.exists():
        print(f"error: cccc binary not found: {cccc_path}", file=sys.stderr)
        sys.exit(1)
    args.cccc = str(cccc_path)

    if is_clang_disguised_as_gcc(args.gcc):
        real = find_real_gcc()
        msg = f"warning: '{args.gcc}' reports as clang, not GNU gcc."
        if real:
            msg += f" Auto-switching to {real}. Pass --gcc to override."
            args.gcc = real
        else:
            msg += " No GNU gcc found in /opt/homebrew/bin or /usr/local/bin."
            msg += " Install with `brew install gcc` or pass --gcc PATH."
        print(msg, file=sys.stderr)

    args.lua_dir = bench_dir / "lua"
    if args.lua:
        args.lua = shutil.which(args.lua)
        if not args.lua:
            print("note: lua not found; skipping the lua reference column",
                  file=sys.stderr)

    sources = sorted(bench_dir.glob("*.c"))
    if args.filter:
        sources = [s for s in sources if fnmatch.fnmatch(s.name, args.filter)]
    if not sources:
        print(f"error: no benchmark sources found in {bench_dir}", file=sys.stderr)
        sys.exit(1)

    def log(msg):
        if not args.quiet:
            print(msg, flush=True)

    log(f"Benchmarks: {[s.name for s in sources]}")
    log(f"Runs: {args.runs} (warmup: {args.warmup})")
    log(f"CCCC: {args.cccc}    GCC: {args.gcc}    Lua: {args.lua}")
    log(f"Configs: cccc    lua ({args.lua or 'skipped'})    gcc × {{O0,O2}}")
    if args.vm_profile:
        log(f"VM opcode profiles: {args.vm_profile_dir}")

    records = []
    for src in sources:
        try:
            rec = run_benchmark(src, args, root, build_dir, log)
            records.append(rec)
        except Exception as e:
            log(f"  ERROR: {e}")
            records.append(
                {
                    "name": src.name,
                    "stem": src.stem,
                    "configs": {},
                    "correctness": {"status": "error", "error": str(e)},
                }
            )

    if not args.keep_builds:
        for f in build_dir.glob("*-O?"):
            try:
                f.unlink()
            except Exception:
                pass

    if args.format in ("table", "both"):
        print()
        print("=" * 100)
        print(" CCCC vs GCC benchmark results (median ms, lower is better)")
        print("=" * 100)
        print(
            render_table(
                records,
                CCCC_CONFIGS,
                GCC_CONFIGS,
            )
        )
        print()
        print(
            render_speedup_table(
                records,
                CCCC_CONFIGS,
                GCC_CONFIGS,
            )
        )
        print()
        print("Peak memory (MB, lower is better):")
        print(render_memory_table(records, CCCC_CONFIGS, GCC_CONFIGS))
        print()
        bad = [r for r in records if r["correctness"]["status"] != "ok"]
        if bad:
            print("Correctness: FAILED")
            for r in bad:
                print(f"  - {r['name']}: {r['correctness']['status']}")
        else:
            print(
                "Correctness: all benchmarks produce identical output across all configs"
            )

    selfhost = None
    if args.selfhost:
        log("\n=== self-host compile memory ===")
        selfhost = run_selfhost(args, root, log)
        if args.format in ("table", "both"):
            print()
            print(render_selfhost(selfhost))

    if args.format in ("json", "both"):
        run_id = args.run_id
        if args.json_out:
            out_path = Path(args.json_out)
        else:
            results_dir = results_root
            results_dir.mkdir(parents=True, exist_ok=True)
            out_path = results_dir / f"run-{run_id}.json"

        payload = {
            "tool": "cccc-bench",
            "version": "1",
            "run_id": run_id,
            "host": detect_platform_info(),
            "compilers": {
                "cccc": compiler_version(args.cccc),
                "gcc": compiler_version(args.gcc),
                "lua": compiler_version(args.lua) if args.lua else None,
            },
            "config": {
                "runs": args.runs,
                "warmup": args.warmup,
                "include": args.include,
                "gcc_extra_flags": GCC_EXTRA_FLAGS,
            },
            "cccc_configs": [c[0] for c in CCCC_CONFIGS],
            "gcc_configs": [c[0] for c in GCC_CONFIGS],
            "selfhost": selfhost,
            "benchmarks": [
                {
                    "name": r["name"],
                    "correctness": r["correctness"],
                    "configs": {
                        cfg: {k: v for k, v in data.items() if k != "stdout"}
                        for cfg, data in r["configs"].items()
                    },
                }
                for r in records
            ],
        }
        out_path.parent.mkdir(parents=True, exist_ok=True)
        with open(out_path, "w") as f:
            json.dump(payload, f, indent=2)
        if not args.quiet:
            print(f"\nJSON results written to {out_path}")

    bad = [
        r for r in records if r["correctness"]["status"] not in ("ok", "no_reference")
    ]
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
