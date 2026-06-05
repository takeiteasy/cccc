#!/usr/bin/env python3
"""Cross-reference static and dynamic opcode bigram counts.

Reads the text output of tools/bytecode_ngrams (static .jbc analysis) and
the JSON output of `jcc --vm-profile-json` (runtime bigram counts) and
prints a unified ranking. The score is `static_count * dynamic_count`,
so sequences that are both common in the bytecode AND executed many
times rise to the top — these are the strongest fusion candidates
(see ticket #250).

Usage:
    tools/cross_ref_ngrams.py file.jbc file.c [extra_run_args...]

The .jbc file is the compiled bytecode for `file.c`; `extra_run_args` are
passed to the compiled program so you can profile non-default inputs
(e.g. a larger benchmark size).

Environment variables:
    JCC          path to the jcc binary     (default: ./jcc)
    NGRAM_TOOL   path to bytecode_ngrams    (default: ./tools/bytecode_ngrams)

The VM profile currently tracks only bigrams; trigram cross-referencing
falls back to scoring the trigram's first bigram as a heuristic.
"""
import argparse
import json
import os
import re
import subprocess
import sys

JCC = os.environ.get("JCC", "./jcc")
NGRAM_TOOL = os.environ.get("NGRAM_TOOL", "./tools/bytecode_ngrams")

STATIC_RE = re.compile(
    r"^\s*(?P<count>\d+)\s+(?P<ops>(?:[A-Z0-9_]+(?:\s+[A-Z0-9_]+)+))"
)


def parse_static(text):
    """Yield (count, opcode_sequence_str) from n-gram tool text output."""
    for line in text.splitlines():
        line = line.rstrip()
        m = STATIC_RE.match(line)
        if not m:
            continue
        count = int(m.group("count"))
        ops = " -> ".join(m.group("ops").split())
        yield count, ops


def load_dynamic(path):
    """Return {sequence: dict} from a --vm-profile-json file."""
    with open(path) as f:
        data = json.load(f)
    return {
        f"{b['from']} -> {b['to']}": b for b in data.get("bigrams", [])
    }


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("jbc", help="Path to .jbc file (compiled bytecode)")
    ap.add_argument("source", help="Path to .c source to compile and run")
    ap.add_argument("run_args", nargs=argparse.REMAINDER,
                    help="Extra arguments passed to the compiled program")
    ap.add_argument("--jcc", default=JCC, help="Path to jcc binary")
    ap.add_argument("--ngram-tool", default=NGRAM_TOOL,
                    help="Path to bytecode_ngrams tool")
    ap.add_argument("--top", type=int, default=25,
                    help="Show only top N rows (default 25)")
    args = ap.parse_args()

    result = subprocess.run(
        [args.ngram_tool, "-n", "2", "-t", "9999", args.jbc],
        capture_output=True, text=True, check=True,
    )
    static_text = result.stdout

    profile_path = "/tmp/_jcc_xref_profile.json"
    subprocess.call(
        [args.jcc, "--vm-profile-json", profile_path,
         "-I", "include", args.source] + args.run_args,
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
    )
    try:
        dynamic = load_dynamic(profile_path)
    finally:
        try:
            os.unlink(profile_path)
        except OSError:
            pass

    print(f"{'static':>8s}  {'dynamic':>10s}  {'score':>12s}  sequence")
    rows = []
    for s_count, s_seq in parse_static(static_text):
        d = dynamic.get(s_seq)
        if d is None:
            continue
        rows.append((s_count * d["count"], s_count, d["count"], s_seq))
    rows.sort(reverse=True)
    for score, s_count, d_count, s_seq in rows[:args.top]:
        print(f"{s_count:>8d}  {d_count:>10d}  {score:>12d}  {s_seq}")


if __name__ == "__main__":
    main()

