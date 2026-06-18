FROM ubuntu:24.04 AS build

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        ca-certificates \
        clang \
        libffi-dev \
        make \
        pkg-config \
        python3 \
    && rm -rf /var/lib/apt/lists/*

ENV CC=clang
WORKDIR /workspace/cccc

COPY . .

RUN make clean && make

FROM build AS test

RUN make test

FROM build AS runtime

CMD ["make", "test"]
