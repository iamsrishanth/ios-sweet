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

The granule value therefore remains **unverified on this unit**. Cortex-A53
granule support is implementation-defined: Arm's programmer's guide says the
core supports all three sizes, while an Arm engineer notes that integrations may
leave 16K disabled and a DragonBoard 410C (also A53) fails to boot a 16K kernel.
`granule-probe-el1.S` is the experiment that settles it.

## Building the EL1 probe image

```sh
# A boot image that is known to boot on this phone supplies only the v0 header
# geometry (page size, load addresses). Nothing is flashed by this command.
tools/j8/build-granule-probe.sh work/gapps/boot-stock.img /tmp/granule-probe-j8.img
```

Expected on a Cortex-A53 integration that disables 16K: the splash band turns
**red**, and after about ten seconds the phone powers off (PSCI `SYSTEM_OFF`).
A **green** band means the 16 KiB granule is present. The probe powers off in
both cases so it cannot reboot-loop on this dead-power-button unit; re-enter
TWRP with Volume Up + USB and restore the boot image either way.

## Safety rules

- `j8-compat-check.sh` must pass before any write operation.
- Flashing the probe writes the `boot` partition. Only do it with a verified
  rollback image, with the user present, and never as part of an automated
  sequence. See `docs/j8/INSTALL-RECOVERY.md`.
- These tools never write `modem`, `efs`, `persist`, `modemst*`, `fsg`, `fsc`,
  bootloader partitions or `recovery`.
- Do not commit device dumps, serial numbers or Apple files. `.gitignore`
  already excludes the relevant directories.
