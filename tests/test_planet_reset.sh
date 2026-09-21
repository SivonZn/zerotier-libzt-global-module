#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TEST_BASE="$(mktemp -d)"
trap 'rm -rf "$TEST_BASE"' EXIT

mkdir -p "$TEST_BASE/runtime" "$TEST_BASE/state"
printf 'custom planet\n' > "$TEST_BASE/planet"
printf 'legacy roots\n' > "$TEST_BASE/roots"
printf 'cached custom planet\n' > "$TEST_BASE/state/roots"
printf 'partial upload\n' > "$TEST_BASE/runtime/planet-upload-deadbeef.bin"
cp "$ROOT_DIR/tests/fixtures/planet-daemon.sh" "$TEST_BASE/mock-daemon.sh"
cp "$ROOT_DIR/tests/fixtures/planet-service.sh" "$TEST_BASE/mock-service.sh"
chmod 0755 "$TEST_BASE/mock-daemon.sh" "$TEST_BASE/mock-service.sh"
printf 'storage_path=%s/state\n' "$TEST_BASE" > "$TEST_BASE/config.ini"

before="$(ZT_GLOBAL_BASE="$TEST_BASE" sh "$ROOT_DIR/module/zt-globalctl" planet-info)"
case "$before" in *'"mode":"custom"'*) ;; *) echo "custom Planet was not detected" >&2; exit 1 ;; esac

reset="$(ZT_GLOBAL_BASE="$TEST_BASE" ZT_GLOBAL_DAEMON="$TEST_BASE/mock-daemon.sh" ZT_GLOBAL_SERVICE="$TEST_BASE/mock-service.sh" sh "$ROOT_DIR/module/zt-globalctl" planet-reset)"
[ "$reset" = '{"ok":true,"mode":"official","planetLoaded":true,"restarted":true}' ]
[ ! -e "$TEST_BASE/planet" ]
[ ! -e "$TEST_BASE/roots" ]
[ ! -e "$TEST_BASE/state/roots" ]
[ -e "$TEST_BASE/runtime/starts" ]

after="$(ZT_GLOBAL_BASE="$TEST_BASE" sh "$ROOT_DIR/module/zt-globalctl" planet-info)"
case "$after" in *'"mode":"official"'*) ;; *) echo "official Planet fallback was not reported" >&2; exit 1 ;; esac

echo 'Planet reset lifecycle passed'
