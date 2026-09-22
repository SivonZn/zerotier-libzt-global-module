#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
mkdir -p "$ROOT/build/tests"
WORK="$(mktemp -d "$ROOT/build/tests/identity.XXXXXX")"
c++ -std=c++17 -Wall -Wextra -Werror -I "$ROOT/src" \
  "$ROOT/tests/test_identity_store.cpp" -o "$WORK/test"
"$WORK/test" "$WORK"
