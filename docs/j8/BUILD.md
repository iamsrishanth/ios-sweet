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
| Kernel CI runner | GitHub **`macos-15-intel`**, Xcode 16.x (the pinned `macos-14` image was retired on 2026-11-02 and its jobs are cancelled before a runner is assigned) | workflow |
| iOS userland | iPhone X (`iPhone10,3` / D22AP) iOS 16.7.16 build `20H392` | user-supplied IPSW, **not in git** |
| Probe toolchain | Zig (pip `ziglang`) or `aarch64-linux-gnu-gcc`; Python 3.11 | host |

A different macOS/Xcode/KDK combination is not guaranteed to produce the same
kernel; the reference project's CI exists because those three are coupled (see
the `iig`/IORPC comment in the workflow). Moving from macos-14 (Xcode 15) to
macos-15 (Xcode 16+) changes `iig`: it emits `IORPC.kernelContent`, which
xnu-8796 does not have. `xnu/patches/0083-iig-kernelcontent.patch` backports the
field (as in current xnu) and is **prepared, not yet shown to be sufficient**;
the first macos-15 run will tell.

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

## 4. Building the kernel (verified for J8: compilation and link only)

The workflow is the upstream one, with `machine_config=J8`:

```sh
gh workflow run xnu.yml -R iamsrishanth/ios-sweet --ref samsung-j8-bringup \
  -f macos_version=13.5 -f kernel_config=DEVELOPMENT -f machine_config=J8
gh run download <RUN_ID> -R iamsrishanth/ios-sweet -n xnu-kernel -D work/j8-kernel
```

Verified runs: **44028918918** (first successful link, no interrupt support)
and **44033283321** (with the GICv2 driver, 2026-11-17 06:54 UTC), both
`conclusion=success`; the current artifact is from the second:

| Field | Value |
|---|---|
| File | `kernel.development.j8` |
| Size | 16,529,184 bytes (15.76 MiB) |
| SHA-256 | `f73a0b19dfb90837909fd67b243642c48116f5323ae91e4c3b6489696aa06e0b` |
| Mach-O | `cputype 0x0100000c` (ARM64), `cpusubtype 0` (plain arm64, **not arm64e**), `MH_EXECUTE` |
| Toolchain | GitHub `macos-15-intel`, Xcode 16 (MacOSX15.0 SDK), KDK 13.5 build `22G74` installed by the harness |
| Config | `config/MASTER.arm64.j8`, `-DARM64_BOARD_CONFIG_J8 -march=armv8-a+crypto`, no `__ARM_16K_PG__` |

This proves the 4 KiB-granule machine config **compiles and links** against
xnu-8796.101.5 with the ios-sweet component set. It does **not** prove the
kernel boots: GICv2 interrupt delivery is absent (`pexpert/arm/pe_fiq.c` has
only the GICv3 path), and there are no J8 storage, display, touch or USB
drivers, no J8 hardware boot image and no J8 device tree. The build is a
compile milestone, not a bootable artifact.

The shim now has an explicitly selected `J8_4K` geometry (`PG_SHIFT=12`, 2 MiB
blocks, 4 KiB TG0/TG1, and 4 KiB payload placement). It is source-validated but
not yet cross-compiled in this Windows workspace because the repository's
Linux shim build requires WSL/aarch64-linux-gnu tooling. The phone is Samsung
Download/Odin-only, not fastboot; therefore a hardware test would be a
persistent `boot` write, not a temporary boot.

The runs needed patches 0082-0087 (machine config; Xcode 16 `IORPC.kernelContent`
backport; iOS user-VA limit for the 4 KiB commpage; staging the ios-sweet
sources for J8; applying their per-object flags; two link stubs; and the GICv2
timer/SGI interrupt path). CI also had
to leave the retired `macos-14` image: the `macos-15` arm64 pool failed to
acquire a runner for 15 minutes, so `macos-15-intel` is used. The KDK glob and
`cancel-in-progress` handling were fixed so a job result means the build, not
the queue.

Still missing, in dependency order:

1. a shim that builds 4 KiB-granule tables (`shim/mmu.c`, `shim/shim.c` are
   16 KiB-only);
2. a GICv2 interrupt driver (distributor/CPU interface, timer PPI 19, SGIs)
   and the `sleh_irq` dispatch it feeds;
3. the J8 board devices (SDCC/eMMC, display, touch, keys) as separate patches;
4. a J8 ADT/device tree for the shim and QEMU harness.

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
