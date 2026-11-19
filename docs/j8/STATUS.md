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
| XNU kernel built for J8 | **QEMU: zones/threads, pmap fault** | CI run **44575684242**: `kernel.release.j8`, 11,943,240 bytes, SHA-256 `012342908698006a69d18334c81a4edf620c7aa73d94c8140f01d9345a5d6ca5`. Console works; the crypto fallbacks (0102) let zones, threads and the panic log initialize; new panic `write_pte_fast: PTE write is unaligned` (`pmap.c:623`). No phone boot. |
| J8 shim (4 KiB granule MMU) | **researched** | `shim/mmu.c` is 16 KiB-only |
| Anything booting on the J8 from this project | **QEMU only** | VM init complete; blocked by a NULL platform-expert object; nothing flashed since the rejected probe |

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
| 2026-11-17 06:54 UTC | J8 kernel with GICv2 interrupt path (run **44033283321**) | **success**: links; GICv2 driver, timer PPI 19 dispatch and SGI IPIs are compile-verified only | `BUILD.md` section 4, patch 0087 |
| 2026-11-17 07:16 UTC | QEMU J8 boot (GICv2 machine, timer INTID 27, `cortex-a53`) | payload enters the kernel, then a **prefetch-abort loop at `0xfffffe0007412200`** before serial; 4 KiB bootstrap `T1SZ=25` cannot map the -2 TiB macOS-platform link address | `research/4k-boot-va.md` |
| 2026-11-17 07:56 UTC | J8 L0 bootstrap build (run **44040806897**) | **build success**; QEMU still faults at `_arm_init` with translation fault level 0, although the live L0/L1/L2 entries for that address are valid; TCR has T1SZ 23 | `research/4k-boot-va.md` |
| 2026-11-17 11:49 UTC | QEMU J8 boot, non-large layout + no-LSE build (run **44074310435**) | **boots into XNU VM init**: `pmap_startup`, 76,367 free pages, 97 VM maps; then panics at `kmem_range_init: vm_map_locate_space failing for claim kmem_ptr_range @vm_kern.c:3645` | `evidence/qemu-2026-11-17-kmem-range-panic.png` |
| 2026-11-17 14:52 UTC | Diagnostic panic with claim sizes (run **44098877305**) | `zones` 8 GiB failed at `start=0xfffffff0840e0000`; largest hole 26 GiB; claims were packed forward from a fuzzed cursor | `evidence/qemu-2026-11-17-kmem-zones-debug.png` |
| 2026-11-17 20:08 UTC | J8 **RELEASE** build, no KASAN (run **44162509756**) | boots to VM init; same failure: `zones` 8 GiB at `start=0xfffffff597a18000`, map 112 GiB, largest hole 45.9 GiB. KASAN is not the cause | `evidence/qemu-2026-11-17-kmem-zones-release.png` |
| 2026-11-18 08:21 UTC | J8 RELEASE, zone map capped at 2 GiB (run **44263886581**) | **`kmem_range_init` passes**: VM bootstrap done (86 maps), zone map/compressor/firehose configured; A53 then panics on `ldadd x8, x8, [x9]` (LSE) at pc `0xfffffff0071d0a54` | `evidence/qemu-2026-11-18-lse-undefined-a53.png` |
| 2026-11-18 08:21 UTC | Same kernel on `-cpu max` (LSE capable) | boots past VM init; panic `Kernel data abort` at pc `0xfffffff0077f5f74`, ESR `0x96000005`, FAR `0x28` (`NULL+0x28`) | `evidence/qemu-2026-11-18-data-abort-maxcpu.png` |
| 2026-11-18 09:00 UTC | LSE emulation for J8 (run **44269840744**) | **A53 boots past the LSE**: same `NULL+0x28` platform fault as `-cpu max`; faulting code loads the global at `0xfffffff00713e680` (NULL), caller strings `chip-id`/`IOPlatformExpert` | `evidence/qemu-2026-11-18-lse-emulated-null-abort.png` |
| 2026-11-18 10:08 UTC | Platform expert enabled for J8 (run **44279342492**) | class and `xiaomi,sweet` match string are in the kernel, but the platform expert is still not instantiated: same `Kernel data abort` at `0xfffffff0077f5f74`. Needs `PE_state`/ADT or match diagnostics | `evidence/qemu-2026-11-18-platform-expert-still-null.png` |
| 2026-11-19 17:01 UTC | Clean match kernel (run **44568565796**) | console works; panic in `generatePlatformUUID` (`g_crypto_funcs` NULL, FAR 0x28) because `/chosen` carries chip-id | `evidence/qemu-2026-11-19-clean-match-panic.png` |
| 2026-11-19 17:40 UTC | QEMU ADT without chip-id/unique-chip-id | UUID SHA1 path skipped; next panic `crypto_random_kmem_ctx_size` (FAR 0x150) in `kmem_crypto_init`; patch 0102 extends the SWEET fallbacks to J8 | `evidence/qemu-2026-11-19-crypto-random-panic.png` |
| 2026-11-19 17:40 UTC | Crypto fallbacks for J8 (run **44575684242**) | boots through zones, threads (13) and the panic log; panic `write_pte_fast` unaligned PTE (`ptep=0xfffffff025207290`; `TEST_PAGE_RATIO_4` needs 32-byte alignment) | `evidence/qemu-2026-11-19-write-pte-panic.png` |
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
2. **Done: J8 machine config and GICv2 path** — patches 0082-0087 compile and
   link (CI 44033283321). The GICv2 timer/SGI dispatch is compile-verified only;
   PPI 19 and the non-secure Group 1 view still need a runtime check.
3. **QEMU J8: VM init complete; LSE emulated; platform expert still NULL.**
   Patch 0098 (2 GiB zone map) fixed `kmem_range_init`; patch 0099 emulates LSE
   in the undefined handler. Patch 0100 compiles `SweetPlatformExpert` and its
   `xiaomi,sweet` match entry, but no instance is created and the same
   `NULL+0x28` fault remains. Next: instrument `PE_state.deviceTreeHead` /
   `deviceTreeSize` and the `IOPlatformExpertDevice` match, or fix the ADT
   pointer. Evidence: `research/post-vm-faults.md`,
   `evidence/qemu-2026-11-18-platform-expert-still-null.png`.
4. **Port the shim's own MMU to 4 KiB** (`mmu.c`; the kernel is entered with the
   MMU off, so this is the shim's identity map only), then add the J8 shim
   pieces: GICv2, SDCC/eMMC kernel loading, display and the J8 ADT.
5. **Write the SDCC/eMMC driver** and get a persistent log + HFS+ root.
6. Display/touch/keys, then GPU, then Wi-Fi, then the rest.

Every step has a gate in `FEASIBILITY.md` section 5 and a test in section 7 of
that document. Step 2 is done as a build milestone; nothing has booted.
