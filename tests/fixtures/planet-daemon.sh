#!/bin/sh
case "$1" in
  --with-lock) shift 2; exec "$@" ;;
  --validate-planet) ! grep -q invalid "$2" ;;
  --control)
    case "$2" in
      stop) rm -f "$ZT_GLOBAL_BASE/runtime/mock-status" ;;
      status) cat "$ZT_GLOBAL_BASE/runtime/mock-status" 2>/dev/null ;;
    esac ;;
esac
