#!/usr/bin/env bash
# Shared host-side toolchain discovery. Explicit paths take precedence and
# fail visibly when invalid, rather than falling back to another NDK.
resolve_android_toolchain() {
  local sdk candidate tool host
  NDK="${ANDROID_NDK_HOME:-${ANDROID_NDK_ROOT:-}}"
  if [[ -z "$NDK" ]]; then
    sdk="${ANDROID_SDK_ROOT:-${ANDROID_HOME:-}}"
    if [[ -z "$sdk" ]]; then
      case "$(uname -s)" in
        Darwin) sdk="${HOME}/Library/Android/sdk" ;;
        Linux) sdk="${HOME}/Android/Sdk" ;;
      esac
    fi
    if [[ -n "$sdk" && -d "$sdk/ndk" ]]; then
      # Natural numeric order for side-by-side NDK revisions, excluding
      # unrelated directories. NDK revision folders are dot-separated digits.
      candidate="$(find "$sdk/ndk" -mindepth 1 -maxdepth 1 -type d -print |
        awk -F/ '$NF ~ /^[0-9]+\.[0-9]+\.[0-9]+$/ {print}' | sort -V | tail -n 1)"
      NDK="$candidate"
    fi
  fi
  if [[ -z "$NDK" || ! -f "$NDK/build/cmake/android.toolchain.cmake" ]]; then
    echo "Android NDK not found; set ANDROID_NDK_HOME (NDK directory) or ANDROID_SDK_ROOT (SDK directory)" >&2
    return 1
  fi
  NDK="$(cd "$NDK" && pwd)"
  case "$(uname -s):$(uname -m)" in
    Darwin:*) host=darwin-x86_64 ;;
    Linux:x86_64) host=linux-x86_64 ;;
    *) echo "Unsupported NDK host: $(uname -s) $(uname -m)" >&2; return 1 ;;
  esac
  tool="$NDK/toolchains/llvm/prebuilt/$host/bin"
  LLVM_STRIP="$tool/llvm-strip"
  LLVM_READELF="$tool/llvm-readelf"
  LLVM_NM="$tool/llvm-nm"
  [[ -x "$LLVM_STRIP" && -x "$LLVM_READELF" && -x "$LLVM_NM" ]] || {
    echo "Required LLVM tools missing under $tool" >&2; return 1;
  }
}

strip_runtime_binaries() {
  "$LLVM_STRIP" --strip-unneeded "$@"
}
