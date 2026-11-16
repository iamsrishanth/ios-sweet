# J8 installation, backup and recovery

**Status: no installation is validated. Nothing in this repository has been
flashed to the phone by this project.** The only image prepared so far is a
diagnostic translation-granule probe (`tools/j8/granule-probe-el1.S`). Read this
whole file before writing anything.

The phone is a development unit with a **dead power button**. That changes
recovery more than it changes installation: every procedure below is entered
through TWRP and USB, not through key combos that need power.

## 1. Hard prerequisites (all must be true)

| # | Requirement | How to check |
|---|---|---|
| 1 | The device is the supported model | `tools/j8/j8-compat-check.sh` prints `RESULT PASS` |
| 2 | You have a boot image that is known to boot **on this phone** | hash it and keep a copy on the phone and on the host |
| 3 | Battery above 50 % and stable USB | `dumpsys battery` / TWRP battery indicator |
| 4 | The user is physically present | required for TWRP entry and for judging a failed boot |
| 5 | You accept the data-loss and brick risks in section 6 | explicit approval, per operation |
| 6 | No partition outside `boot` is being written | read the command twice |

If any item is false, stop.

## 2. Device facts that the procedure relies on

- Boot image: legacy `ANDROID!` **v0**, page size 2048, `kernel_addr
  0x80008000`, `ramdisk_addr 0x82000000`, `tags_addr 0x81e00000`, kernel
  `Image.gz-dtb` plus appended DTB. Verified by parsing the image that boots
  today (`tools/j8/mkbootv0.py parse`).
- The boot image that currently boots has **no AVB footer and no
  `SEANDROIDENFORCE` marker**, yet the ROM boots. So this unlocked unit accepts
  a plain v0 image written to `boot`. This is empirical evidence, not a promise
  about arbitrary images.
- `boot` = `mmcblk0p25`, 32 MiB; `recovery` = `mmcblk0p26`, 32 MiB and is
  independent of `boot`; `userdata` = `mmcblk0p46` (~53.5 GiB).
- TWRP 3.6.1 is installed on `recovery` and has survived every flash in the
  recorded history.

## 3. Entering and leaving recovery (dead power button)

- **Enter TWRP:** unplug USB, hold **Volume Up**, plug USB in, keep holding for
  about 10 s.
- **Leave TWRP to the system:** `/sbin/twrp reboot` (or the GUI).
- `adb reboot` **from TWRP** returns to TWRP, not to the system.
- `adb reboot recovery` **from Android** works.
- Download Mode (Odin/Heimdall) is normally `Volume Down + Home + Power`. With a
  dead power button its reachability is **unverified**. Do not plan a recovery
  that depends on Download Mode.
- If `/data` is ever formatted, adb authorization in TWRP is lost
  (`/data/misc/adb/adb_keys`). Re-seed the key over MTP/terminal before relying
  on adb.

## 4. Backups (before the first write, and after anything that may have changed it)

Back up to the host, verify, then keep the copies off git.

```sh
# From TWRP (root shell), or a root shell in Android if available.
mkdir -p /sdcard/j8-backup-$(date +%Y%m%d)
for p in boot recovery persist modem modemst1 modemst2 fsg fsc efs; do
    dd if=/dev/block/by-name/$p of=/sdcard/j8-backup-$(date +%Y%m%d)/$p.img bs=1M
done
sync
```

Then pull and hash on the host:

```sh
adb pull /sdcard/j8-backup-YYYYMMDD ./j8-backup-YYYYMMDD
sha256sum ./j8-backup-YYYYMMDD/*.img > ./j8-backup-YYYYMMDD/SHA256SUMS
```

Rules:

- Never write `persist`, `efs`, `modemst1`, `modemst2`, `fsg`, `fsc`, `modem`,
  `apnhlos`, the bootloader chain (`sbl1`, `aboot`, `tz`, `rpm`, `devcfg`, ...)
  or `recovery` as part of this project. They are read-only evidence.
- Verify a backup by hashing it and by reading the first and last 4 KiB back.
  An unverified backup is not a backup.
- Keep the backups out of git and out of chat.

## 5. Prepared operation: translation-granule probe (NOT yet approved)

Purpose: read `ID_AA64MMFR0_EL1.TGran16` at EL1 and report it without a console
(see `FEASIBILITY.md` section 2 and `tools/j8/README.md`).

Build and verify on the host:

```sh
tools/j8/build-granule-probe.sh work/gapps/boot-stock.img out/granule-probe-j8.img
python3 tools/j8/mkbootv0.py parse out/granule-probe-j8.img
```

The image built on 2026-11-17 has sha256
`661884831810b34b46818829453e37fc8ab68d405e5e01f3a38c48e7d8a2cfd7`
(4096-byte payload, 6144-byte image). Rebuild it and compare; do not flash a
different hash without re-reading the payload.

Flash procedure (only with explicit approval, user present):

```sh
# 1. device must pass the gate
adb shell sh /data/local/tmp/j8-compat-check.sh

# 2. copy the image and the rollback image to the phone
adb push out/granule-probe-j8.img /sdcard/granule-probe-j8.img
adb push work/gapps/boot-stock.img /sdcard/boot-rollback.img

# 3. hash both on the phone before writing
adb shell 'sha256sum /sdcard/granule-probe-j8.img /sdcard/boot-rollback.img'

# 4. boot TWRP, then write ONLY the boot partition
adb reboot recovery
adb wait-for-device
adb shell 'sha256sum /sdcard/granule-probe-j8.img; dd if=/sdcard/granule-probe-j8.img of=/dev/block/by-name/boot bs=1M conv=fsync; sync'

# 5. verify what landed (read back and hash)
adb shell 'dd if=/dev/block/by-name/boot bs=1M count=6 2>/dev/null | sha256sum'

# 6. reboot and observe
/sbin/twrp reboot        # from a TWRP shell; or the TWRP GUI
```

Observe the screen immediately after the reboot:

| Observation | Meaning |
|---|---|
| red band at the top, then the phone powers off | probe ran; `TGran16 = 0` |
| green band at the top, then the phone powers off | probe ran; `TGran16 = 1` (surprising; re-check) |
| normal boot animation/ROM, or a hang/reboot loop | probe did not run; restore `boot` |

The probe powers off in both cases on purpose: a `SYSTEM_RESET` would re-enter
the same image (it is still the boot partition) and loop forever, which is
painful on a unit with a dead power button.

Rollback (do this before any other work, whatever the observation):

```sh
# TWRP entry: unplug, hold Volume Up, plug USB
adb shell 'sha256sum /sdcard/boot-rollback.img; dd if=/sdcard/boot-rollback.img of=/dev/block/by-name/boot bs=1M conv=fsync; sync'
/sbin/twrp reboot
```

The rollback image must hash to
`111aa3ed0a5191ada9e1b32aa7ed82d495f27df2ab67561415c58ca2b873a2ab` (the Havoc
`boot` image kept for this purpose). If it does not, stop and re-copy it.

## 6. Risks and irreversible consequences

| Risk | Consequence | Mitigation |
|---|---|---|
| Writing the wrong partition | identity/calibration loss, no telephony, no recovery | allow-list of partitions; never write by number, only by name; read the command twice |
| Interrupted write | unbootable `boot` (recoverable) or worse | stable USB, charged battery, `conv=fsync`, verify readback |
| Corrupting the bootloader chain | **hard brick**, no software recovery known here | never write `sbl1`, `aboot`, `tz`, `rpm`, `devcfg`, `lksecapp`, `keymaster`, `cmnlib*`, `ddr`, `sec`, `pad` |
| Corrupting `recovery` | lose the only verified entry point | never write `recovery` |
| Stock firmware unavailable | Odin restore may be impossible (Samsung FUS keys rotated; no `version.xml` for EOL models) | treat the bootloader chain and recovery as the last line of defence |
| Knox/rollback counters | irreversible, may block downgrades | do not flash older Samsung firmware (anti-rollback) |
| iOS installation | **erases Android user data** if `userdata` is used as the iOS root | back up what matters; this is a separate, later decision |

**Worst case to understand before starting:** if both `boot` and `recovery`
become unbootable and Download Mode is unreachable, this unit has no known
software recovery path in this workspace. That is why only `boot` is written in
the prepared operation and why `recovery` is off-limits.

## 7. Not validated

- No image built by this project has been flashed.
- Download Mode reachability with a dead power button is unknown.
- The bootloader's acceptance of the probe image is untested (evidence from the
  existing Havoc image suggests unsigned v0 images boot).
- The probe's framebuffer assumption (`splash_region` at `0x90001000`, 720 wide,
  BGRA) is read from the device tree but has not been visually confirmed at
  kernel hand-off.
- TWRP's `sha256sum` availability: the handover records a limited toybox. If
  `sha256sum` is missing, use `md5sum` and compare against a host-side hash, and
  record which tool was used.
