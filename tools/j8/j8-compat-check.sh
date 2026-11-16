#!/system/bin/sh
# j8-compat-check - read-only compatibility gate for the Galaxy J8 XNU port.
#
# Run it on the phone (Android shell or TWRP):
#   adb push tools/j8/j8-compat-check.sh /data/local/tmp/
#   adb shell sh /data/local/tmp/j8-compat-check.sh
#
# It changes nothing. Exit status 0 means "this is the supported model and the
# expected partitions exist", NOT "the port has been validated on it".
# Installation tooling must run this (or its checks) before writing anything.

fail=0
warn=0
ok()   { echo "PASS  $*"; }
bad()  { echo "FAIL  $*"; fail=1; }
maybe(){ echo "WARN  $*"; warn=1; }

prop() { getprop "$1" 2>/dev/null; }

echo "== identity =="
model=$(prop ro.product.model)
device=$(prop ro.product.device)
[ "$model" = "SM-J810G" ] && ok "model $model" || bad "model '$model' != SM-J810G"
[ "$device" = "j8y18lte" ] && ok "device $device" || bad "device '$device' != j8y18lte"
plat=$(prop ro.board.platform)
[ "$plat" = "msm8953" ] && ok "platform $plat" || bad "platform '$plat' != msm8953"
bt=$(prop ro.boot.bootloader)
echo "INFO  bootloader ${bt:-unknown}"
echo "INFO  build ${model:-?} / $(prop ro.build.display.id)"

echo "== storage topology =="
bootdev=$(prop ro.boot.bootdevice)
[ "$bootdev" = "7824900.sdhci" ] && ok "boot device $bootdev" \
    || bad "boot device '$bootdev' != 7824900.sdhci (UFS? wrong phone?)"
[ "$(prop ro.boot.emmc)" = "true" ] && ok "emmc flag true" || bad "ro.boot.emmc != true"
if [ -b /dev/block/mmcblk0 ]; then
    ok "/dev/block/mmcblk0 exists"
    sectors=$(cat /sys/class/block/mmcblk0/size 2>/dev/null)
    if [ -n "$sectors" ]; then
        kib=$((sectors / 2))
        [ "$kib" -gt 55000000 ] && [ "$kib" -lt 65000000 ] \
            && ok "mmcblk0 size ${kib} KiB (~64 GB)" \
            || bad "mmcblk0 size ${kib} KiB, expected ~61,000,000 KiB"
    else
        maybe "cannot read mmcblk0 size"
    fi
else
    bad "/dev/block/mmcblk0 missing"
fi

echo "== partitions =="
BYNAME=/dev/block/by-name
if [ -d "$BYNAME" ]; then
    ok "by-name directory present"
else
    bad "by-name directory missing (wrong kernel/recovery?)"
fi
expect_kib() {
    name=$1; want=$2
    p="$BYNAME/$name"
    [ -e "$p" ] || { bad "partition $name missing"; return; }
    sz=$(cat "/sys/class/block/$(readlink -f "$p" | sed 's#.*/##')/size" 2>/dev/null)
    [ -n "$sz" ] || { maybe "cannot size $name"; return; }
    kib=$((sz / 2))
    [ "$kib" -eq "$want" ] && ok "$name ${kib} KiB" \
        || bad "$name ${kib} KiB, expected ${want} KiB"
}
expect_kib boot 32768
expect_kib recovery 32768
expect_kib persist 32768
expect_kib modem 98304
[ -e "$BYNAME/userdata" ] && ok "userdata present" || bad "userdata missing"

echo "== cpu / kernel =="
if grep -q 'CPU part.*0xd03' /proc/cpuinfo 2>/dev/null; then
    ok "Cortex-A53 (part 0xd03)"
else
    bad "CPU is not 0xd03 - not the expected SoC"
fi
ver=$(cat /proc/version 2>/dev/null)
echo "INFO  kernel $ver"
if [ -r /proc/config.gz ]; then
    cfg=$(zcat /proc/config.gz 2>/dev/null)
    echo "$cfg" | grep -q '^CONFIG_ARM64=y' && ok "CONFIG_ARM64=y" || bad "kernel is not arm64"
    echo "$cfg" | grep -q '^CONFIG_ARM64_4K_PAGES=y' && ok "CONFIG_ARM64_4K_PAGES=y" \
        || maybe "4K page config not found"
    echo "$cfg" | grep -q '^CONFIG_ARM64_16K_PAGES=y' && bad "kernel built for 16K pages?" || true
else
    maybe "/proc/config.gz not readable; kernel config not checked"
fi
echo "INFO  translation granule is not readable from EL0 (HCR_EL2.TID3 trap);"
echo "INFO  Cortex-A53 has no 16K granule - see docs/j8/FEASIBILITY.md"

echo "== environment =="
if [ -x /sbin/twrp ]; then
    echo "INFO  running in TWRP ($(prop ro.twrp.version))"
else
    echo "INFO  running in Android ($(prop ro.build.version.release))"
fi

echo
if [ "$fail" -ne 0 ]; then
    echo "RESULT FAIL - do not flash anything on this device from this project"
    exit 1
fi
echo "RESULT PASS (model/partition compatibility only; no installation is validated)"
[ "$warn" -ne 0 ] && echo "NOTE: warnings above were not fatal"
exit 0
