# J8 port status

Updated 2026-11-17. This is the honest scoreboard. The requested end state
(GPU acceleration, App Store, calls, Wi-Fi, Bluetooth and the basic drivers
working on the J8 under iOS/XNU) is **not achieved**, and most of it is not yet
implemented. What exists is a verified device map, a corrected feasibility
analysis, runnable diagnostics, and a recovery procedure.

## 1. Status vocabulary

| Status | Meaning |
|---|---|
| **hardware-verified** | demonstrated on this J8 by a reproducible test recorded here |
| **partial** | works with known limitations, measured |
| **implemented-untested** | code/tooling exists and was built or dry-run, but not exercised on the phone |
| **researched** | design and dependencies understood from sources; no J8 implementation |
| **unknown** | no reliable evidence either way |
| **blocked** | requires something outside the project's control, or a prerequisite that does not exist yet |

Android results are labelled *historical (Android)* and never count as iOS/XNU
evidence.

## 2. Summary

| Item | State | Evidence |
|---|---|---|
| Exact device identification | **hardware-verified** | `WORKSPACE-EVIDENCE.md` section 3, `HARDWARE.md` |
| Partition map / storage type (eMMC, not UFS) | **hardware-verified** | live `/proc/partitions`, `/dev/block/by-name`, device tree |
| Recovery path and rollback assets | **hardware-verified** *(historical Android use)* + documented | `INSTALL-RECOVERY.md` |
| Page-size capability (`TGran16`) | **documented: no 16K** (unit confirmation failed) | Cortex-A53 r0p4 TRM `TGran16 = 0x0`; EL0 access is architecturally absent (SIGILL measured); the EL1 probe image was built and flashed but the **bootloader rejected it**, so no on-unit read exists |
| XNU kernel built for J8 | **build-verified, not bootable** | CI run 44028918918 (commit `105df05`): `kernel.development.j8`, 16.5 MB, SHA-256 `38adfd548d5c6f1865aa778f3e5d554e86b682a1a1fb37877566bbac12de6dc0`, plain ARM64 Mach-O; patches 0082-0086 |
| J8 shim (4 KiB granule MMU) | **researched** | `shim/mmu.c` is 16 KiB-only |
| Anything booting on the J8 from this project | **none** | nothing flashed |

## 3. Feature matrix

| Feature | Status | What exists | What is missing | Next action |
|---|---|---|---|---|
| Boot hand-off (ABL to payload) | researched | v0 header geometry verified; reference shim exists | J8 shim (4K tables, no 16K requirement), payload format test | build shim, QEMU smoke test, then recoverable boot test |
| CPU bring-up / SMP / PSCI | researched | SM7150 PSCI patches as reference | 8x A53, 2 clusters, MPIDR handling | J8 board config |
| Interrupts (GICv2) | researched | XNU GICv2 path exists (BCM2837) | `qcom,msm-qgic2` bases, PPI numbering, SGI/IPI | GICv2 driver patch |
| Timers | researched | arch timer code in references | PPI 19 validation | with GICv2 |
| Memory map | researched | both DDR banks and carve-outs measured | usable-memory policy, boot-args `memSize` | memory-map patch |
| Storage (eMMC/SDCC) | **blocked** (no driver) | reference UFS driver only | SDCC host driver, RPM clock votes, block device | new driver - the critical path |
| Root filesystem (HFS+) | researched | HFS+ patches exist in the reference | a block device to sit on | after storage |
| Display (MDP5/DSI panel) | researched | reference framebuffer/IOMFB services | `S6E8AA5X01` panel init, MDP5, DSI command mode | after storage/logging |
| Touch (Zinitix BT532) | researched | Goodix/RMI4 references | I2C driver, GPIO IRQ, regulator, HID mapping | after display |
| Buttons (power/volume) | researched | SPMI key driver reference | SM5708/PM8953 key handling | after display |
| Battery / charging | researched | PM6150/PMI8994 references | SM5708 fuel gauge + charger driver | later |
| **GPU acceleration** | **blocked / researched** | Pixel Adreno 530 driver + Mesa/freedreno EAGL shim as reference | Adreno 506 bring-up (1-SP, 136 KiB GMEM), SMMUv2 instance, `a506_zap` via PIL (presence unverified), Mesa build for a506, present path | confirm `a506_zap` in `firmware_mnt`; port GPU driver; GPU self-test |
| **Wi-Fi** | **blocked / researched** | qcacld/prima confirmed on the device; reference QCA6174 PCIe driver is *not* reusable | WCNSS (Riva) PIL firmware load, SNOC/CE, HTC/WMI, scan/join, WPA2, Apple80211 face | new driver |
| **Bluetooth** | **blocked / researched** | `vendor.qcom.bluetooth.soc=pronto`, MAC in `/efs/bluetooth/bt_addr` | transport (UART/HS) setup, firmware/NVM, HCI, iOS Bluetooth stack integration | research the transport first |
| **Cellular calls** | **blocked** | modem/QMI/IPA paths mapped; **no SIM installed** (`gsm.sim.state=ABSENT`) | QMI/GLINK transport, SIM/EFS, telephony integration, voice audio, carrier IMS | none until a SIM is fitted and a telephony design exists |
| SMS / mobile data | **blocked** | as above | as above | as above |
| **Audio** | **blocked / researched** | QDSP6 + TFA9896 identified | ADSP firmware, AFE/DAI, codec/amp, iOS audio HAL integration | new driver/DSP work |
| **App Store** | **blocked** (by design) | analysis in `research/appstore.md` | Apple activation/identity, Apple ID auth, FairPlay/DRM, APNs, receipts | none responsible; see section 6 |
| USB (device mode, logs) | researched | working polled DWC3 driver in the Pixel port | QUSB2 PHY/clock differences | later |
| Suspend/resume, power | researched | - | power management driver | later |
| Cameras / GPS / sensors / fingerprint / SD / NFC | unknown | partial DT evidence (SDCC, SSC) | drivers, firmware, integration | out of scope for first light |
| Security model (AMFI/sandbox/AKS) | **researched; inherited stubs** | reference disables AMFI, permissive sandbox, stub AKS | nothing verified on J8 | do not treat as secure |

## 4. Test log (what was actually run)

| Date | Test | Result | Evidence |
|---|---|---|---|
| 2026-10-30 | LineageOS 17.1 (Android 10) first boot on this J8 | boots; panel/touch/charger/GPU work under Android | `repo/evidence/2026-10-30-first-boot/` (historical Android) |
| 2026-11-15 | Havoc-OS 4.19 (Android 11) install, TWRP, GApps | boots; recovery procedures established | `HANDOVER-2026-11-15.md` (historical Android) |
| 2026-11-17 | Read-only device inventory (props, config, partitions, device tree, firmware listing) | completed | `HARDWARE.md`, `WORKSPACE-EVIDENCE.md` |
| 2026-11-17 | EL0 ID-register probe (`tools/j8/j8probe.c`) | AArch64 EL0 runs; `MRS ID_AA64MMFR0_EL1` traps with SIGILL | `tools/j8/README.md` |
| 2026-11-17 | Compatibility gate (`tools/j8/j8-compat-check.sh`) | `RESULT PASS` on model/partitions/kernel | tool output |
| 2026-11-17 | Boot image round-trip (`mkbootv0.py selftest`) | byte-identical | tool output |
| 2026-11-17 | EL1 granule probe image build | image built, sha256 `73fffc...b24a` | `INSTALL-RECOVERY.md` section 5 |
| 2026-11-17 01:22 | EL1 probe flashed to `boot`, readback verified, rebooted | **bootloader rejected it** (`BOOT RECOVERY MODE`/Download Mode); probe did not run | `INSTALL-RECOVERY.md` section 5.1 |
| 2026-11-17 ~01:50 | Recovery: Odin 3.13.1 AP tar with the ROM `boot.img` | **PASS**, Android booted; root lost (boot had been Magisk-patched) | `INSTALL-RECOVERY.md` section 5.2 |
| 2026-11-17 05:43-06:19 UTC | J8 kernel build in GitHub CI (runs 43955617494 -> 43964523225 -> 44026100030 -> **44028918918**) | **success**: `kernel.development.j8` compiled and linked; earlier runs exposed the 4 KiB pmap assertion, HFS staging, component flags and two link stubs, all fixed | `BUILD.md` section 4 |
| - | Any J8 boot of XNU/iOS | not attempted: no GICv2 delivery, shim, device tree or drivers | - |

## 5. What this task implemented

Documentation and tooling, not a port:

- `docs/j8/`: this status, feasibility, hardware, workspace-evidence, build and
  install/recovery documents, plus six research notes under `research/`.
- `tools/j8/`: EL0 probe (source + build script), EL1 granule probe
  (assembly + boot-image builder), legacy boot image parser/packer with a
  round-trip self-test, and a read-only device compatibility gate. All of these
  were built/run except the EL1 probe, which needs a flash.
- XNU patches 0082-0086 and CI changes: the J8 4 KiB machine config and build
  target; the Xcode 16 `IORPC.kernelContent` backport; the iOS user-VA limit
  that the 4 KiB commpage requires; staging of the ios-sweet sources and their
  per-object flags for J8; and two link stubs. CI builds and links
  `kernel.development.j8` (run 44028918918).
- Still no shim change, no device tree, no bootable kernel and no iOS image.
  The one flashed diagnostic image (the EL1 granule probe) was **rejected by
  the bootloader**; the phone was restored with Odin. That is not a port
  installation.

## 6. App Store: separate milestone, currently blocked

Tracked separately from the drivers, and none of these is claimed:

| Staged outcome | State | Why |
|---|---|---|
| App Store app launches | unknown/plausible | it is an ordinary iOS app; no Apple dependency until it reaches the network |
| Apple ID authentication succeeds | **blocked** | needs Apple-issued device identity/attestation material and anisette provisioning; the port's local activation stub does not create them |
| Free app downloads | **blocked** | the licence is issued server-side to an authenticated account; the payload is personalised |
| Installation completes | **blocked** | install coordination/containers are stubbed permissively; no trust cache |
| App runs | **blocked** | FairPlay/DRM root of trust absent (no SEP) |
| Updates and persistence | **blocked** | authenticated update path and receipt state absent |

No credentials, tokens or account data have been used or stored. Any future
authentication would have to happen through Apple's own trusted interface, on a
device Apple recognises, and is out of scope for this port.

## 7. Next concrete steps (dependency order)

1. **Do not re-flash the EL1 probe as built.** The first attempt was rejected by
   the bootloader (no appended DTB/ramdisk; see `INSTALL-RECOVERY.md` section
   5.1). Either build a bootloader-acceptable image (DTB + ramdisk, and a staged
   Odin rollback) or accept the TRM as the answer for `TGran16` and spend the
   effort on the kernel instead.
2. **Add a J8 machine config to XNU** (4K-granule variant, GICv2 bases, 8 CPUs,
   2 clusters, timer PPI 19) and compile it in CI; boot it in QEMU `virt` with
   4 KiB page tables and a 16 KiB advertised page size.
3. **Port the shim to 4 KiB granule** and print the first EL1 output.
4. **Write the SDCC/eMMC driver** and get a persistent log + HFS+ root.
5. Display/touch/keys, then GPU, then Wi-Fi, then the rest.

Every one of those steps has a gate recorded in `FEASIBILITY.md` section 5 and a
test in section 7 of the same document. None is started.
