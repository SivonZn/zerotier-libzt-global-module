#!/usr/bin/env bash
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TEST_ROOT="$(mktemp -d)"
trap 'rm -rf "$TEST_ROOT"' EXIT
mkdir "$TEST_ROOT/bin"
printf '#!/bin/sh\nexit 0\n' > "$TEST_ROOT/bin/chown"
chmod +x "$TEST_ROOT/bin/chown"
export PATH="$TEST_ROOT/bin:$PATH"
cp "$ROOT_DIR/tests/fixtures/planet-daemon.sh" "$TEST_ROOT/daemon"
cp "$ROOT_DIR/tests/fixtures/planet-service.sh" "$TEST_ROOT/service"
chmod +x "$TEST_ROOT/daemon" "$TEST_ROOT/service"
export ZT_GLOBAL_DAEMON="$TEST_ROOT/daemon" ZT_GLOBAL_SERVICE="$TEST_ROOT/service"
ctl() { sh "$ROOT_DIR/module/zt-globalctl" "$@"; }
setup() {
  export ZT_GLOBAL_BASE="$TEST_ROOT/$1" MOCK_PLANET_RESULT=ok
  mkdir -p "$ZT_GLOBAL_BASE/state" "$ZT_GLOBAL_BASE/runtime"
  printf 'storage_path=%s/state\n' "$ZT_GLOBAL_BASE" > "$ZT_GLOBAL_BASE/config.ini"
}
upload() {
  ctl planet-begin deadbeef 200 >/dev/null
  local encoded
  encoded="$(printf '%0200d' 1 | base64 | tr -d '\n')"
  ctl planet-chunk deadbeef "$encoded" >/dev/null
}
setup unauthorized
upload
result="$(ctl planet-commit deadbeef)"
[[ "$result" == *'"planetLoaded":true'* ]]
[[ "$(cat "$ZT_GLOBAL_BASE/runtime/mock-status")" == *'"nodeOnline":false'* ]]
[ -f "$ZT_GLOBAL_BASE/planet" ]
[ ! -e "$ZT_GLOBAL_BASE/planet-transaction" ]
[ "$(wc -l < "$ZT_GLOBAL_BASE/runtime/starts" | tr -d ' ')" = 1 ]

for failure in start-failed not-loaded stale-token; do
  setup "$failure"
  printf 'previous custom' > "$ZT_GLOBAL_BASE/planet"
  printf 'previous roots' > "$ZT_GLOBAL_BASE/roots"
  upload
  export MOCK_PLANET_RESULT="$failure"
  if result="$(ctl planet-commit deadbeef)"; then exit 1; fi
  [[ "$result" == *'"rolledBack":true'* ]]
  [ "$(cat "$ZT_GLOBAL_BASE/planet")" = 'previous custom' ]
  [ "$(cat "$ZT_GLOBAL_BASE/roots")" = 'previous roots' ]
  [ ! -e "$ZT_GLOBAL_BASE/planet-transaction" ]
done

setup official-rollback
printf 'unrelated stale backup' > "$ZT_GLOBAL_BASE/planet.previous"
upload
export MOCK_PLANET_RESULT=start-failed
if ctl planet-commit deadbeef; then exit 1; fi
[ ! -e "$ZT_GLOBAL_BASE/planet" ]
[ ! -e "$ZT_GLOBAL_BASE/roots" ]

setup reset-rollback
printf 'custom before reset' > "$ZT_GLOBAL_BASE/planet"
export MOCK_PLANET_RESULT=start-failed
if ctl planet-reset; then exit 1; fi
[ "$(cat "$ZT_GLOBAL_BASE/planet")" = 'custom before reset' ]

setup recovery
printf 'recover me' > "$ZT_GLOBAL_BASE/planet"
upload
export MOCK_PLANET_RESULT=both-failed
if result="$(ctl planet-commit deadbeef)"; then exit 1; fi
[[ "$result" == *'"rolledBack":false'* ]]
[ -e "$ZT_GLOBAL_BASE/planet-transaction/rollback" ]
export MOCK_PLANET_RESULT=ok
if result="$(ctl restart)"; then exit 1; fi
[[ "$result" == *'"rolledBack":true'* ]]
[ "$(cat "$ZT_GLOBAL_BASE/planet")" = 'recover me' ]
[ ! -e "$ZT_GLOBAL_BASE/planet-transaction" ]

setup invalid-path
printf 'storage_path=%s/../unauthorized/state\n' "$ZT_GLOBAL_BASE" > "$ZT_GLOBAL_BASE/config.ini"
if result="$(ctl planet-reset)"; then exit 1; fi
[[ "$result" == *invalid_storage_path* ]]
[ ! -e "$ZT_GLOBAL_BASE/runtime/starts" ]
echo 'Planet activation, offline acceptance, rollback and recovery tests passed'
