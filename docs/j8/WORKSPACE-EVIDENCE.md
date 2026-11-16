# J8 workspace evidence — inventory, provenance and applicability

Status: written 2026-11-17. This file records what already existed in
`C:\Code\j8-os` before this porting effort started, how reliable each item is,
and what it does and does not prove for an iOS/XNU port.

Nothing here was produced by the iOS port. Every item is either a historical
Android result or a measurement taken read-only from the connected phone on
2026-11-17. Keep that distinction when citing it.

## 1. How this inventory was made

- Read the handover documents, `repo/`, `scripts/`, `work/`, `ota_audit/`,
  `local_manifests/` and `wa_probe/` in `C:\Code\j8-os`.
- Re-read the device with read-only `adb shell` commands (properties, kernel
  config, `/proc/partitions`, `/dev/block/by-name`, `/sys/class/*`,
  `/proc/device-tree`). No partition was written, no image was flashed, no
  reboot was performed.
- Cross-checked conflicting values instead of picking one silently. Conflicts are
  listed in section 6.

Sanitisation: the device serial, `em.did`/`ap_serial` and MAC addresses are
deliberately **not** reproduced here, in line with `AGENTS.md`. They stay in the
existing local dumps only.

## 2. Source inventory

| Path | Date | What it is | Reliability | Relevance to this port |
|---|---|---|---|---|
| `repo/docs/device.md` | 2026-10-30 | Measured J8 reference: identity, arch, partitions, boot v0 header values, kernel config, vendor blob pins | High for measurements; partly stale (written for a LineageOS 17.1 build that is no longer installed) | **Primary** hardware reference |
| `repo/evidence/2026-10-30-first-boot/` | 2026-10-30 | `getprop`, `dmesg-boot.txt` (76 KB), `logcat-errors.txt`, AVC summary, screenshots from the **LineageOS 17.1 (Android 10)** first boot, redacted | High; captures a different ROM than the one now installed | Driver names, panel/touch/PMIC behaviour, boot timing |
| `repo/README.md`, `STATUS.md`, `VALIDATION.md`, `docs/*.md` | 2026-10-30 | LineageOS 17.1 device-tree project: build, install, recovery, signing, validation | High for Android build facts | Partition/backup/build conventions |
| `HANDOVER-2026-11-15.md` | 2026-11-15 | Havoc-OS 4.19 (Android 11) install, Magisk/PIF, WhatsApp/Play-Integrity investigation, recovery procedures, dead power button | High, first-hand | Recovery constraints, root reality, TWRP behaviour |
| `HANDOVER-2026-10-30.md`, `HANDOVER-2026-10-26.md`, `HANDOVER.md`, `agent_handover_lineageos_20_gsi_on_galaxy_j8.md` | 2026-10 to 2026-10-30 | Earlier Android/GApps/GSI experiments | Historical, partly superseded | Device quirks only |
| `work/gapps/boot-stock.img` | 2026-11 | Havoc boot image kept for rollback, md5 `05836b23f6c3390a331049e211c46d9f` | High; hash verified in the handover | Recovery anchor, boot v0 example |
| `work/havoc/`, `work/gapps/` | 2026-11 | WhatsApp probe, GApps overlay, Magisk modules, adb-key repair | High for Android work | None for XNU (except recovery scripts) |
| `ota_audit/` | 2026-11-13 | OTA package forensics scripts (`bootparse.ps1`, `sigverify*.ps1`, `zipinfo.ps1`, CRCs) | High | Independently re-parsed the boot v0 header |
| `scripts/`, `local_manifests/` | 2026-10 | LineageOS sync/build/backup scripts; `j8y18lte.xml` manifest | High | Backup procedure, device-tree pins |
| `wa_probe/` | 2026-11-13 | WhatsApp/Play-Integrity evidence, 900+ MB | Irrelevant | Must not be committed |

There is **no** prior XNU/iOS work in the workspace. `ios-sweet` was an empty
directory until this task; the repository was cloned from
`https://github.com/iamsrishanth/ios-sweet` (a fork of `dipa-studio/ios-sweet`)
on branch `samsung-j8-bringup`.

## 3. Device identity — measured 2026-11-17 (this port)

| Property | Value | Source |
|---|---|---|
| Model / device | SM-J810G / `j8y18lte` | `ro.product.model`, `ro.product.device` |
| Platform / board | `msm8953` / `MSM8953` (Snapdragon 450) | `ro.board.platform`, `ro.product.board` |
| Bootloader | `J810GDDU6CWH2` | `ro.boot.bootloader` |
| Hardware revision | 4 | `ro.boot.revision` |
| Current OS | Havoc-OS 4.19, Android 11 (RQ3A.211001.001), userdebug, permissive | `ro.build.*` |
| CPU | 8 × ARM part `0xd03` r0p4 (Cortex-A53), `CPU architecture: 8` | `/proc/cpuinfo` (32-bit view) |
| Kernel | 3.18.140, `CONFIG_ARM64=y`, `CONFIG_ARM64_4K_PAGES=y`, `CONFIG_ARM64_VA_BITS=39`, `CONFIG_COMPAT=y` | `/proc/config.gz` |
| Userspace ABI | `armeabi-v7a,armeabi`; `zygote32`; **no** `/system/lib64` or `/vendor/lib64` | properties, file system |
| Storage | eMMC, 61,071,360 KiB, single `mmcblk0`, `7824900.sdhci`, `ro.boot.emmc=true` | `/proc/partitions`, `/sys/block` |
| GPU | `Adreno506v1`, max 600 MHz (600/560/510/400/320/216/133 MHz) | `/sys/class/kgsl/kgsl-3d0` |
| Panel | Samsung `S6E8AA5X01` AMS604RM01 HD, 720×1480, DSI command mode | kernel cmdline `mdss_mdp.panel`, DT |
| Touch | Zinitix BT532 (`zinitix,bt532_ts_device`) on `i2c@78b6000` | `/proc/device-tree` |
| Wi-Fi | integrated WCNSS (`qcom,wcnss-wlan@0a000000`), prima/qcacld driver, firmware `/vendor/firmware/wlan/prima` | DT, `dmesg` (`VosMC`, `limProcessSwitchChannelRsp`) |
| Modem data | IPA `qcom,ipa@07900000`, QMI/GLINK/SMD paths present | DT |
| Charger/PMIC | Samsung SM5708; `samsung,sec-battery` | DT, `dmesg` |
| Audio | `qcom,msm-dai-q6`, `samsung,q6audio-adaptation`, NXP TFA9896 firmware present | DT, `/vendor/firmware/Tfa9896.cnt` |
| USB | DWC3 `ssusb@7000000` + `qusb@79000` | DT |
| Interrupt controller | `qcom,msm-qgic2` — **GICv2**: GICD `0x0b000000`, GICC `0x0b002000` | DT |
| PSCI | `arm,psci-1.0`, method `smc` | DT |
| RAM | lower bank `0x80000000` + 2 GiB; upper bank `0x1_0000_0000` + 1.75 GiB | DT `memory/reg` |

## 4. Historical results and what they establish

### 4.1 LineageOS 17.1 first boot (2026-10-30)

Establishes, for Android 10 on this exact phone: the panel/touch/charger/GPU
drivers work, the partition layout in `repo/docs/device.md` is correct, and the
unit boots a from-source AOSP build. It does **not** establish anything about
XNU, iOS userland, or 64-bit userspace: that ROM is 32-bit userspace too.

### 4.2 Havoc-OS 4.19 (2026-11-15)

Establishes: the boot partition accepts a custom, testkey-signed `boot.img`;
TWRP 3.6.1 on `recovery` is independent of `boot` and survived every flash; the
ROM boots with `androidboot.selinux=permissive`; `/data` can be formatted and
rebuilt; adb works but `adb root` is refused (`ro.debuggable=0`).

### 4.3 Recovery constraints (apply to this port)

- **Power button is dead.** TWRP entry: unplug, hold Volume Up, plug USB, keep
  holding. Return to system: `/sbin/twrp reboot`. `adb reboot` **from TWRP**
  reboots back into TWRP.
- TWRP CLI (`/sbin/twrp install <zip>`) works; invalid arguments wedge the
  recovery until a cold restart. OpenRecoveryScript is not reliable on this build.
- Formatting `/data` deletes `/data/misc/adb/adb_keys`; adb then stays
  `unauthorized` in TWRP until a key is re-seeded.
- Download Mode reachability without a power button is **unverified** (see
  `INSTALL-RECOVERY.md`). All flash procedures in this repository use TWRP + adb
  only, because that path is the one with first-hand evidence.

## 5. Assets that must be preserved

- `work/gapps/boot-stock.img` (Havoc boot, md5 `05836b23f6c3390a331049e211c46d9f`).
- `/data/media/0/havoc-4.19.zip` on the phone (md5 `06342874b73a70c21b8d57ed14208065`).
- `recovery` partition (TWRP) — never write to it from this project.
- Bootloader chain, `modem`, `modemst1/2`, `fsg`, `fsc`, `efs`, `persist`, `apnhlos`,
  `apdp`, `msadp`, `devcfg`, `tz`, `rpm`, `sbl1`, `aboot`, `keymaster`, `cmnlib*`,
  `keystore`, `ssd`, `sec`, `pad`, `ddr`.
- Existing partition backups referenced by the handovers. They were **not**
  modified, moved or re-read as part of this task.

## 6. Conflicts and stale claims (do not paper over)

1. **Verified-boot state.** Live `getprop` reports
   `ro.boot.verifiedbootstate=green`, `flash.locked=1`, `warranty_bit=0`; the
   boot command line actually embedded in `boot-stock.img` reports
   `androidboot.verifiedbootstate=orange`, `androidboot.warranty_bit=1`. On this
   ROM the `ro.boot.*` values are therefore **not** a trustworthy readout of the
   bootloader state; treat both as ROM-fed and verify physically in Download Mode
   if it ever matters. (`repo/docs/device.md` recorded `orange`/`1`.)
2. **`ro.boot.emmc=true` vs UFS assumptions.** The reference ports use UFS. This
   phone is eMMC behind `7824900.sdhci`; the reference UFS driver and its
   partition naming do not apply. This is confirmed three ways (property, block
   device topology, device tree).
3. **Userspace architecture.** `repo/docs/device.md` describes `arm32_binder64`
   with a 64-bit kernel; that is still true today. A 32-bit Android userspace
   does not imply the boot chain cannot run an AArch64 kernel — the installed
   kernel *is* AArch64 (`CONFIG_ARM64=y`) and a hand-built AArch64 EL0 binary
   runs (see `docs/j8/research/arm-granules.md`).
4. **`repo/docs/device.md` partition sizes** were measured on an earlier build;
   the live sizes agree to within one erase block for `userdata`, as already
   noted there. Always measure before writing.
5. **Havoc `boot` md5** identifies the Havoc boot image; it is not a Samsung
   stock image and cannot be used to restore stock firmware.

## 7. Questions the workspace could not answer

- Does the bootloader accept an unsigned or differently signed `boot.img`? (Only
  testkey-signed AVB images have been tried.)
- Is Download Mode reachable with a dead power button (button-combo or `adb
  reboot download`)?
- Which exact eMMC part and speed mode, and whether the SDCC controller is
  usable from a non-Linux kernel without the RPM clock votes.
- Bluetooth transport and firmware files (the phone's BT was never brought up in
  the recorded tests; `bluetoothd` had no hardware under XNU).
- Fingerprint sensor part and bus; sensor hub (SSC) firmware interface.
- Whether this unit's TEE/TrustZone can load the GPU zap shader through PIL from
  a non-Android kernel.

Each of these is listed with its blocker in `STATUS.md`.
