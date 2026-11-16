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

## 5. Translation-granule probe (attempted 2026-11-17; rejected by the bootloader)

Purpose: read `ID_AA64MMFR0_EL1.TGran16` at EL1 and report it without a console
(see `FEASIBILITY.md` section 2 and `tools/j8/README.md`).

Build and verify on the host:

```sh
tools/j8/build-granule-probe.sh work/gapps/boot-stock.img out/granule-probe-j8.img
python3 tools/j8/mkbootv0.py parse out/granule-probe-j8.img
```

The image built on 2026-11-17 has sha256
`73fffc9d575e93a7431c2fb680fda2b0266de919b0ca40e20cb055703b04b24a`
(4096-byte payload, 6144-byte image). Rebuild it and compare; do not flash a
different hash without re-reading the payload.

Flash procedure (only with explicit approval, user present). The commands are
the ones that actually ran on 2026-11-17; TWRP's toybox **rejects
`conv=fsync`**, so use a plain write followed by `sync`.

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
adb shell 'dd if=/sdcard/granule-probe-j8.img of=/dev/block/by-name/boot bs=2048 count=3; sync'

# 5. verify what landed (the probe image is 6144 bytes = 3 x 2048)
adb shell 'dd if=/dev/block/by-name/boot bs=2048 count=3 2>/dev/null | sha256sum'
# must print 73fffc9d...b24a

# 6. reboot and observe
/sbin/twrp reboot        # from a TWRP shell; or the TWRP GUI
```

Observe the screen immediately after the reboot:

| Observation | Meaning |
|---|---|
| red band at the top, then the phone powers off | probe ran; `TGran16 = 0` |
| green band at the top, then the phone powers off | probe ran; `TGran16 = 1` (surprising; re-check) |
| **`BOOT RECOVERY MODE` / `Download mode !!`** | **the bootloader refused the image; the probe did not run** (observed 2026-11-17) |
| normal Android boot | boot content was not the probe |

The probe powers off in both cases on purpose: a `SYSTEM_RESET` would re-enter
the same image (it is still the boot partition) and loop forever, which is
painful on a unit with a dead power button.

### 5.1 Result of the first attempt (2026-11-17, 01:22 IST) — rejected

The image above was written, verified by readback, and the phone was rebooted.
It did **not** run. The phone was later found in Samsung **`BOOT RECOVERY MODE
(OMR SPEC)` / `Download mode !!`**, with:

- `Set Warranty Bit : kernel`, `CURRENT BINARY: Custom`, `OEM LOCK: OFF`
- `QUALCOMM SECUREBOOT: ENABLE (CSB)`, `SECURE DOWNLOAD : ENABLE`

Conclusion: the ABL loaded the image, marked it as a custom kernel, and then
aborted the boot; the probe's framebuffer paint never appeared. The J8's
`by-name` list has **no `vbmeta` partition**, so this is Samsung's own boot
authorisation, not an AVB hash descriptor. The probe image is also structurally
unusual (a 4 KiB raw `Image`-style payload with **no appended DTB and no
ramdisk**, unlike the ROM's `Image.gz-dtb` + 778 KB ramdisk), which is the most
likely trigger. Until that is fixed and the image is accepted, do not re-flash
it.

### 5.2 Recovery from Download Mode (this worked)

Because the power button is dead, a boot failure lands in Download Mode and the
only recovery is Odin/Heimdall over USB. The image that restored the phone was
the ROM boot image, packaged as an Odin AP tar:

```python
# host-side: tar containing boot.img (the ROM image, sha256 111aa3ed...a2ab)
import tarfile, os
with tarfile.open("j8-boot-rollback.tar", "w", format=tarfile.USTAR_FORMAT) as t:
    ti = tarfile.TarInfo("boot.img"); ti.size = os.path.getsize("boot-stock.img"); ti.mtime = 0
    t.addfile(ti, open("boot-stock.img", "rb"))
```

Odin 3.13.1, phone in Download Mode:

1. confirm the blue `ID:COM` port; leave only **Auto Reboot** and **F. Reset
   Time** ticked (never NAND Erase or Re-Partition),
2. **AP** = `j8-boot-rollback.tar`,
3. **Start** → green **PASS!** → phone rebooted into Android.

The restore replaced a Magisk-patched `boot` (the installed image's first 6144
bytes hashed `3235…`, not the ROM image's `a8139d…`), so **root was lost**. That
is acceptable for recovery; re-rooting is a separate operation.

Heimdall 1.4.2 on this Windows host detects the phone in Download Mode but
cannot claim the interface (`libusb error -12`): the Samsung CDC/modem driver is
bound instead of WinUSB. Odin is the working path here.

### 5.3 TWRP rollback (only if TWRP is reachable)

```sh
adb shell 'dd if=/sdcard/boot-rollback.img of=/dev/block/by-name/boot bs=1024 count=12782; sync'
adb shell 'dd if=/dev/block/by-name/boot bs=1024 count=12782 2>/dev/null | sha256sum'
/sbin/twrp reboot
```

The rollback image must hash to
`111aa3ed0a5191ada9e1b32aa7ed82d495f27df2ab67561415c58ca2b873a2ab` (the ROM
`boot` image). If it does not, stop and re-copy it.

## 6. Risks and irreversible consequences

| Risk | Consequence | Mitigation |
|---|---|---|
| Bootloader refuses the image | phone lands in Download Mode with no adb; recovery needs Odin/Heimdall (power button is dead) | use a boot image the ABL accepts (ROM image, or one with a valid DTB/ramdisk); stage Odin and the rollback tar before flashing |
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

## 7. Validated and not validated

Validated on 2026-11-17:

- The read-only compatibility gate passes on the unit.
- TWRP adb, the `boot` write (6144 bytes), and the readback hash check work.
- A failed boot lands in Download Mode; **Odin 3.13.1 with the ROM `boot.img`
  AP tar restores Android** (observed PASS and a booted system).
- Heimdall 1.4.2 cannot flash on this host (Samsung driver bound; libusb -12).

Not validated / open:

- The bootloader's acceptance of a custom probe image: the first attempt was
  **rejected** (section 5.1). Whether an image with an appended DTB + ramdisk is
  accepted is untested.
- The probe's framebuffer assumption (`splash_region` at `0x90001000`, 720 wide,
  BGRA) is read from the device tree but has not been visually confirmed at
  kernel hand-off.
- The installed `boot` before the experiment was not the ROM image (likely
  Magisk-patched). No full backup of that exact image was taken; only the
  first 6144 bytes were read back (hash `3235…`). Root was lost in the restore.
- TWRP's `sha256sum` availability: the handover records a limited toybox. If
  `sha256sum` is missing, use `md5sum` and compare against a host-side hash, and
  record which tool was used.
- Download Mode key combos with a dead power button remain unverified; this
  fallback was entered by the bootloader itself, not by a key combo.
