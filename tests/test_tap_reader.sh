#!/usr/bin/env bash
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
mkdir -p "$ROOT_DIR/build/tests"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pthread -I "$ROOT_DIR/src" \
  "$ROOT_DIR/tests/test_tap_reader.cpp" -o "$ROOT_DIR/build/tests/tap-reader"
"$ROOT_DIR/build/tests/tap-reader"
