# J8 feasibility, baseline and architecture decision

Status: 2026-11-17. This is an engineering assessment, not a promise. Where a
statement is not backed by a measurement or a primary source it is labelled
**unverified**.

Question: can the `ios-sweet` approach (Apple XNU 8796 + an unmodified iOS 16
userland) be brought up on the Samsung Galaxy J8 (`SM-J810G`, `j8y18lte`,
Snapdragon 450 / msm8953), and what stands between the current state and the
requested end state (GPU acceleration, App Store, calls, Wi-Fi, Bluetooth, basic
drivers)?

Short answer: **the page-size concern raised for this SoC is not the blocker it
looks like, but the port is still a multi-month hardware bring-up, and two of the
requested outcomes (App Store, cellular calls) have dependencies outside this
project's control.** Details below.

## 1. What the reference ports actually are

| Repository | Commit read on 2026-11-17 | Device | Proven, first-hand by its authors |
|---|---|---|---|
| `joranv1/ios-sailfish` | `6f27e41ff249f2680540ebe8ee5ab982c5e641c9` (2026-10-22) | Pixel 1, MSM8996 | boots iOS 16.7.16 to SpringBoard; GPU (Adreno 530) via Mesa/freedreno + EGL shim; QCA6174 Wi-Fi; touch/buttons/battery; no BT/cellular/audio/camera |
| `dipa-studio/ios-sweet` | `45263bee51df4a0f55cdf9c649af886399488ddb` (2026-10-23) | Redmi Note 10 Pro, SM7150 | boots iOS 16.7.16 to SpringBoard, CPU-rendered; touch/buttons; no GPU/Wi-Fi/BT/cellular/audio |
| `iamsrishanth/ios-sweet` | same SHA at task start | fork of the above | no J8 work before this branch |

Both are genuine ports: real XNU patch series (183 patches in `sailfish`, 81 in
`sweet`), real device trees, real boot logs. **None of their hardware results
transfer to the J8.** They share the same *method*:

```
ABL/bootloader -> shim (own MMU) -> XNU + generated Apple Device Tree
              -> block driver -> HFS+ root -> dyld -> launchd -> backboardd
```

The Android/LineageOS work in this workspace is a separate body of evidence and
does not demonstrate any of this.

## 2. The page-size question (resolved, with caveats)

### 2.1 Hardware: Cortex-A53 r0p4 has no 16 KiB granule (documented)

- CPU: ARM part `0xd03`, revision `r0p4` = Cortex-A53. `/proc/cpuinfo` on the
  device reports exactly this; r0p4 is the final A53 revision, so the r0p4 TRM
  (DDI 0500J) is the right document.
- The Cortex-A53 TRM's `ID_AA64MMFR0_EL1` table states, field by field:
  `TGran4 [31:28] = 0x0` (4 KB supported), `TGran64 [27:24] = 0x0` (64 KB
  supported), `TGran16 [23:20] = 0x0` (**16 KB not supported**). The same TRM's
  `TCR_EL1.TG0` description lists only `0b00` 4 KB and `0b01` 64 KB; the 16 KB
  encoding is not a legal value for this core. Composed with the other documented
  fields (SNSMem 1, BigEnd 1, ASIDBits 2, PARange 2) the value is
  `0x0000_0000_0000_1122`.
- An independent Linux report for a big.LITTLE part corroborates exactly that
  composed value: the A53 cluster reads `0x00000000001122` while the A73 cluster
  reads `0x00000000101122`, differing only in `TGran16` bits [23:20].
- Correction of an earlier draft in this branch: Arm's Cortex-A53 programmer's
  guide (DEN0024A) contains a sentence that the A53 "supports all three sizes".
  That sentence is not core-revision-specific and conflicts with the r0p4 TRM.
  The TRM plus the `0x1122` observation win for this handset; the DEN0024A text
  is recorded in `docs/j8/research/arm-granules.md` so the discrepancy is not
  lost. Also corrected there: `TGran4` is [31:28], not [15:12] ([15:12] is
  `SNSMem`), and the `TGran4`/`TGran64` polarity is inverted relative to
  `TGran16`.
- The reference ports both *require* the 16 KiB granule in hardware:
  `pexpert/pexpert/arm64/SWEET.h` defines `__ARM_16K_PG__ 1` and
  `shim/mmu.c` programs `TCR_EL1.TG0 = 16K` with 16K page-table geometry. A J8
  kernel built that way would not boot.
- **Device measurement attempt (2026-11-17):** EL0 cannot read the register at
  all. Arm ARM D24.2.87 shows that at EL0 the read is UNDEFINED (or trapped by
  FEAT_IDST); `HCR_EL2.TID3` is the EL1-to-EL2 trap, not an EL0 gate. A static
  AArch64 probe (`tools/j8/j8probe.c`) ran on the phone and took SIGILL exactly
  as required. The EL0 probe therefore *confirms the access model*, not the
  value.
- **Confirmation attempt failed (2026-11-17):** the EL1 probe
  (`tools/j8/granule-probe-el1.S`) was built, flashed to `boot` (readback
  verified) and rebooted. The bootloader **rejected it** and entered `BOOT
  RECOVERY MODE` / Download Mode; the probe never executed, so there is still no
  on-unit read of the register. The image lacked the ROM boot image's appended
  DTB and ramdisk, which is the most likely reason. The phone was restored with
  Odin and the ROM `boot.img`. Details and recovery in `INSTALL-RECOVERY.md`
  section 5. The documented TRM answer is unchanged; a future confirmation needs
  a bootloader-accepted image, not just a correct probe payload.
- The installed kernel's `CONFIG_ARM64_4K_PAGES=y` is a software choice and is
  consistent with the TRM, but is not itself proof of the granule.

### 2.2 XNU decouples the hardware granule from the advertised page size

This is the important finding, and it changes the conclusion.

- `ARM_PGSHIFT` is the **hardware granule** used to build page tables:
  `osfmk/arm64/proc_reg.h:1147-1153` — 14 with `__ARM_16K_PG__`, else 12.
- `PAGE_SHIFT_CONST` is the **advertised kernel page size**, chosen at boot:
  `osfmk/arm/arm_init.c:392-410` — on arm64 with more than 1 GiB of RAM it is
  set to `PAGE_MAX_SHIFT` (14 = 16 KiB); at or below 1 GiB it is `ARM_PGSHIFT`.
  The comment names the older Apple SoCs that ran 4 KiB hardware pages.
- `osfmk/mach/arm/vm_param.h:60-84` makes `PAGE_SHIFT` alias `ARM_PGSHIFT`
  only for the `__ARM_16K_PG__` kernel-private case, otherwise
  `PAGE_SHIFT_CONST`. `PAGE_SIZE`, `PAGE_RATIO` and
  `TEST_PAGE_SIZE_16K`/`TEST_PAGE_SIZE_4K` follow from that.
- `osfmk/arm/pmap/pmap.c:221-270` keeps **both** page-table geometries
  (`pmap_pt_attr_4k`, `pmap_pt_attr_16k`) and selects `native_pt_attr` from
  `__ARM_16K_PG__`; `osfmk/arm/pmap/pmap.h:256` defines
  `PAGE_RATIO = (1 << PAGE_SHIFT) >> ARM_PGSHIFT`, i.e. how many hardware pages
  make one software page. The pmap then walks `PAGE_RATIO` PTEs per software
  page and has explicit `TEST_PAGE_RATIO_4` paths (e.g. `pmap.c:616, 5620,
  5922`).
- Boot-time TCR values exist for both granules:
  `proc_reg.h:936-951, 999-1000` (`TCR_EL1_4KB`, `TCR_EL1_16KB`, `T0SZ_BOOT`).

**Consequence for the J8 (both branches):** if this handset turns out to expose
`TGran16 = 0`, a J8 machine config that omits `__ARM_16K_PG__` gets
`ARM_PGSHIFT = 12` (4 KiB page tables) while `PAGE_SHIFT_CONST` is 14 on a 4 GiB
phone, i.e. XNU advertises **16 KiB pages to the iOS userland** and maps each
16 KiB software page with four 4 KiB hardware PTEs. The unmodified iOS 16.7.16
root filesystem, dyld shared cache and binaries (which are built for 16 KiB
pages) would see 16 KiB, as on an iPhone. If the handset does expose
`TGran16 = 1`, the existing 16 KiB `SWEET`-style configuration can be used
instead and the 4 KiB work is unnecessary. The 4 KiB-granule board config is
therefore a hedge that must be built and tested either way.

Relevant source locations:

| File | Lines | What it establishes |
|---|---|---|
| `osfmk/arm64/proc_reg.h` | 1147-1153 | `ARM_PGSHIFT` = hardware granule |
| `osfmk/mach/arm/vm_param.h` | 60-84 | `PAGE_SHIFT_CONST`, `PAGE_SHIFT`, `PAGE_MAX_SHIFT` |
| `osfmk/arm/arm_init.c` | 392-410 | 16 KiB advertised for >1 GiB RAM |
| `osfmk/arm/pmap/pmap.c` | 221-270 | 4K and 16K page-table geometries, `native_pt_attr` |
| `osfmk/arm/pmap/pmap.h` | 256 | `PAGE_RATIO` |
| `osfmk/arm/pmap/pmap.c` | 616, 5620, 5922 | explicit `PAGE_RATIO == 4` paths |
| `osfmk/arm64/proc_reg.h` | 936-951, 999-1000 | boot TCR for 4K and 16K |
| `pexpert/pexpert/arm64/BCM2837.h` | whole file | a non-Apple, 4 KiB-granule, GICv2 board config precedent |

### 2.3 What is still unproven

- No 4 KiB-granule **board config ships in the public xnu-8796 tree**. The
  only in-tree configs are 16 KiB (`H13`, `VMAPPLE`), and `BCM2837` is a Pi 3
  bring-up target with 1 GiB (so it advertises 4 KiB pages, not 16 KiB). The
  `PAGE_RATIO` path is therefore present but **not exercised by either
  reference port**, and must be validated.
- The shim is 16 KiB-only (`shim/mmu.c`: `PG_SHIFT 14`, 32 MiB L2 blocks,
  `TG0=16K`) and `shim/shim.c` checks for the 16 KiB granule. A J8 shim needs a
  4 KiB-granule table builder (2 MiB L2 blocks, `T0SZ` per
  `proc_reg.h:948`, `TG0=4K`) and must drop the 16 KiB requirement.
- `boot_args.memSize` must be set so `arm_init` chooses 16 KiB; the J8 has
  2 GiB + 1.75 GiB banks, and only the upper bank is free of firmware carve-outs
  (see `HARDWARE.md` §1.2), so the memory map and `memSize` need care.
- Whether the 16 KiB software / 4 KiB hardware path survives iOS's
  `pmap`-heavy operations (dyld shared cache mapping, JIT-less W^X, IOSurface
  mappings, GPU SMMU-backed buffers) is an open engineering risk. The fallback
  is to advertise 4 KiB pages (`PAGE_SHIFT_CONST = 12`), which would require
  the userland to tolerate 4 KiB; that is **not** assumed to work and is not the
  preferred path.

**Verdict (2026-11-17):** the Cortex-A53 r0p4 in this J8 is **documented as
having no 16 KiB granule** (DDI 0500J: `TGran16 = 0x0`; `TCR_EL1.TG0` allows only
4 KB and 64 KB; corroborated by the `0x1122` cluster value). A kernel built with
`__ARM_16K_PG__` therefore cannot run here. That is not a dead end: XNU can
advertise a 16 KiB software page size on 4 KiB hardware tables via `PAGE_RATIO`,
so the port becomes a machine-config + shim + validation task. What remains
unproven is whether that path is sound in xnu-8796.101.5 (it exists in-tree but
no board config or reference port exercises it) and whether it holds up on the
J8 in QEMU and on hardware. The EL1 probe is confirmation of the documented
answer, not the decision.

The confirmation experiment (see `tools/j8/README.md` and
`INSTALL-RECOVERY.md`). Its first attempt was rejected by the bootloader; the
table records the intended procedure and the observed failure:

| Step | Action |
|---|---|
| 1 | `tools/j8/build-granule-probe.sh work/gapps/boot-stock.img` → image + sha256 |
| 2 | verify the rollback image and hash are present on the phone/host |
| 3 | flash the probe to `boot` from TWRP, reboot (**attempted 2026-11-17: the bootloader rejected the image; no on-unit read**) |
| 4 | observe: **red band + power off** = `TGran16 = 0` (confirms the TRM); **green band + power off** = `TGran16 = 1` (would contradict the TRM and require a re-think); normal splash/hang = probe did not run |
| 5 | restore the rollback image before any other work |

## 3. Selected software baseline

| Choice | Value | Why |
|---|---|---|
| XNU | Apple `xnu-8796.101.5`, tag commit `aca3beaa3dfbd42498b42c5e5ce20a938e6554e5` | The reference patch series is written against it; it is the iOS 16 / macOS 13.5 generation and is public |
| iOS userland | iPhone X (`iPhone10,3` / D22AP) iOS 16.7.16 (20H392) root + shared cache, as used by `ios-sweet` | The destination repository's patches, `mkadt.py` identity, `mkiosroot.sh` and the 1125×2436 presentation are built around this image |
| Alternative considered | iPhone 8 (`D20AP`) image as used by `ios-sailfish` | Its GPU work is the best reference for Adreno, but its identity/panel/scale assumptions differ; the identity can be revisited when GPU work starts, without changing the kernel baseline |
| Not chosen | iOS 17/18 or macOS 14/15 XNU | No matching public reference patch series; more security dependencies; no evidence it helps on ARMv8.0 |
| Toolchain | `blacktop/darwin-xnu-build` @ `663906528d1cd5ea8fde7de6f16de73db9511549`, macOS 14 runner, Xcode 15.x, KDK 13.5 | The workflow that produced every reference artifact; pinned in `.github/workflows/xnu.yml` |
| Shim/probes | AArch64 freestanding C, `aarch64-linux-gnu-gcc` (CI/Linux) or Zig (local cross-compile) | No macOS needed for the shim |

CPU/memory fit: the iOS 16 userland is ARMv8.0-clean (the sailfish port runs it
on Kryo with no v8.2 features); the A53 is ARMv8.0 without PAC/LSE, which
matches. RAM: 2 GiB + 1.75 GiB physical; the reference ports use a single upper
bank (~3.95 GiB on an 8 GiB phone). The J8's free upper bank alone (1.75 GiB) is
smaller than an iPhone X's 3 GiB, so the J8 port must map usable memory from
both banks around the firmware carve-outs. That is a real design task, not a
formality, and is listed as a milestone gate.

## 4. Hardware deltas that dominate the schedule

Full table in `HARDWARE.md` §7. In order of cost:

1. **eMMC/SDCC storage driver (new).** The references use UFS. Without a block
   device there is no root filesystem, no userland, no logs. This is the first
   hard dependency after boot/MMU/timer.
2. **GICv2 interrupt controller (new for a phone).** Both references are GICv3.
   XNU's GICv2 support is the Raspberry Pi path; timer PPI numbering, SGI/IPI
   use and the `qcom,msm-qgic2` layout must be implemented and debugged.
3. **Memory map and boot hand-off.** Two banks, firmware carve-outs, 4 KiB
   page tables, shim rewrite, `boot_args` correctness.
4. **Display + touch + buttons (adapt/new).** MDP5/DSI panel init for
   `S6E8AA5X01`, Zinitix BT532 I²C touch, SM5708 PMIC keys. Needed before any
   GUI; can be deferred behind storage + serial/framebuffer logging.
5. **GPU (adapt).** Adreno 506 + SMMUv2 + PIL zap shader + Mesa/freedreno +
   EAGL shim. The Pixel work is the blueprint, but the SMMU instance, clocks,
   firmware subset and panel path differ; see
   `docs/j8/research/gpu-adreno506.md`.
6. **Wi-Fi (new).** WCNSS/prima over SNOC with PIL firmware, not QCA6174 PCIe.
7. **Audio, Bluetooth, cellular, sensors (new).** No reference implementation in
   either repository; each is a firmware/DSP/QMI integration project.
8. **App Store.** Not a driver problem; see §6.

## 5. Architecture and milestone plan

Dependency-ordered, with the gate that must pass before the next step:

| # | Milestone | Gate (evidence required) | Estimated effort |
|---|---|---|---|
| 0 | Workspace evidence, compatibility report, recovery assets | this document set + `check-device` output + verified backups | done in this task |
| 1 | 4 KiB-granule XNU build for a J8 board config | kernel links; QEMU boots it with 4 KiB tables and 16 KiB advertised pages | 1–2 engineer-weeks |
| 2 | J8 shim: 4 KiB MMU, FDT parse, payload load, framebuffer console | shim reaches EL1 and prints over the panel/serial; recoverable boot image | 1–2 weeks |
| 3 | CPU, GICv2, timer, PSCI, SMP | all 8 cores online; timer ticks; IPIs work; panic log readable | 2–3 weeks |
| 4 | eMMC + HFS+ root + persistent log | mount, read/write, fsck-clean after reboot | 3–5 weeks |
| 5 | Display/touch/buttons/battery, launchd → SpringBoard | home screen on the panel, touch and power key usable | 3–5 weeks |
| 6 | GPU acceleration (Mesa/freedreno + EAGL + present) | GPU command execution proven, accelerated frames, timings recorded | 4–8 weeks |
| 7 | Wi-Fi, then BT/audio/sensors | per-feature acceptance checks in `STATUS.md` | 4–10 weeks each |
| 8 | Cellular calls / SMS / data | SIM, registration, two-way voice audio | research-grade; no reference |
| 9 | App Store | see §6 | blocked pending Apple-service analysis |

These estimates assume one experienced engineer with the hardware on the bench
and are deliberately rough. Nothing beyond milestone 0 has been executed here.

## 6. The requested outcomes that are not ordinary driver work

### 6.1 App Store

The reference ports run a local "Hactivation" stub: `mobileactivationd` takes a
native code path selected by device-tree security properties, and AMFI/sandbox
are permissive. That produces a device that *looks* activated to local daemons.
It does **not** create an Apple activation record, an Apple-issued device
identity, a FairPlay identity, or APNs credentials. The App Store additionally
depends on Apple ID authentication against Apple's servers, StoreKit/appstored,
install coordination, provisioning and FairPlay. A Qualcomm phone with no SEP
and a spoofed iPhone identity cannot supply the hardware root of trust those
services expect. This is analysed in `docs/j8/research/appstore.md`; the staged
outcomes (launch / authenticate / download / install / run / update) are tracked
in `STATUS.md` and none is claimed.

No credentials, tokens or account data may be placed in this repository, in
logs, or in chat.

### 6.2 Cellular calls

The modem is a separate Hexagon running Qualcomm firmware, reached over QMI.
Android's telephony stack is not reusable by XNU. iOS's CommCenter expects a
baseband through its own transport and AT/QMI-like command set, and voice calls
need the QDSP6 audio path plus carrier IMS/VoLTE where applicable. No reference
port implements any of this. "Modem detected" would not mean "calls work".

### 6.3 GPU

Adreno 506 is a5xx-family and Mesa/freedreno supports a5xx GLES, so acceleration
is *plausible*. It is not implied that Vulkan, Metal, hardware video decode or
FairPlay work. The zap shader/PIL path on a Samsung TrustZone is the main
unknown. See the GPU research note.

## 7. Risks and how they will be tested

| Risk | Test that settles it | When |
|---|---|---|
| 4 KiB-granule/16 KiB-page XNU path is broken | build a J8 board config and boot it in QEMU `virt` with a 4 KiB-granule CPU model | milestone 1 |
| Shim cannot hand off a 4 KiB-granule mapping | shim prints page-table walk results before and after `TTBR` switch | milestone 2 |
| Firmware carve-outs / memory map incomplete | map both banks, verify no access to reserved regions, run a memory stress boot | milestone 3 |
| Samsung bootloader rejects the image | recoverable `boot` flash of a probe image with a known rollback; see `INSTALL-RECOVERY.md` | before any install |
| GPU zap shader unavailable outside Android | read back CP/GMU state after PIL load; GPU self-test | milestone 6 |
| Wi-Fi firmware path differs from prima expectations | firmware load + scan test on the WCNSS | milestone 7 |
| App Store prerequisites unmet | staged App Store test with no credentials committed | milestone 9 |

## 8. Sources

1. `https://github.com/apple-oss-distributions/xnu` tag `xnu-8796.101.5` (commit `aca3beaa3dfbd42498b42c5e5ce20a938e6554e5`) — accessed 2026-11-17.
2. `https://github.com/joranv1/ios-sailfish` commit `6f27e41ff249f2680540ebe8ee5ab982c5e641c9` — accessed 2026-11-17.
3. `https://github.com/dipa-studio/ios-sweet` commit `45263bee51df4a0f55cdf9c649af886399488ddb` — accessed 2026-11-17.
4. Arm Cortex-A53 Technical Reference Manual (ARM DDI 0500) — translation granule support; accessed 2026-11-17.
5. Arm Architecture Reference Manual for A-profile (ARM DDI 0487), `ID_AA64MMFR0_EL1` encodings — accessed 2026-11-17.
6. Linux arm64 kernel documentation and source for `CONFIG_ARM64_4K_PAGES`, `/proc/cpuinfo` feature reporting — accessed 2026-11-17.
7. Device evidence in this workspace: `repo/docs/device.md`, `repo/evidence/2026-10-30-first-boot/`, `HANDOVER-2026-11-15.md`, and the 2026-11-17 read-only device reads recorded in `WORKSPACE-EVIDENCE.md` and `HARDWARE.md`.
