#!/usr/bin/env python3
"""Cross-compiler benchmark runner for CCCC.

Compiles and runs a set of portable C99/C11 benchmark programs under CCCC
(the VM has no optimiser -- one configuration) and under GCC (across
-O0..-O3), verifies that the programs produce identical output under each
compiler, and reports wall clock timings as a human-readable table and as a
machine-readable JSON file.

Usage:
    python3 bench.py [options]

Options:
    --benchmarks DIR    benchmark source directory (default: tests/benchmarks)
    --runs N            timed iterations per (bench, config) (default: 3)
    --warmup N          warmup iterations discarded (default: 1)
    --cccc PATH          path to cccc binary (default: ./cccc)
    --gcc PATH          path to gcc binary (default: gcc)
    --include PATH      include flag for cccc (default: -I./include)
    --format FMT        output: table, json, both (default: both)
    --json-out PATH     json output file (default: profile/bench-results/run-<utc>.json)
    --filter PATTERN    glob filter on benchmark source names
    --no-correctness    skip the stdout-equality check
    --vm-profile        collect VM opcode profile JSON for CCCC configs
    --keep-builds       keep compiled gcc binaries in build/ (default: clean)
    --quiet             suppress per-benchmark progress output
"""

import argparse
import datetime
import fnmatch
import json
import os
import platform
import statistics
import subprocess
import sys
import time
from pathlib import Path

CCCC_CONFIGS = [
    ("cccc", []),
]

GCC_CONFIGS = [
    ("gcc-O0", "O0"),
    ("gcc-O1", "O1"),
    ("gcc-O2", "O2"),
    ("gcc-O3", "O3"),
]

GCC_EXTRA_FLAGS = ["-ffp-contract=off", "-std=c11"]


def run_cmd(cmd, cwd, env=None):
    start = time.perf_counter()
    result = subprocess.run(cmd, capture_output=True, text=True, cwd=cwd, env=env)
    elapsed = time.perf_counter() - start
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
    for cand in ("gcc-15", "gcc-14", "gcc-13", "gcc-12", "gcc-11"):
        for prefix in ("/opt/homebrew/bin", "/usr/local/bin", "/usr/bin"):
            p = Path(prefix) / cand
            if p.exists():
                return str(p)
    return None


def time_runs(runs_fn, n_runs, n_warmup):
    times = []
    stdout = None
    exit_code = None
    stable = True
    for i in range(n_runs + n_warmup):
        r, t = runs_fn()
        times.append(t)
        if i >= n_warmup:
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
        "stable": stable,
        "exit_code": exit_code,
        "stdout": stdout or "",
    }


def build_gcc(src, out, gcc, opt, root, log):
    cmd = [gcc, f"-{opt}", *GCC_EXTRA_FLAGS, "-o", str(out), str(src)]
    r = subprocess.run(cmd, capture_output=True, text=True, cwd=root)
    if r.returncode != 0:
        log(f"  gcc -O{opt[-1]} build failed for {src.name}:\n{r.stderr.strip()}")
        return False
    return True


def run_benchmark(src, args, root, build_dir, log):
    log(f"\n=== {src.name} ===")
    log("  compiling gcc variants...")
    binaries = {}
    for label, opt in GCC_CONFIGS:
        out = build_dir / f"{src.stem}-{opt}"
        if not build_gcc(src, out, args.gcc, opt, root, log):
            continue
        binaries[label] = out

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
        )

    log("  running gcc variants...")
    for label, opt in GCC_CONFIGS:
        if label not in binaries:
            continue
        binp = binaries[label]

        def make_gcc():
            return run_cmd([str(binp)], root)

        r = time_runs(make_gcc, args.runs, args.warmup)
        results[label] = r
        log(
            f"    {label:<12} median={r['median_ms']:>10.1f}ms  min={r['min_ms']:>10.1f}ms"
        )

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


def render_table(records, cccc_configs, gcc_configs):
    all_configs = (
        [c[0] for c in cccc_configs]
        + [c[0] for c in gcc_configs]
    )
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


def render_speedup_table(records, cccc_configs, gcc_configs):
    lines = []
    all_configs = (
        [c[0] for c in cccc_configs]
        + [c[0] for c in gcc_configs]
    )
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


def make_run_id():
    return datetime.datetime.now(datetime.UTC).strftime("%Y%m%dT%H%M%SZ")


def main():
    p = argparse.ArgumentParser(description="Cross-compiler benchmark runner for CCCC")
    p.add_argument("--benchmarks", default="tests/benchmarks")
    p.add_argument("--runs", type=int, default=3)
    p.add_argument("--warmup", type=int, default=1)
    p.add_argument("--cccc", default="./cccc")
    p.add_argument("--gcc", default="gcc")
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
    log(f"CCCC: {args.cccc}    GCC: {args.gcc}")
    cccc_cfg_str = " × {none,O1,O2,O3,O4}"
    log(f"Configs: cccc{cccc_cfg_str}    gcc × {{O0,O1,O2,O3}}")
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
        bad = [r for r in records if r["correctness"]["status"] != "ok"]
        if bad:
            print("Correctness: FAILED")
            for r in bad:
                print(f"  - {r['name']}: {r['correctness']['status']}")
        else:
            print(
                "Correctness: all benchmarks produce identical output across all configs"
            )

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
            },
            "config": {
                "runs": args.runs,
                "warmup": args.warmup,
                "include": args.include,
                "gcc_extra_flags": GCC_EXTRA_FLAGS,
            },
            "cccc_configs": [c[0] for c in CCCC_CONFIGS],
            "gcc_configs": [c[0] for c in GCC_CONFIGS],
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
