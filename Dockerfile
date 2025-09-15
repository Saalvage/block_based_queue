FROM ubuntu:22.04 AS final

WORKDIR /block_multi_fifo

RUN apt-get update && apt-get install -y \
    g++ \
    cmake \
    ninja-build \
    texlive-latex-base \
    python3 \
    python3-numpy

COPY . .

CMD ["python3", "./scripts/master.py"]
