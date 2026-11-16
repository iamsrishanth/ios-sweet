# Samsung J8 (SM-J810G / j8y18lte) — boot chain, image format, install and recovery constraints

Read-only study for the ios-sweet bring-up, 2026-11-17. Nothing was flashed; no phone command was run. Labels: **documented** (cited), **measured** (file or log from this unit), **inferred**,
**unverified** (needs the user). Evidence: repo/docs/{device,recovery,install}.md, repo/artifacts/MANIFEST.md, HANDOVER-2026-11-15.md §3/§7/§11.6, work/subagentB/AVB-VERITY-ANALYSIS.md,
repo/evidence/2026-10-30-first-boot/getprop.txt, work/gapps/boot-stock.img, work/gapps/pifork19/{service.sh,post-fs-data.sh}.

## 0. Verdict

1. No fastboot, so **no temporary boot**: Samsung download mode speaks the Odin/thor protocol [3]. The sweet-style "fastboot boot a test image" loop does not exist here.
2. Workable paths: TWRP zip install or TWRP dd. Download mode + Odin/Heimdall is theoretical — not observed since the power button died (measured, recovery.md §6), and every documented entry combo needs Power [3].
3. boot.img is legacy ANDROID! v0, page 2048, 0x80008000 / 0x82000000 / 0x81e00000, Image.gz-dtb, no AVB footer, no SEANDROIDENFORCE (measured, §3). Unsigned images boot because the loader is unlocked and the images carry no AVB metadata.
4. The green / flash.locked=1 / warranty_bit=0 readings are PlayIntegrityFork's, not the bootloader's (§3, prop discussion). The real reading is verifiedbootstate=orange, warranty_bit=1 (2026-10-30).
5. Rollback means boot only: no system/vendor image exists off-device (MANIFEST.md §4/§6, install.md §9).

## 1. Boot chain on msm8953

**Stages and inputs.** **Documented** [5]: the on-die PBL (mask ROM) loads SBL1 from eMMC; SBL1 brings up DDR and loads QSEE/TrustZone, DEVCFG, the debug policy, RPM firmware and the APPSBL (LK-family
bootloader); QSEE hands over to the APPSBL, which loads and verifies the kernel and switches to AArch64 through an SCM call. XBL/ABL are later Qualcomm UEFI loader names (SDM845 on); for msm8953 the
documented stages are SBL1 and APPSBL/LK [5]. Whether Samsung's loader here is LK or a Samsung sboot build is **unverified** — no source, no dump. Measured identity only: ro.boot.bootloader =
J810GDDU6CWH2 (getprop.txt:316). Call this chain SBL1/LK, not XBL/ABL.

The loader reads the boot header and cmdline (§3) and appends its own androidboot.* arguments, which is where ro.boot.* comes from: measured bootdevice=7824900.sdhci, security_mode=1526595584,
boot_recovery=0, warranty_bit=1, verifiedbootstate=orange (getprop.txt:314-351). It also reads the boot-control block at misc offset 0: writing boot-recovery there forces the next boot into recovery
(documented, recovery.md §4), the main safety lever on this phone (§4 item 5).

**dtbo and vbmeta.** **Documented** [2]: AVB keeps the signed VBMeta struct (image hashes or hashtree roots, plus chained-partition descriptors) in a vbmeta partition, and the loader checks that
signature before trusting boot/system/vendor; dtbo is the Android 9+ way of shipping DT overlays outside the kernel. Here (**measured**, work/subagentB/AVB-VERITY-ANALYSIS.md): no vbmeta.img,
vbmeta_system.img or dtbo.img is built; boot.img is not padded to 32 MiB and has no AVBf footer; the kernel has no CONFIG_DM_VERITY_AVB or CONFIG_ANDROID_VERIFIED_BOOT symbol, so AVB could not be
enforced even with a vbmeta. The DTB rides inside Image.gz-dtb (FDT magic at offset 11,209,222 in boot-stock.img). Whether the eMMC still has partitions named dtbo or vbmeta is **unverified** — list
/dev/block/by-name first (§5).

**Samsung versus a Pixel or Xiaomi fastboot device.** Download mode implements the thor protocol used by Odin [3]; Odin is Samsung's proprietary alternative to fastboot [3]; Heimdall is its
open-source libusb replacement [4]. Entry is a key combination (Power + Volume Down + Home, Power + Volume Down + Bixby, or Volume Up + Volume Down while plugging USB) [3], not adb reboot bootloader;
2026 firmware also demands Maintenance mode first [3], which does not apply here. Odin flashes tar archives into BL / AP / CP / CSC; the bootloader slot is always signature-verified and the loader
re-locks if an unlock was not authorised [6]. There is no raw-partition fastboot path, so every experiment is a persistent write plus reboot, with a second write as rollback.

## 2. Temporary boot, real flash paths, unlock state

**Temporary boot over fastboot: no.** Download mode is Odin/thor [3]; Odin exists because these devices do not speak fastboot [3]; Heimdall reimplements the Samsung protocol, not fastboot [4]. So fastboot boot (run once from RAM, no write) and fastboot flash are both unavailable. This is the hard constraint on the port's test loop and it is not configurable.

**Paths that exist.** TWRP zip install via /sbin/twrp install **works and was exercised** (HANDOVER §3, install.md §5). TWRP raw writes via adb shell dd to /dev/block/by-name/… **work** (root shell in
TWRP) and are the method in §6. Download mode with Odin (Windows) or Heimdall [3][4] has **never been exercised on this unit**: an Odin SetupConnection stall is reported historically and the phone has
not been seen in download mode since the button died (recovery.md §6). Fastboot does not exist [3].

**Unlock state.** OEM unlocking is the Developer-options switch authorising an unlock; unlocking permanently trips Knox and breaks Pay, Pass, Health, Secure Folder and similar [6]. This unit runs a
testkey-signed ROM with a custom boot image, only possible unlocked (**inferred**, strongly, from the fact that it boots). The Knox warranty bit is 1, measured 2026-10-30 (getprop.txt:351): already
tripped, permanent [6], so no boot experiment can worsen it. KG (Knox Guard) state is a separate mechanism that can block flashing or re-lock, shown in download mode; that mode is unreachable, so KG
is **unverified** and adb cannot read it. SW REV / anti-rollback: the firmware binary revision cannot be downgraded [6], so never flash an older Samsung package to restore anything.

Irreversible: the unlock and the tripped warranty bit (both done); flashing below the current SW REV (blocked [6]); a bad bootloader-chain write (no adb, no recovery); a bad efs/persist/modemst/fsg/fsc write (identity, IMEI, calibration); relocking with custom images installed (not a supported rollback — treat as one-way).

## 3. Boot image format, measured

Measured today on work/gapps/boot-stock.img; despite the name it is the ROM/Havoc image, its md5 equals the boot.img inside the ROM zip (HANDOVER §3).

| Field | Value |
|---|---|
| Magic / header version / page size | ANDROID! / 0 (legacy v0) / 2048 |
| kernel_size, kernel_addr | 12,304,751 / 0x80008000 |
| ramdisk_size, ramdisk_addr | 778,850 / 0x82000000 |
| second_size, second_addr, tags_addr | 0 / 0x00000000 / 0x81e00000 |
| name, os_version | 16 NUL bytes / 0x16000167 (not meaningful for this vendor fork) |
| cmdline | console=null androidboot.hardware=qcom msm_rtb.filter=0x237 ehci-hcd.park=3 androidboot.bootdevice=7824900.sdhci lpm_levels.sleep_disabled=1 androidboot.selinux=permissive buildvariant=userdebug |
| size | 13,088,768 B (partition 33,554,432 B) |
| md5 / sha256 | 05836b23f6c3390a331049e211c46d9f / 111aa3ed0a5191ada9e1b32aa7ed82d495f27df2ab67561415c58ca2b873a2ab |
| SEANDROIDENFORCE / AVBf footer | both **absent** (0 occurrences; last 64 bytes zero) |
| Appended DTB | FDT magic at offset 11,209,222, inside the kernel payload |

The device tree pins the same geometry — base 0x80000000, ramdisk offset 0x02000000, tags offset 0x01e00000, pagesize 2048, Image.gz-dtb (device.md §5). device.md quotes kernel_size 12,297,618 for the
Lineage 17.1 build; boot-stock.img is Havoc. **Documented** [1]: v0 is one header page, then page-aligned kernel, ramdisk and optional second stage, placed at the header addresses with ATAGS at
tags_addr and kernel_args appended.

**Unsigned or modified images: accepted, measured.** The Havoc image (md5 05836b…) boots after a TWRP write; the project's own OTA boot image, signed with project keys, is recorded as installed and
booted (MANIFEST.md §1); the whole bring-up history is images Samsung never signed. Mechanism, split honestly: known — no AVB footer and no vbmeta, so AVB cannot validate them [2]; known — the kernel
cannot verify anything (no AVB/verity symbols, subagentB §1/§3); **inferred** — ABL skips its locked-mode signature check while OEM LOCK is off, and that logic inside J810GDDU6CWH2 is not public
(**unverified**). Safe statement: here, unlocked, unsigned v0 images boot; do not generalise to a locked device.

**Why green / locked=1 / warranty_bit=0 is not the bootloader.**
1. Measured 2026-10-30, before any integrity module existed (getprop.txt:350-351): verifiedbootstate = orange, warranty_bit = 1. That dump has **no ro.boot.flash.locked key at all** — this loader does not export it.
2. Measured: PlayIntegrityFork (installed 2026-11-15) rewrites these properties each boot. service.sh:33-44 sets ro.secureboot.lockstate locked, ro.boot.flash.locked 1, ro.boot.vbmeta.device_state locked, ro/vendor.boot.verifiedbootstate green, ro.boot.veritymode enforcing; post-fs-data.sh:21-24 sets ro.boot.warranty_bit 0.
3. The writes run after sys.boot_completed=1: an early getprop shows real values, a late one masked values.
4. Cross-check: the cmdline in boot-stock.img contains no verifiedbootstate, flash.locked, warranty_bit or vbmeta token, so init cannot derive them from the image. They come from ABL's appended cmdline (orange, 1 — real) or from resetprop (green, 1, 0).

Trust the pre-PIF snapshot: orange, warranty_bit=1, flash.locked not exported. Orange is the AOSP state for an unlocked device with a custom boot chain (general AOSP semantics, **inferred** here). The
earlier "green, flash.locked=1" claims (HANDOVER.md:129, HANDOVER-2026-11-15.md:160) are best explained as reads of the spoofed properties or assertions never captured —
work/subagentA/ARTIFACT-REVIEW.md:233/376 flags exactly that. device.md §1 mixes both eras: quote value and date together.

## 4. Recovery with a dead power button

1. **TWRP entry** (documented, exercised): unplug USB, hold Volume Up, plug USB in, hold about 10 s; adb shows recovery (recovery.md §1, HANDOVER §7). Recovery is mmcblk0p26 and no ROM update touches it — byte-identical after the OTA, sha256 1b6538d5…6e05 (MANIFEST.md §3). Sideload has no shell.
2. **Return to system**: /sbin/twrp reboot system. Plain adb reboot from TWRP returns to TWRP, observed repeatedly (HANDOVER §11.6, recovery.md §3). Likely cause: the BCB in misc still says boot-recovery and only TWRP's own reboot path clears it (**inferred**, no misc dump proves it). Never use adb reboot to leave recovery.
3. **twrp CLI wedge**: invalid arguments park it on "Running Recovery Commands" and every later action fails with "Another threaded action is already running" (HANDOVER §4.2). Valid arguments only.
4. **Download mode: unknown, probably no.** Every documented combo includes Power [3]; the button is dead (recovery.md §8). /sbin/twrp reboot download is worth one supervised attempt, but it has never worked here and Odin has never completed a session (recovery.md §6). Never plan a route that depends on it.
5. **Pre-arm the next boot**: write the 12-byte BCB first (reversible, documented): printf 'boot-recovery' | dd of=/dev/block/by-name/misc bs=1 seek=0 conv=notrunc. If the new image never reaches adbd, the next power-up lands in TWRP instead of a dead loop.

## 5. Partition map

Measured entries only, sizes in bytes (device.md §4, HANDOVER.md §5, MANIFEST.md §4).

| Partition | Block | Size |
|---|---|---|
| efs | mmcblk0p16 | 16,777,216 (named in audit logs for /efs/FactoryApp, log4.txt:2933) |
| boot / recovery | mmcblk0p25 / p26 | 33,554,432 each |
| persist | mmcblk0p31 | 33,554,432 |
| system / vendor | mmcblk0p40 / p41 | 3,397,386,240 / 503,316,480 |
| cache | mmcblk0p43 | 314,572,800 |
| userdata | mmcblk0p46 | 57,415,790,592 (live read 57,415,806,976 — measure before writing) |
| misc / config | not captured | 1,048,576 each |
| modemst1 / modemst2 / fsg | not captured | 4,194,304 each |
| rpmb | mmcblk0rpmb | not captured |

**Known gap**: the full p1..p46 map and the bootloader-chain names are **not captured anywhere in this workspace** — searching mmcblk0pNN returns only the rows above. p1-p15, p17-p24, p27-p30, p32-p39, p42, p44, p45 are unknown, and with them the exact never-write set. Capture first:

    adb shell ls -l /dev/block/by-name
    adb shell "for f in /dev/block/by-name/*; do echo \$f \$(blockdev --getsize64 \$f); done"

Never overwrite: the bootloader chain (names unverified; Samsung always verifies it [6], and a bad write removes every software route); eMMC hardware boot partitions mmcblk0boot0/1, write-protected by
default for this reason [7]; mmcblk0rpmb; modem and apnhlos; efs, persist, modemst1/2, fsg, fsc, config (identity, IMEI, calibration — device.md §4, recovery.md §7); the PIT or any repartitioning; ODM
(stock property overrides, device.md §11). Safe with care: boot (experiment target), recovery (verified image only), cache, misc (12 bytes), system/vendor/userdata (ROM payload, destructive).

## 6. Safe flashing procedure for an experimental boot image (TWRP + adb)

Pre-flight: battery at least 40 percent on a powered USB port (a drained phone was lost for hours mid-reboot, recovery.md §5); TWRP via Volume Up + USB with adb authorised (re-seed
/data/misc/adb/adb_keys if unauthorized, recovery.md §2); candidate at most 33,554,432 bytes with its header parsed (§3); rollback image hashed (boot-stock.img, md5 05836b23…); current boot hash
recorded (adb shell sha256sum /dev/block/by-name/boot).

    adb push newboot.img /tmp/newboot.img
    adb shell sha256sum /tmp/newboot.img                     # must match the host hash
    adb shell blockdev --getsize64 /dev/block/by-name/boot    # expect 33554432
    adb shell "printf 'boot-recovery' | dd of=/dev/block/by-name/misc bs=1 seek=0 conv=notrunc"
    adb shell "dd if=/tmp/newboot.img of=/dev/block/by-name/boot bs=4096; sync"
    adb shell "dd if=/dev/block/by-name/boot of=/tmp/readback.img bs=4096"
    adb shell sha256sum /tmp/readback.img                    # must match the pushed hash

Stop on any mismatch; never reboot after a failed write (install.md §5). The readback is the only proof the write landed. Then boot and check:

    adb shell "/sbin/twrp reboot system"; adb wait-for-device
    adb shell getprop sys.boot_completed      # 1
    adb shell getprop ro.boot.selinux         # as built
    adb shell cat /proc/version               # 3.18.140 expected

First boot after a change dex2oats at full CPU for 5-10 minutes: not a failure, and do not pull power. Post-checks: sha256 of recovery unchanged (1b6538d5…, proves TWRP survived); sha256 of boot equals the intended image; ro.build.flavor/release identify the ROM that really booted; dmesg free of panic (avc denials are noise while permissive, subagentB §7).

Rollback with adb or TWRP alive is one write of boot-stock.img (pushed and hash-checked first) plus sync and readback. Full ROM rollback: /sbin/twrp install /data/media/0/havoc-4.19.zip (md5
06342874b73a70c21b8d57ed14208065), rewriting system, vendor and boot. If neither adbd nor TWRP comes up there is no software route: the phone waits for a power cycle, which without a power button
means battery depletion or service (recovery.md §8). Hence the BCB write first.

## 7. Physical verification and never-automate

Verify with the user present: battery level and powered port before any write; that TWRP was reached by the Volume Up + USB sequence and adb is authorised; the first boot of every new image (watch it
reach the boot animation or adbd, ready to cut USB and use the BCB); any attempt at download mode, which may leave the phone in a mode nothing else exits; the decision to format data, which destroys
/sdcard and the adb key.

Never automate: writes to the bootloader chain, mmcblk0boot0/1, rpmb, PIT or a repartition; writes to efs, persist, modemst1/2, fsg, fsc, config, modem, apnhlos; Odin/Heimdall flashes of BL/CP/CSC and
any firmware downgrade (anti-rollback [6]); relocking the bootloader; formatting data without a verified identity backup; twrp with invalid arguments or adb reboot from TWRP; flashing below about 40
percent battery; flashing an image whose header was not parsed or which exceeds the partition.

## 8. Summary

| Operation | Tool | Reversible? | Data loss | Prerequisites | Evidence |
|---|---|---|---|---|---|
| Flash boot with a test image | TWRP + adb dd | Yes, rewrite boot | No, unless it never boots | TWRP + adb, hash verified, BCB pre-set | measured §6, install.md §6 |
| Roll back boot | TWRP + adb dd | n/a | No | boot-stock.img md5 05836b23… | measured HANDOVER §7, §3 |
| Reflash the ROM | /sbin/twrp install | Yes, to that ROM | Yes: system/vendor/boot | zip md5 06342874…, format may be needed | documented HANDOVER §3, install.md §5 |
| Format data | /sbin/twrp format data | No | Yes: /data, /sdcard, adb key | backup taken | documented install.md §4 |
| Force next boot to recovery | dd 12 bytes to misc | Yes | No | misc writable | documented recovery.md §4 |
| Flash TWRP | dd to recovery, or Odin AP | Yes if verified | No | TWRP hash 1b6538d5… | documented MANIFEST §3 |
| Temporary boot of a test kernel | fastboot boot | Not available | — | — | documented [3], inferred |
| Flash via download mode | Odin / Heimdall | In principle | Depends on slots | download mode — unreachable today | documented [3][4], unverified here |
| Flash the bootloader chain | Odin BL | No | Possible total loss | Never | documented [6][7] |
| Read boot state | getprop | Read-only | No | adb | measured getprop.txt, PIFork scripts |

## Sources

1. AOSP mkbootimg boot image header v0 layout (bootimg.h) — https://android.googlesource.com/platform/system/tools/mkbootimg/+/a0eaee9cbff3c2e3b67c03974a2da18df391ecee%5e%21/include/bootimg/bootimg.h — accessed 2026-11-17.
2. Android Verified Boot 2.0 README (VBMeta struct, vbmeta partition, locked/unlocked mode) — https://android.googlesource.com/platform/external/avb/+/refs/heads/main/README.md — accessed 2026-11-17.
3. Odin (firmware flashing software), Wikipedia (download mode, thor protocol, alternative to fastboot, entry combos, 2026 Maintenance mode) — https://en.wikipedia.org/wiki/Odin_(firmware_flashing_software) — accessed 2026-11-17.
4. Heimdall, open-source cross-platform replacement for Odin — https://github.com/gfgit/Heimdall — accessed 2026-11-17.
5. 高通8953启动流程, MSM8953 boot flow: PBL, SBL1, QSEE, DEVCFG, APPSBL/LK, HLOS — https://www.cnblogs.com/zzb-Dream-90Time/p/9998427.html — accessed 2026-11-17.
6. bootloader-unlock-wall-of-shame, Samsung section (unlock permanently trips Knox, SW REV downgrade rule, Samsung always verifies bootloaders, unsigned-binary behaviour) — https://github.com/zenfyrdev/bootloader-unlock-wall-of-shame/blob/main/brands/samsung/README.md — accessed 2026-11-17.
7. Linux kernel documentation, SD and MMC Device Partitions (eMMC boot partitions write-protected by default) — https://docs.kernel.org/driver-api/mmc/mmc-dev-parts.html — accessed 2026-11-17.
