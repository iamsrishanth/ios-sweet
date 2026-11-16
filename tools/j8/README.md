# J8 tools

Small, dependency-light tools for the Samsung Galaxy J8 (`SM-J810G`,
`j8y18lte`, msm8953) port. They are diagnostics and packaging helpers; none of
them installs anything by itself.

| Tool | What it does | Runs on |
|---|---|---|
| `j8probe.c` | Reads AArch64 ID registers from EL0 and reports the result | phone (static AArch64 binary) |
| `build-probe.sh` | Cross-compiles `j8probe.c` (Zig, `$CROSS_CC`, or `aarch64-linux-gnu-gcc`) | host |
| `granule-probe-el1.S` | EL1 probe: reads `ID_AA64MMFR0_EL1.TGran16`, paints the splash, powers off | phone (boot image) |
| `build-granule-probe.sh` | Assembles the EL1 probe and wraps it in a legacy boot image v0 | host |
| `mkbootv0.py` | Parses/packs legacy `ANDROID!` v0 boot images; `selftest` round-trips one | host (Python 3) |
| `j8-compat-check.sh` | Read-only model/partition/kernel compatibility gate | phone (Android or TWRP) |

## Measured result (2026-11-17, this phone)

`j8probe` runs as a 64-bit process but the ID-register read is trapped by the
kernel:

```
j8probe: AArch64 EL0 identification probe
uname: sysname=Linux machine=armv8l release=3.18.140
os page size (sysconf): 4096
AT_HWCAP=0xff AT_HWCAP2=0x0
EL0-ID-READ: TRAPPED (SIGILL) - cannot read ID registers at EL0
```

The architecture has **no EL0 access path** for this register: at EL0 the read
is UNDEFINED (or trapped by FEAT_IDST), so the SIGILL is the required outcome and
says nothing about the value. The value comes from the core's TRM
(Cortex-A53 r0p4, DDI 0500J: `TGran16 = 0x0`, **not supported**; `TCR_EL1.TG0`
allows only 4 KB and 64 KB), corroborated by the `0x1122` cluster value seen in
Linux. `granule-probe-el1.S` is the on-unit confirmation. An earlier version of
this probe decoded `TGran4` from the wrong bits and with the wrong polarity;
that is fixed in `j8probe.c`.

## Building the EL1 probe image

```sh
# A boot image that is known to boot on this phone supplies only the v0 header
# geometry (page size, load addresses). Nothing is flashed by this command.
tools/j8/build-granule-probe.sh work/gapps/boot-stock.img /tmp/granule-probe-j8.img
```

**First attempt (2026-11-17): the bootloader rejected this image.** It was
flashed, readback-verified and rebooted; the phone entered Samsung
`BOOT RECOVERY MODE` / Download Mode instead of running it. The image has no
appended DTB and no ramdisk (unlike the ROM's `Image.gz-dtb`), which is the most
likely reason. Recovery needed Odin over Download Mode because the power button
is dead; see `docs/j8/INSTALL-RECOVERY.md` section 5.1. **Do not re-flash it as
built.** A future probe needs a bootloader-accepted boot image (DTB + ramdisk)
and a staged Odin rollback.

## Safety rules

- `j8-compat-check.sh` must pass before any write operation.
- Flashing the probe writes the `boot` partition. Only do it with a verified
  rollback image, with the user present, and never as part of an automated
  sequence. See `docs/j8/INSTALL-RECOVERY.md`.
- These tools never write `modem`, `efs`, `persist`, `modemst*`, `fsg`, `fsc`,
  bootloader partitions or `recovery`.
- Do not commit device dumps, serial numbers or Apple files. `.gitignore`
  already excludes the relevant directories.
