FROM debian:bookworm-slim

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        g++ \
        make \
        libboost-system-dev \
        libssl-dev \
        nlohmann-json3-dev \
        ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY main.cpp .
RUN g++ -std=c++17 main.cpp -o interscity -lboost_system -lssl -lcrypto -pthread

EXPOSE 5000

CMD ["./interscity"]
