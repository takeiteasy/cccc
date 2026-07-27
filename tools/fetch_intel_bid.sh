#!/usr/bin/env bash
# fetch_intel_bid.sh -- fetch, verify, and build the Intel(R) Decimal
# Floating-Point Math Library (BID), used as the optional runtime for
# real IEEE-754-2008 _Decimal32/64/128 arithmetic (make CCCC_HAS_DECIMAL=1).
#
# The Intel library is NEVER vendored into this repository. This script
# downloads (or reuses an already-present copy of) the upstream tarball,
# verifies its checksum, extracts only the arithmetic sources it needs
# (LIBRARY/src -- LIBRARY/float128/ is not required), builds them into a
# static archive, and leaves everything under a gitignored prefix
# directory. It is a plain script, not a `make` target: nothing else in
# this build ever invokes it automatically.
#
# Usage:
#   tools/fetch_intel_bid.sh [--prefix DIR] [--jobs N] [--verify-only] [--clean]
#
#   --prefix DIR     Where to place src/ and lib/libbid.a (default: build/intel-bid)
#   --jobs N         Parallel compile jobs (default: number of CPUs)
#   --verify-only    Only check/report the tarball's checksum; don't build
#   --clean          Remove the prefix directory and exit
#
# Tarball is located, in order:
#   1. ./IntelRDFPMathLib20U4.tar.gz (repo root)
#   2. $CCCC_BID_TARBALL, if set
#   3. downloaded via curl from the pinned upstream URL
#
# On success, prints the make invocation to enable decimal support:
#   make CCCC_HAS_DECIMAL=1 CCCC_BID_PREFIX=<prefix>

set -euo pipefail

BID_URL="https://www.netlib.org/misc/intel/IntelRDFPMathLib20U4.tar.gz"
BID_NAME="IntelRDFPMathLib20U4.tar.gz"
# SHA3-256 of the exact upstream archive (v2.4 / Update 4). Verified against
# the tarball used during development of ticket #402.
BID_SHA3_256="$(cat "$(dirname "$0")/intel_bid.sha3-256" 2>/dev/null || true)"

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PREFIX="$ROOT/build/intel-bid"
JOBS="$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)"
VERIFY_ONLY=0
DO_CLEAN=0

while [ $# -gt 0 ]; do
  case "$1" in
    --prefix) PREFIX="$2"; shift 2 ;;
    --prefix=*) PREFIX="${1#--prefix=}"; shift ;;
    --jobs) JOBS="$2"; shift 2 ;;
    --jobs=*) JOBS="${1#--jobs=}"; shift ;;
    --verify-only) VERIFY_ONLY=1; shift ;;
    --clean) DO_CLEAN=1; shift ;;
    -h|--help) sed -n '2,30p' "$0"; exit 0 ;;
    *) echo "unknown argument: $1" >&2; exit 1 ;;
  esac
done

if [ "$DO_CLEAN" -eq 1 ]; then
  echo "Removing $PREFIX"
  rm -rf "$PREFIX"
  exit 0
fi

sha3_256() {
  # Not every shasum build supports SHA3 (stock macOS's does not), so probe
  # rather than assume. Prefer it when available, else python3 hashlib
  # (present everywhere python3 is), else sha3sum (some Linux distros).
  if command -v shasum >/dev/null 2>&1 && shasum -a 3-256 "$1" >/dev/null 2>&1; then
    shasum -a 3-256 "$1" | awk '{print $1}'
  elif command -v python3 >/dev/null 2>&1; then
    python3 -c "import hashlib,sys; print(hashlib.sha3_256(open(sys.argv[1],'rb').read()).hexdigest())" "$1"
  elif command -v sha3sum >/dev/null 2>&1; then
    sha3sum -a 256 "$1" | awk '{print $1}'
  else
    echo "no SHA3-256 tool available (need shasum, python3, or sha3sum)" >&2
    exit 1
  fi
}

# --- locate the tarball -----------------------------------------------------
TARBALL=""
if [ -f "$ROOT/$BID_NAME" ]; then
  TARBALL="$ROOT/$BID_NAME"
elif [ -n "${CCCC_BID_TARBALL:-}" ] && [ -f "${CCCC_BID_TARBALL:-}" ]; then
  TARBALL="$CCCC_BID_TARBALL"
fi

if [ -z "$TARBALL" ]; then
  if command -v curl >/dev/null 2>&1; then
    echo "Downloading $BID_URL ..."
    TMP="$ROOT/$BID_NAME"
    if curl -fsSL -o "$TMP" "$BID_URL"; then
      TARBALL="$TMP"
    fi
  fi
fi

if [ -z "$TARBALL" ]; then
  echo "SKIP: Intel BID tarball not found and could not be downloaded."
  echo
  echo "Fetch it manually with:"
  echo "  curl -fsSL -o $BID_NAME $BID_URL"
  if [ -n "$BID_SHA3_256" ]; then
    echo "Expected SHA3-256: $BID_SHA3_256"
  fi
  echo "Then re-run this script."
  exit 0
fi

# --- verify checksum before extracting anything -----------------------------
ACTUAL_SHA="$(sha3_256 "$TARBALL")"
if [ -n "$BID_SHA3_256" ]; then
  if [ "$ACTUAL_SHA" != "$BID_SHA3_256" ]; then
    echo "FAIL: SHA3-256 mismatch for $TARBALL" >&2
    echo "  expected: $BID_SHA3_256" >&2
    echo "  actual:   $ACTUAL_SHA" >&2
    exit 1
  fi
  echo "OK: checksum verified ($ACTUAL_SHA)"
else
  echo "WARNING: no pinned checksum on file (tools/intel_bid.sha3-256 missing)."
  echo "  tarball SHA3-256: $ACTUAL_SHA"
  echo "  record this value in tools/intel_bid.sha3-256 to pin it."
fi

if [ "$VERIFY_ONLY" -eq 1 ]; then
  exit 0
fi

# --- extract only LIBRARY/src ------------------------------------------------
CC="${CC:-cc}"
CC_TAG="$(echo "$CC" | tr -c 'A-Za-z0-9' '_')"
SRC_DIR="$PREFIX/src"
LIB_DIR="$PREFIX/lib"
OBJ_DIR="$PREFIX/obj-$CC_TAG"
LIBBID_A="$LIB_DIR/libbid-$CC_TAG.a"
LIBBID_A_STABLE="$LIB_DIR/libbid.a"

# Only short-circuit if the stable symlink already points at *this* CC's
# archive -- otherwise switching $CC would silently keep linking the other
# compiler's stale archive until a full rebuild happened to be forced.
if [ -f "$LIBBID_A" ] && [ "$LIBBID_A" -nt "$TARBALL" ] \
   && [ "$(readlink "$LIBBID_A_STABLE" 2>/dev/null)" = "$(basename "$LIBBID_A")" ]; then
  echo "Up to date: $LIBBID_A"
  echo
  echo "make CCCC_HAS_DECIMAL=1 CCCC_BID_PREFIX=$PREFIX"
  exit 0
fi

mkdir -p "$SRC_DIR" "$OBJ_DIR" "$LIB_DIR"
echo "Extracting LIBRARY/src ..."
tar xzf "$TARBALL" -C "$SRC_DIR" --strip-components=2 LIBRARY/src

# bid_conf.h must be included before bid_functions.h by any consumer; that's
# a consumer-side contract (documented in src/internal.h), not something
# this script needs to enforce.

echo "Compiling BID library sources ($JOBS parallel jobs) ..."
BID_CFLAGS="-O2 -w -DLINUX -Defi2 -D__NO_BINARY80__ \
  -DDECIMAL_CALL_BY_REFERENCE=0 -DDECIMAL_GLOBAL_ROUNDING=0 \
  -DDECIMAL_GLOBAL_EXCEPTION_FLAGS=0 -I$SRC_DIR"

cd "$SRC_DIR"
SRC_COUNT=$(ls -1 ./*.c | wc -l | tr -d ' ')
pids=0
fail=0
# eval is required, not cosmetic: $CC may itself contain flags (e.g. the
# macos-x86_64-build target sets CC="clang -arch x86_64"), and eval is what
# lets that word-split correctly inside the compile command.
for f in ./*.c; do
  base="$(basename "$f" .c)"
  ( eval "$CC $BID_CFLAGS -c \"$f\" -o \"$OBJ_DIR/$base.o\"" ) &
  pids=$((pids + 1))
  if [ "$pids" -ge "$JOBS" ]; then
    wait || fail=1
    pids=0
  fi
done
wait || fail=1

OBJ_COUNT=$(ls -1 "$OBJ_DIR"/*.o 2>/dev/null | wc -l | tr -d ' ')
if [ "$fail" -ne 0 ] || [ "$OBJ_COUNT" -ne "$SRC_COUNT" ]; then
  echo "FAIL: compiled $OBJ_COUNT/$SRC_COUNT BID sources -- aborting archive build" >&2
  exit 1
fi

rm -f "$LIBBID_A"
ar rcs "$LIBBID_A" "$OBJ_DIR"/*.o
ln -sf "$(basename "$LIBBID_A")" "$LIBBID_A_STABLE"

echo
echo "Built: $LIBBID_A_STABLE"
echo
echo "make CCCC_HAS_DECIMAL=1 CCCC_BID_PREFIX=$PREFIX"
