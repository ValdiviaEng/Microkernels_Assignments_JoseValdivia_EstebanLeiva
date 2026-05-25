#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"

mkdir -p "$ROOT/logs"

echo "[run] launching QEMU for 20 seconds"
echo "[run] stdin is redirected from /dev/null so pasted shell commands are not sent to QEMU"
echo "[run] timeout termination is expected if the guest keeps running"

timeout 20s make -C "$ROOT/mkc" r </dev/null 2>&1 | tee "$ROOT/logs/latest_run.log" || true

echo "[run] finished or timeout reached"
