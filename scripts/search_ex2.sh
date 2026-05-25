#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"

grep -RIn --exclude='*:Zone.Identifier*' \
  -E "Ec::current|root_invoke|make_current|syscall|sysenter|sysexit|yield|create|block|unblock|ready|priority|prio|schedule|dump|ret_user" \
  "$ROOT/mkc/kern/include" "$ROOT/mkc/kern/src" "$ROOT/mkc/user/src" || true
