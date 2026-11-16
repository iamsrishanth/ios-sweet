# Arm translation granules on Cortex-A53 (and why the J8's value is unverified)

Status: 2026-11-17. This note is the basis for `docs/j8/FEASIBILITY.md` §2.

## 1. The encoding

`ID_AA64MMFR0_EL1` (Arm ARM DDI 0487, current edition) reports translation
granule support per stage-1 table:

| Field | Bits | `0b0000` | other values |
|---|---|---|---|
| `TGran4` | [15:12] | **supported** | `0b1111` = not supported |
| `TGran16` | [23:20] | **NOT supported** | `0b0001` supported; `0b0010` supported with 52-bit input addresses (FEAT_LPA2) |
| `TGran64` | [27:24] | **supported** | `0b1111` = not supported |
| `TGran64_2` | [31:28] | as `TGran64`, for the second stage | |

Note the asymmetry: for `TGran4`/`TGran64`, zero means *supported*; for `TGran16`,
zero means *not supported*. Code that treats all four fields the same way reads
the 16 KiB answer backwards.

The 16 KiB granule is optional in ARMv8-A. A core may implement it, and an SoC
integration may still leave it unavailable; the only authority is the register
on the actual part.

## 2. What Arm says about the Cortex-A53 specifically

The public statements conflict, which is exactly why this cannot be settled from
a datasheet:

- Arm's Cortex-A53 programmer's guide (DEN0024A) says the supported granule
  sizes are 4 KB, 16 KB and 64 KB and that it is implementation-defined which are
  supported, then states: *"The Cortex-A53 processor supports all three sizes,
  but this is not the case for early versions of some processors, such as the
  Cortex-A57, which did not support the 16K granule size."*
- An Arm engineer (Suzuki K. Poulose) replying on linux-arm-kernel about a
  DragonBoard 410C (APQ8016, Cortex-A53) that would not boot a 16 KB-page
  kernel: *"As it clearly says, it really depends on the implementation and by
  default A53 doesn't enable 16K support."* The thread's diagnosis is that the
  kernel reaches `__no_granule_support`, i.e. the hardware reports no 16 KB.

So: the core *can* implement 16 KB, and real A53 devices exist on both sides.
For the Snapdragon 450 in this phone, no public register dump or Qualcomm
statement was found. **The J8's `TGran16` is unknown until it is read.**

## 3. Why it was not read on the J8 yet

- EL0 (`adb shell`, any userspace binary) cannot read the register: the kernel
  traps ID-register reads (`HCR_EL2.TID3`). A static AArch64 probe built for this
  study (`tools/j8/j8probe.c`) runs on the phone and reports
  `EL0-ID-READ: TRAPPED (SIGILL)`. That is a device measurement, not a guess.
- Linux does not print the granule in `/proc/cpuinfo`; the 32-bit view the J8's
  userspace sees (`armv8l`) prints ARM32 HWCAP names, and the 64-bit view has no
  granule feature either. `CONFIG_ARM64_4K_PAGES=y` is the kernel's software
  choice, not a hardware capability report.
- The decisive method is an EL1 probe. `tools/j8/granule-probe-el1.S` reads
  `ID_AA64MMFR0_EL1` at EL1, paints the bootloader splash (green = 16 KB
  supported, red = not), then PSCI-resets or powers off. The image is built
  (`sha256 6b7012…0136`) and the procedure is in `INSTALL-RECOVERY.md` §5, but
  it has **not been flashed**.

## 4. Consequence for the port (decision tree)

| Probe result | Kernel configuration | Notes |
|---|---|---|
| `TGran16 = 0` | J8 machine config **without** `__ARM_16K_PG__` (`ARM_PGSHIFT = 12`), 4 KiB page tables; `PAGE_SHIFT_CONST` still 14 on a 4 GiB phone, so iOS still sees 16 KiB pages (`PAGE_RATIO = 4`) | The path exists in XNU (`pmap_pt_attr_4k`, `PAGE_RATIO`, `TEST_PAGE_RATIO_4`) but no in-tree board config exercises it; must be validated in QEMU and on hardware |
| `TGran16 = 1` | Reuse the reference 16 KiB configuration (a `SWEET`-style `__ARM_16K_PG__ 1` board config), with J8 devices | Simplest kernel-side outcome; still needs the shim, storage, display etc. |

Either way the userland can stay the unmodified iOS 16.7.16 image: the
software-visible page size is a kernel decision, not a property of the binaries.

## 5. What is still unverified

- `TGran16` on this handset (the EL1 probe is the test).
- Whether XNU's 4 KiB-granule/16 KiB-page path is sound in this release. It is
  present by design and has `PAGE_RATIO == 4` special cases, but neither
  reference port exercises it.
- Whether the iOS 16.7.16 userland makes any assumption that breaks if the
  kernel reports 4 KiB pages instead (only relevant if the fallback is taken).

## Sources

1. Arm Architecture Reference Manual for A-profile, DDI 0487 — `ID_AA64MMFR0_EL1`
   encodings — accessed 2026-11-17.
2. Arm Cortex-A53 processor documentation / DEN0024A quotation in the
   linux-arm-kernel thread below — accessed 2026-11-17.
3. Suzuki K. Poulose, "16KB PAGE_SIZE not work on DB410C", linux-arm-kernel,
   2015-12-26 — https://lore.kernel.org/all/567E7F63.80707@arm.com/ — accessed
   2026-11-17.
4. Device measurements: `/proc/cpuinfo` (CPU part `0xd03` r0p4),
   `/proc/config.gz` (`CONFIG_ARM64_4K_PAGES=y`), and the EL0 probe output in
   `tools/j8/README.md` — 2026-11-17.
5. `apple-oss-distributions/xnu` `xnu-8796.101.5`, files and lines cited in
   `FEASIBILITY.md` §2.2 — accessed 2026-11-17.
