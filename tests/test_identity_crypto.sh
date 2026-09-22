#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
bash "$ROOT/tools/prepare-libzt.sh"
cmake -S "$ROOT/build/libzt-source" -B "$ROOT/build/identity-host-libzt" \
  -DBUILD_HOST_SELFTEST=OFF -DCMAKE_BUILD_TYPE=Release -DCMAKE_POLICY_VERSION_MINIMUM=3.5
cmake --build "$ROOT/build/identity-host-libzt" --target zt-shared --parallel 8
mkdir -p "$ROOT/build/tests"
WORK="$(mktemp -d "$ROOT/build/tests/identity-crypto.XXXXXX")"
c++ -std=c++17 -Wall -Wextra -Werror -I "$ROOT/src" -I "$ROOT/build/libzt-source/include" \
  "$ROOT/tests/test_identity_crypto.cpp" -L "$ROOT/build/identity-host-libzt/lib" \
  -Wl,-rpath,"$ROOT/build/identity-host-libzt/lib" -lzt -o "$WORK/test"
"$WORK/test" "$WORK"
