#!/bin/sh
# Build the EL0 identification probe as a static AArch64 Linux binary.
#
# It is meant to be pushed to /data/local/tmp and executed over adb; it needs
# no root. On the J8 the ID-register read is expected to trap (SIGILL) and the
# probe reports that, which is itself the result.
#
# Usage: tools/j8/build-probe.sh [OUT]
#   Toolchain: $CROSS_CC, or `zig cc`, or aarch64-linux-gnu-gcc, in that order.
set -eu

here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
out=${1:-"$here/out/j8probe"}
mkdir -p "$(dirname -- "$out")"

if [ -n "${CROSS_CC:-}" ]; then
    cc=$CROSS_CC
elif command -v zig >/dev/null 2>&1; then
    cc="zig cc -target aarch64-linux-musl"
elif command -v aarch64-linux-gnu-gcc >/dev/null 2>&1; then
    cc=aarch64-linux-gnu-gcc
else
    echo "error: need CROSS_CC, zig, or aarch64-linux-gnu-gcc" >&2
    exit 1
fi

# shellcheck disable=SC2086
$cc -O2 -static -o "$out" "$here/j8probe.c"
echo "built $out"
sha256sum "$out" 2>/dev/null || true
