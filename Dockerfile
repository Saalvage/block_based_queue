FROM ubuntu:25.10

WORKDIR /block_multi_fifo

ENV DEBIAN_FRONTEND=noninteractive \
    LANG=C.UTF-8 \
    LC_ALL=C.UTF-8 \
    TZ=Etc/UTC

RUN apt-get update && apt-get install -y \
    g++ \
    cmake \
    ninja-build \
    texlive-latex-base \
    python3 \
    python3-numpy

COPY Makefile CMakeLists.txt CMakePresets.txt relaxed_concurrent_fifo/ scripts/ .

CMD ["bash"]
