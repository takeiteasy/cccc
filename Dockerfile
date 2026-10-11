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

# `make` builds ./cccc, the only compiler binary (see docs/BUILD.md);
# mirrors the `build` step in .builds/linux-amd64.yml.
RUN make

FROM build AS test

# Aggregate driver: source suite, native round-trip, smokes and fuzz replay in
# one run -- mirrors .builds/linux-amd64.yml's `test` step.
RUN python3 tools/run_tests.py --binary ./cccc -j 8

FROM build AS runtime

CMD ["python3", "tools/run_tests.py", "--binary", "./cccc", "-j", "8"]
