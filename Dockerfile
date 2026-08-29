FROM ubuntu:24.04 AS build

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        ca-certificates \
        clang \
        libclang-rt-dev \
        llvm \
        file \
        libffi-dev \
        libgdbm-compat-dev \
        make \
        pkg-config \
        python3 \
    && rm -rf /var/lib/apt/lists/*

ENV CC=clang
WORKDIR /workspace/cccc

COPY . .

# Stage0 invariant (#842): a plain `make` with only a system cc and libffi
# produces a bootstrap compiler linked against the committed src/std_seed.c
# (a fresh clone has no src/std.c yet -- it's a gitignored build artifact).
# `make bootstrap` (#857) finishes the dance: regenerate the real src/std.c,
# then unconditionally relink against it. From here, `./cccc --build build.c`
# is the real build system (see build.c's own header and man/BUILD.md) --
# mirrors the `bootstrap` + `build` steps in .builds/linux-amd64.yml.
RUN make bootstrap && ./cccc --build build.c

FROM build AS test

# Aggregate driver: source suite, .c4 bytecode round-trip, audit_ffi, and
# sqlite_smoke (skips cleanly -- the amalgamation zip isn't committed) all in
# one run, against the build.c-produced binary (not the stage0 bootstrap one)
# -- mirrors .builds/linux-amd64.yml's `test` step.
RUN python3 tools/run_tests.py --binary build/cccc -j 8

FROM build AS runtime

CMD ["python3", "tools/run_tests.py", "--binary", "build/cccc", "-j", "8"]
