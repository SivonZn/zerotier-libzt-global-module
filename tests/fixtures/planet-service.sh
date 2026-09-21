#!/bin/sh
printf '%s\n' "$ZT_GLOBAL_ACTIVATION_ID" >> "$ZT_GLOBAL_BASE/runtime/starts"
case "$ZT_GLOBAL_ACTIVATION_ID" in
  *-candidate)
    case "${MOCK_PLANET_RESULT:-ok}" in
      start-failed|both-failed) exit 1 ;;
      not-loaded) loaded=false ;;
      stale-token) ZT_GLOBAL_ACTIVATION_ID=old-process ;;
    esac ;;
  *-rollback) [ "${MOCK_PLANET_RESULT:-ok}" != both-failed ] || exit 1 ;;
esac
# Offline and unauthorized, but local Planet initialization has succeeded.
printf '{"running":true,"planetLoaded":%s,"activationId":"%s","nodeOnline":false,"dataPlaneReady":false}\n' \
  "${loaded:-true}" "$ZT_GLOBAL_ACTIVATION_ID" > "$ZT_GLOBAL_BASE/runtime/mock-status"
