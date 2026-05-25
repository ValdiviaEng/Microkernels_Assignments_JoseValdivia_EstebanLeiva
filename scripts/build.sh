#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"

echo "[build] cleaning generated files"
make -C "$ROOT/mkc" cl || true

echo "[build] compiling kernel and user payload"
make -C "$ROOT/mkc" c

echo "[build] generated outputs"
ls -lh "$ROOT/mkc/kern/build/hypervisor"
ls -lh "$ROOT/mkc/user/build/user.nova"

echo "[build] success"
