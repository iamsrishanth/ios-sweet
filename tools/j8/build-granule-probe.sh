#!/bin/sh
# Build the EL1 translation-granule probe as a legacy Android boot image v0
# for the Galaxy J8.
#
# Usage:
#   tools/j8/build-granule-probe.sh REFERENCE_BOOT_IMG [OUT_IMG]
#
# REFERENCE_BOOT_IMG is an image that boots on this phone (for example the
# Havoc boot image kept for rollback). Only its v0 header geometry is copied:
# page size 2048, kernel_addr 0x80008000, ramdisk_addr 0x82000000,
# tags_addr 0x81e00000. It is NOT flashed and its contents are not modified.
#
# Requires an AArch64 assembler/linker ($CROSS_CC, zig, or
# aarch64-linux-gnu-gcc), an objcopy, and Python 3.
#
# The resulting image is a diagnostic, not an installation. Flashing it writes
# the boot partition; see docs/j8/INSTALL-RECOVERY.md and keep the rollback
# image and its hash at hand.
set -eu

here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ref=${1:?usage: build-granule-probe.sh REFERENCE_BOOT_IMG [OUT_IMG]}
out=${2:-"$here/out/granule-probe-j8.img"}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT INT TERM
mkdir -p "$(dirname -- "$out")"

# Assemble and link at the load address, then flatten to a raw binary.
if [ -n "${CROSS_CC:-}" ]; then
    cc=$CROSS_CC
elif command -v zig >/dev/null 2>&1; then
    cc="zig cc -target aarch64-freestanding"
elif command -v aarch64-linux-gnu-gcc >/dev/null 2>&1; then
    cc=aarch64-linux-gnu-gcc
else
    echo "error: need CROSS_CC, zig, or aarch64-linux-gnu-gcc" >&2
    exit 1
fi

# shellcheck disable=SC2086
$cc -nostdlib -ffreestanding "-Wl,-T,$here/probe.ld" \
    -o "$work/probe.elf" "$here/granule-probe-el1.S"

if command -v zig >/dev/null 2>&1; then
    zig objcopy -O binary "$work/probe.elf" "$work/probe.bin"
elif command -v aarch64-linux-gnu-objcopy >/dev/null 2>&1; then
    aarch64-linux-gnu-objcopy -O binary "$work/probe.elf" "$work/probe.bin"
else
    echo "error: no objcopy found (zig or aarch64-linux-gnu-objcopy)" >&2
    exit 1
fi

python3 "$here/mkbootv0.py" pack \
    --kernel "$work/probe.bin" \
    --header-from "$ref" \
    --name "j8-granule" \
    --cmdline "console=null androidboot.hardware=qcom androidboot.bootdevice=7824900.sdhci" \
    -o "$out"

python3 "$here/mkbootv0.py" parse "$out"
