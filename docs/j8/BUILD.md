# J8 build guide (reproducible setup)

The J8 port inherits the `ios-sweet` build system. This file records the pinned
inputs, which host each stage needs, the commands that have been run, and the
commands that are prepared but **not yet run**.

## 1. Pinned inputs

| Input | Pin | Where |
|---|---|---|
| Destination repo | `github.com/iamsrishanth/ios-sweet`, branch `samsung-j8-bringup` | git |
| Upstream base | `dipa-studio/ios-sweet` @ `45263bee51df4a0f55cdf9c649af886399488ddb` (2026-10-23) | `git log` |
| Reference (GPU/Wi-Fi) | `joranv1/ios-sailfish` @ `6f27e41ff249f2680540ebe8ee5ab982c5e641c9` (2026-10-22) | `git log` |
| XNU | `apple-oss-distributions/xnu` tag `xnu-8796.101.5`, commit `aca3beaa3dfbd42498b42c5e5ce20a938e6554e5` | Apple public source |
| XNU build harness | `blacktop/darwin-xnu-build` @ `663906528d1cd5ea8fde7de6f16de73db9511549` | `.github/workflows/xnu.yml` |
| Kernel CI runner | GitHub `macos-14`, Xcode 15.x, KDK 13.5 | workflow |
| iOS userland | iPhone X (`iPhone10,3` / D22AP) iOS 16.7.16 build `20H392` | user-supplied IPSW, **not in git** |
| Probe toolchain | Zig (pip `ziglang`) or `aarch64-linux-gnu-gcc`; Python 3.11 | host |

A different macOS/Xcode/KDK combination is not guaranteed to produce the same
kernel; the reference project's CI exists because those three are coupled (see
the `iig`/IORPC comment in the workflow).

## 2. Host roles

| Stage | Windows | Linux/WSL | macOS |
|---|---|---|---|
| Docs, device checks, boot-image packing | yes | yes | yes |
| EL0 probe cross-compile | yes (Zig) | yes | yes |
| EL1 probe assembly/packing | yes (Zig) | yes (GNU) | yes |
| Shim build (`shim/build.sh`) | via container | yes (`aarch64-linux-gnu-gcc`) | yes |
| XNU kernel build | no | no | **required** (Xcode + KDK) |
| iOS root image (`mkiosroot.sh`) | no | yes (apfs-fuse, hfsprogs, sudo) | yes (`mkiosroot-mac.sh`) |
| Flashing / device tests | yes (adb/TWRP) | yes | yes |

## 3. Commands that have been run and verified (2026-11-17)

```sh
# 1. Boot image parser round-trip against a known-good image
python3 tools/j8/mkbootv0.py selftest <boot-stock.img>
# -> selftest OK: ... round-trips byte-identically

# 2. EL0 probe (static AArch64, no root)
tools/j8/build-probe.sh out/j8probe
adb push out/j8probe /data/local/tmp/j8probe
adb shell 'chmod 755 /data/local/tmp/j8probe; /data/local/tmp/j8probe'
# -> "EL0-ID-READ: TRAPPED (SIGILL)"  (expected on this kernel)

# 3. Compatibility gate on the phone (read-only)
adb push tools/j8/j8-compat-check.sh /data/local/tmp/
adb shell sh /data/local/tmp/j8-compat-check.sh
# -> RESULT PASS (model/partition compatibility only)

# 4. EL1 granule probe image (built, NOT flashed)
tools/j8/build-granule-probe.sh <boot-stock.img> out/granule-probe-j8.img
# -> sha256 73fffc9d575e93a7431c2fb680fda2b0266de919b0ca40e20cb055703b04b24a
```

The `<boot-stock.img>` used locally is the Havoc boot image kept for rollback
(sha256 `111aa3ed0a5191ada9e1b32aa7ed82d495f27df2ab67561415c58ca2b873a2ab`).
It is device data and is not committed. Any v0 image that boots on the phone can
supply the same header geometry; the packer refuses v1+ headers on purpose.

## 4. Building the kernel (prepared, not run for J8)

The upstream workflow is used unchanged:

```sh
gh workflow run xnu.yml -R iamsrishanth/ios-sweet \
  -f macos_version=13.5 -f kernel_config=DEVELOPMENT -f machine_config=SWEET
gh run download <RUN_ID> -R iamsrishanth/ios-sweet -n xnu-kernel -D xnu/out
```

For a J8 kernel this needs, first:

1. a `J8` machine config (`pexpert/pexpert/arm64/J8.h`, `MakeInc.def`,
   `board_config.h`, the `pexpert` Makefile) with `ARM_PGSHIFT = 12`, i.e. **no
   `__ARM_16K_PG__`**, `MAX_CPUS 8`, `MAX_CPU_CLUSTERS 2`, cache line 64 B,
   GICv2 bases (`0x0b000000` / `0x0b002000`), timer PPI 19, PSCI/SMC;
2. a shim that builds 4 KiB-granule tables and no longer requires the 16 KiB
   granule (`shim/mmu.c`, `shim/shim.c`);
3. the J8 board devices (SDCC/eMMC, display, touch, keys) added as separate
   patches so the reference ports stay intact.

None of that exists yet. The workflow will not produce a bootable J8 kernel
until at least items 1-2 are implemented. A build of the J8 config is the
intended first CI run after those changes; it validates compilation only.

## 5. Building the iOS root image (not run)

Unchanged from `ios-sweet` (`tools/mkiosroot.sh` with the iPhone X IPSW). The
J8 port must not redistribute Apple firmware, IPSWs, disk images or extracted
system files. `.gitignore` excludes `research/`, `backup/`, `logs/`, `xnu/out*`
and similar; verify with `git status --ignored` before committing.

## 6. Artifact hygiene

Committed: source, patches, docs, tool sources, small reproducible test vectors.
Not committed: IPSWs, `*.dmg`, `*.img` roots, vendor firmware (`a530_*.fw`,
`wcnss.*`, `a506_zap.*`), partition dumps, boot images from the phone, logs with
serial numbers/MACs, credentials.

## 7. Checks before pushing

```sh
python3 tools/validate-repo.py          # upstream repo checks, if applicable
python3 tools/j8/mkbootv0.py selftest <known-good v0 image>
python3 -m py_compile tools/j8/mkbootv0.py
git status --short --ignored | grep -v '^!!'   # review every staged path
```

`tools/j8/j8-compat-check.sh` is a device-side check, not a host check.
