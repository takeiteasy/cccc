#!/usr/bin/env python3
"""Audit include/cccc/reflection.h's TypeKind/NodeKind/AttrTargetKind enums
against the compiler's internal TypeKind/NodeKind/AttrTargetKind enums in
src/cccc.h.

reflection.h hand-copies these three enums for comptime macro code to use
(GetTypeKind()/TK_*, NK_*, AttrTargetKind/ATTR_TARGET_*) with numeric values
that must exactly match the internal enums -- reflection.h's copies are
hand-typed integer literals, not derived from the internal definitions, and
nothing checks them against each other. TypeKind (TK_*) and NodeKind (NK_*)
use a renamed prefix (TY_*/ND_* internally) and NodeKind is a deliberate
*subset* (reflection.h doesn't expose every internal node kind); AttrTargetKind
uses the same name and is meant to be an exact, complete copy.

This is the same class of hand-duplication-with-no-static-check that #859
closed for the ~157 comptime builtin FFI signatures. Full generation isn't
a good fit here (NodeKind's exposed subset is a deliberate editorial
choice, not "all of them"), so this follows audit_ffi.py's model instead:
a regex-based cross-check that fails the build on any mismatch, filed as
#860's follow-up to #859.

Checks, for every entry reflection.h defines:
  - TypeKind:      TK_X == internal TY_X
  - NodeKind:      NK_X == internal ND_X   (subset -- extra internal ND_*
                    values not exposed in reflection.h are not an error)
  - AttrTargetKind: ATTR_TARGET_X == internal ATTR_TARGET_X (name and value
                    must match exactly; this one's meant to be a complete copy)
A reflection.h entry whose corresponding internal name doesn't exist at all
(renamed/removed) is also reported.

Exit code is nonzero iff any mismatch or missing internal counterpart is found.
"""
import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
REFLECTION_H = REPO_ROOT / "include" / "cccc" / "reflection.h"
CCCC_H = REPO_ROOT / "src" / "cccc.h"

# (reflection.h enum tag, reflection.h prefix, internal enum tag, internal prefix)
ENUM_SPECS = [
    ("TypeKind", "TK_", "TypeKind", "TY_"),
    ("NodeKind", "NK_", "NodeKind", "ND_"),
    ("AttrTargetKind", "ATTR_TARGET_", "AttrTargetKind", "ATTR_TARGET_"),
]

ANY_ENUM_RE = re.compile(r'typedef\s+enum\s*\{(.*?)\}\s*(\w+)\s*;', re.S)
ENTRY_RE = re.compile(r'([A-Za-z_]\w*)\s*(?:=\s*([^,]+?))?\s*(?:,|$)')


def strip_comments(text):
    text = re.sub(r'/\*.*?\*/', '', text, flags=re.S)
    text = re.sub(r'//[^\n]*', '', text)
    return text


def parse_enum_block(text, tag_name):
    # Scan every `typedef enum { ... } Name;` block (each non-greedily bounded
    # by its own nearest closing brace) and pick the one whose trailing name
    # matches -- anchoring the search on the enum keyword and searching
    # forward for *any* tag name, rather than searching directly for this
    # tag's name, avoids the non-greedy body swallowing every earlier
    # unrelated enum in the file when this tag isn't the first one present.
    body = None
    for m in ANY_ENUM_RE.finditer(text):
        if m.group(2) == tag_name:
            body = m.group(1)
            break
    if body is None:
        return None
    entries = {}
    value = 0
    for entry_m in ENTRY_RE.finditer(body):
        name, raw_val = entry_m.group(1), entry_m.group(2)
        if not name:
            continue
        if raw_val is not None:
            raw_val = raw_val.strip()
            try:
                value = int(raw_val, 0)
            except ValueError:
                # Non-literal initializer (e.g. an expression) -- can't
                # statically evaluate; skip this and anything after it in
                # this enum rather than guessing wrong.
                break
        entries[name] = value
        value += 1
    return entries


def audit():
    reflection_text = strip_comments(REFLECTION_H.read_text())
    cccc_text = strip_comments(CCCC_H.read_text())

    problems = []
    missing = []

    for refl_tag, refl_prefix, internal_tag, internal_prefix in ENUM_SPECS:
        refl_entries = parse_enum_block(reflection_text, refl_tag)
        internal_entries = parse_enum_block(cccc_text, internal_tag)
        if refl_entries is None:
            problems.append((refl_tag, None, f"could not find 'typedef enum {{...}} {refl_tag};' in reflection.h"))
            continue
        if internal_entries is None:
            problems.append((refl_tag, None, f"could not find 'typedef enum {{...}} {internal_tag};' in cccc.h"))
            continue

        for name, refl_val in refl_entries.items():
            if not name.startswith(refl_prefix):
                continue
            suffix = name[len(refl_prefix):]
            internal_name = internal_prefix + suffix
            if internal_name not in internal_entries:
                missing.append((refl_tag, name, internal_name))
                continue
            internal_val = internal_entries[internal_name]
            if refl_val != internal_val:
                problems.append((refl_tag, name, f"reflection.h value={refl_val}, "
                                                  f"internal {internal_name} value={internal_val}"))

    return problems, missing


def main():
    problems, missing = audit()
    if not problems and not missing:
        print("audit_reflection_enums: no mismatches found")
        return 0

    for tag, name, msg in problems:
        if name is None:
            print(f"{tag}: {msg}")
        else:
            print(f"{tag}::{name}: {msg}")

    for tag, name, internal_name in missing:
        print(f"{tag}::{name}: no matching internal '{internal_name}' found in cccc.h "
              f"(renamed/removed?)")

    print(f"\naudit_reflection_enums: {len(problems)} mismatch(es), "
          f"{len(missing)} missing internal counterpart(s)")
    return 1


if __name__ == "__main__":
    sys.exit(main())
