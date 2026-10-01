#!/usr/bin/env bash
set -euo pipefail
export DEBIAN_FRONTEND=noninteractive
apt-get update
apt-get install -y --no-install-recommends autoconf automake bison build-essential ca-certificates cmake git libtool libtool-bin meson ninja-build patch pkg-config python3 python3-yaml ruby wget xxd zstd
rm -rf /var/lib/apt/lists/*
