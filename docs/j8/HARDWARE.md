# J8 hardware inventory and XNU driver mapping

All addresses and interrupt numbers below were read from the **live device tree**
(`/proc/device-tree`, read-only `adb shell`) on 2026-11-17 unless marked otherwise.
The device is `SM-J810G` / `j8y18lte` / `msm8953` (Snapdragon 450), hardware
revision 4, bootloader `J810GDDU6CWH2`. Serial numbers are omitted deliberately.

This is a *hardware* map. It says what the silicon is and what a driver would
have to touch; it does not claim any of it works under XNU.

## 1. CPU, memory, interrupts

### 1.1 CPU

- 8 × Cortex-A53, ARM part `0xd03`, revision `r0p4`, ARMv8.0-A, no pointer
  authentication, no LSE atomics in the exposed feature set
  (`/proc/cpuinfo`: `half thumb fastmult vfp edsp neon vfpv3 tls vfpv4 idiva
  idivt lpae evtstrm aes pmull sha1 sha2 crc32`).
- Two CPU clusters: `cpu@0..3` (cluster 0) and `cpu@100..103` (cluster 1);
  `enable-method = "psci"`, PSCI 1.0 via `smc`.
- Cache line 64 B (`CTR_EL0` is trapped at EL0; XNU board configs use
  `MMU_CLINE 6` for 64 B, matching A53).
- **Translation granules:** 4 KiB page tables are what the installed kernel
  uses (`CONFIG_ARM64_4K_PAGES=y`). Whether the core exposes the optional 16 KiB
  granule (`ID_AA64MMFR0_EL1.TGran16`) is **unverified on this unit**: EL0 reads
  of ID registers are trapped, and the EL1 probe prepared in
  `tools/j8/granule-probe-el1.S` has not been run. See `FEASIBILITY.md` §2.

### 1.2 Memory

| Region | Base | Size | Notes |
|---|---|---|---|
| DDR bank 0 | `0x8000_0000` | 2 GiB | contains all firmware carve-outs |
| DDR bank 1 | `0x1_0000_0000` | 1.75 GiB | above 4 GiB; the natural place for an XNU payload |

Reserved carve-outs declared in the live DT (base / size):

| Node | Base | Size |
|---|---|---|
| `splash_region` | `0x9000_1000` | ~31.7 MiB (framebuffer/splash) |
| `dfps_data_mem` | `0x9000_0000` | 1 MiB |
| `ramoops` | `0x8610_0000` | 1 MiB |
| `other_ext_region` | `0x8630_0000` | 5 MiB |
| `modem_region` | `0x86c0_0000` | ~106 MiB |
| `wcnss_fw_region` | `0x8ee0_0000` | 7 MiB |
| `adsp_fw_region` | `0x8d60_0000` | 24 MiB |
| `sec_debug_low_region` | `0x8500_0000` | 4 MiB |
| `tima_region` | `0x85b0_0000` | 2 MiB |
| `ss_plog` | `0x85d0_0000` | 4 MiB |
| `venus_region` | `0x8200_0000` | 5 MiB |
| `tzapp_region` | `0x8280_0000` | 40 MiB |

`gpu_region`, `secure_region`, `qseecom_region` and `adsp_region` exist as nodes
without a static `reg` (dynamic/carveout). A port must not touch any of these;
the reference ports run from the upper bank for exactly this reason.

### 1.3 Interrupt controller and timers

- **GICv2**, compatible `qcom,msm-qgic2`: distributor `0x0b00_0000` (4 KiB),
  CPU interface `0x0b00_2000` (4 KiB). No GICv3 system registers, no
  redistributors. This is a **structural difference** from both reference ports:
  `ios-sweet` (SM7150) and `ios-sailfish` (MSM8996) implement GICv3
  (`ICC_*_EL1`, `GICR_*`). XNU's legacy GICv2 path exists (used by the
  Raspberry Pi 3 `BCM2837` board config) but has never been used on a phone.
- Architected timer: `CNTV` is expected as PPI 3 → INTID 19, the Qualcomm
  numbering also used by the SM7150 port (`SWEET_VTIMER_INTID 19`). **Unverified
  on this unit**; the DT `timer` node carries no `interrupts` property.
- `timer@b120000` is the Qualcomm timer block, not the architected timer.
- IPIs: GIC SGIs (0–15); the SM7150 port uses SGI 1.

## 2. Storage

- **eMMC** (not UFS), 61,071,360 KiB, single `mmcblk0` on `sdhci@7824900`:
  `reg = <0x07824900 0x500>, <0x07824000 0x800>, <0x07824e00 0x200>,
  <0x0119d000 0x400>`, SPI 123 and 138; SD card on `sdhci@7864900` (SPI 1, 2).
- The reference ports' `sweet_ufs.c` (UniPro/UFS) and its `rd=ufsroot0` boot
  argument are **not usable**. A J8 port needs an SDCC/eMMC host driver
  (ADMA2 or SDMA, CMD/DATA queues, card init, RPM clock votes for `iface_clk`
  and `core_clk`) and a block device for the XNU buffer cache.
- Partition map (live `/proc/partitions` + `/dev/block/by-name`), sizes in KiB:

| # | Name | Size | # | Name | Size |
|---|---|---|---|---|---|
| p1 | `sbl1` | 512 | p24 | `misc` | 1024 |
| p2 | `sbl1bak` | 512 | p25 | `boot` | 32768 |
| p3 | `ddr` | 512 | p26 | `recovery` | 32768 |
| p4 | `rpm` | 512 | p27 | `dtbo` | 2048 |
| p5 | `tz` | 2048 | p28 | `bota` | 8192 |
| p6 | `aboot` | 2048 | p29 | `fota` | 4096 |
| p7 | `sec` | 256 | p30 | `nad_refer` | 6144 |
| p8 | `lksecapp` | 512 | p31 | `persist` | 32768 |
| p9 | `keymaster` | 512 | p32 | `persistent` | 1024 |
| p10 | `cmnlib` | 256 | p33 | `steady` | 4096 |
| p11 | `fsg` | 4096 | p34 | `keystore` | 1024 |
| p12 | `cmnlib64` | 256 | p35 | `config` | 1024 |
| p13 | `pad` | 256 | p36 | `dsp` | 16384 |
| p14 | `modemst1` | 4096 | p37 | `modem` | 98304 |
| p15 | `modemst2` | 4096 | p38 | `apnhlos` | 36864 |
| p16 | `efs` | 16384 | p39 | `debug` | 8192 |
| p17 | `fsc` | 3328 | p40 | `system` | 3317760 |
| p18 | `ssd` | 256 | p41 | `vendor` | 491520 |
| p19 | `devcfg` | 256 | p42 | `odm` | 409600 |
| p20 | `apdp` | 128 | p43 | `cache` | 307200 |
| p21 | `msadp` | 128 | p44 | `hidden` | 102400 |
| p22 | `param` | 12288 | p45 | `omr` | 20480 |
| p23 | `backup` | 4096 | p46 | `userdata` | 56070124 |

`mmcblk0rpmb` (16384 KiB) exists and must not be written.

**Never overwrite:** `sbl1`, `sbl1bak`, `tz`, `rpm`, `aboot`, `sec`, `lksecapp`,
`keymaster`, `cmnlib`, `cmnlib64`, `devcfg`, `apdp`, `msadp`, `fsg`, `fsc`,
`modemst1`, `modemst2`, `efs`, `persist`, `modem`, `apnhlos`, `ssd`, `keystore`,
`ddr`, `pad`, `param`, `recovery`, `rpmb`. They carry the boot chain, radio
calibration, DRM/keystore state or the recovery this project depends on.

Usable for an iOS root without touching Android's system: `userdata` (53.5 GiB)
or, if Android is to be kept, a file-backed image inside it. The reference ports
use the `userdata` partition directly; the same approach is possible here but
**destroys Android user data** and has not been validated on this unit.

## 3. Display, touch, input

- Display: Samsung `S6E8AA5X01` AMS604RM01 HD panel, 720×1480, 60 Hz, DSI
  command mode, MDP5 at `qcom,mdss_mdp@1a00000` (`reg = <0x01a00000 0x90000>,
  <0x01ab0000 0x1040>`, SPI 72), DSI controllers at `0x1a94000` / `0x1a96000`.
  Bootloader leaves a continuous splash at `splash_region` (`0x90001000`).
- Touch: **Zinitix BT532** on `i2c@78b6000` (BLSP1 QUP1, 7-bit address `0x20`),
  interrupt GPIO + `tsp_ldo_en` fixed regulator, `zinitix,x_resolution`,
  `y_resolution`, `firmware_name`, `afe_base`, `page_size` properties present.
- Buttons: power and volume through the PMIC; the power **key** still works
  electrically (the handover's "dead power button" is a hardware/mechanical
  problem that affects boot-time key combos), volume keys work.
- No hardware home button, no capacitive keys: the J8 is a gesture-era Samsung
  with on-screen navigation.

## 4. Graphics

- Adreno 506 (`kgsl-3d0@1c00000`, `reg = <0x01c00000 0x40000>`, SPI 33),
  identified by the running driver as `Adreno506v1`, max 600 MHz.
- GPU IOMMU: `kgsl-iommu@1c40000` (64 KiB), compatible `qcom,kgsl-smmu-v2` —
  a **different SMMU programming model** from the MSM8996 port (also SMMUv2, but
  different instance layout and clocks).
- `qcom,kgsl-hyp` (`qcom,pil-tz-generic`) means the GPU is brought out of secure
  mode by a **signed zap shader loaded through TrustZone/PIL**, as on the Pixel
  port. Firmware present in `/vendor/firmware`: `a530v1_pfp.fw`,
  `a530v1_pm4.fw`, `a530_pfp.fw`, `a530_pm4.fw`, `a530_gpmu.fw2`,
  `a530v2_seq.fw2`, `a530v3_gpmu.fw2`. Which subset an Adreno 506 loads, and
  whether a zap image is present at all, is unresolved (see
  `docs/j8/research/gpu-adreno506.md`).

## 5. Connectivity

- **Wi-Fi:** integrated Qualcomm WCNSS (`qcom,wcnss-wlan@0a000000`) with the
  prima/qcacld driver and firmware under `/vendor/firmware/wlan/prima`. This is
  **not** a QCA6174 PCIe device: there is no PCIe WLAN node, and the boot log
  shows the qcacld `VosMC` WMI traffic. The Pixel port's PCIe/HTC/WMI driver
  cannot be reused unchanged; the 8953 path is SNOC + copy engines + PIL-loaded
  Riva firmware.
- **Bluetooth:** no `hci0` in the recorded logs (BT was off). msm8953 platforms
  normally share the WCNSS radio and run BT over a high-speed UART with a
  Qualcomm NVM/firmware download; the exact transport, GPIOs and NVM file for
  this board are **unresolved**.
- **Cellular:** separate Hexagon modem; data path through IPA
  (`qcom,ipa@07900000`, `reg = <0x07900000 0x4effc>, <0x07904000 0x26934>`,
  SPI 228/230), control through QMI over GLINK/SMD
  (`qcom,ipc_router_q6_xprt`, `qcom,smdtty`, `qcom,smdpkt`, `qcom,smp2p-*`),
  shared memory at `qcom,smem@86300000`. Modem firmware lives in `modem` /
  `apnhlos` partitions. iOS telephony expects a baseband through CommCenter's
  own transport abstraction; nothing in XNU speaks QMI.
- **Audio:** QDSP6 (`qcom,msm-dai-q6`), Samsung adaptation node
  (`samsung,q6audio-adaptation`), `qcom,msm-stub-codec`, NXP **TFA9896** smart
  amplifier firmware in `/vendor/firmware/Tfa9896.cnt`. Audio is a DSP firmware
  problem (ADSP image from `adsp_fw_region` + `adsp` partition), not a simple
  codec register sequence.
- **USB:** DWC3 `ssusb@7000000` + QUSB2 PHY `qusb@79000`; the SM7150 port has a
  working polled DWC3 device-mode driver that is a useful starting point, but
  the PHY/clock/reset wiring differs.

## 6. Power, charging, sensors, other

- Charger/MUIC: Samsung **SM5708** (`samsung,sm5708-charger`,
  `samsung,sec-battery`, fuel gauge registers visible in the boot log). This is
  Samsung-specific and has no counterpart in either reference port (PMI8994 /
  PM6150).
- Regulators/clocks: **RPM** (`qcom,rpm-smd`, `qcom,rpm-log@29fc00`,
  `qcom,rpm-stats@200000`), i.e. the older SMD/GLINK RPM interface. The Pixel
  port talks to the RPM (useful reference); the Redmi port talks to RPMh (not
  applicable).
- Sensor hub: `qcom,msm-ssc-sensors` (SLPI/SSC) — its own firmware and QMI
  service; not required for first light but required for rotation/proximity.
- Fingerprint: not identified in the DT dump; likely a separate SPI device.
  Unresolved.
- microSD: `sdhci@7864900`, present.
- NFC: `ro.hardware.nfc_nci=nqx.default` (NXP NCI); no DT node captured.

## 7. XNU driver mapping

Status vocabulary: **reuse** (drop-in with parameter changes), **adapt** (same
hardware class, different registers/sequence), **new** (no reference code).

| Function | J8 hardware | Reference code | Verdict | Notes |
|---|---|---|---|---|
| Boot handoff | ABL → `boot.img` v0, DTB in x0 | shim (`shim/*.c`), `mkboot-v0.sh` | adapt | shim MMU is 16K-granule only; J8 needs a 4K-granule shim (`FEASIBILITY.md` §3) |
| CPU bring-up | PSCI 1.0 `smc`, 2 clusters × 4 A53 | patch 0065/0078 (SM7150 PSCI) | adapt | MPIDR cluster handling differs (Aff1 = 0/1) |
| Interrupts | **GICv2** `qcom,msm-qgic2` | GICv3 code in patches 0004/0079 | **new/adapt** | XNU GICv2 support exists only in the BCM2837 path |
| Timer | CNTV PPI 19, 19.2 MHz | patch 0017/0004 | adapt | Qualcomm PPI numbering |
| Page tables | 4K hardware granule, 16K software pages | pmap 4K attrs + `PAGE_RATIO` | adapt | see `FEASIBILITY.md` §2; untested configuration |
| Storage | eMMC via SDCC `7824900` | `sweet_ufs.c` (UFS) | **new** | biggest single driver task |
| Root filesystem | HFS+ in a partition | patches 0026–0031 | reuse | needs a block device first |
| Display | MDP5 + DSI command panel `S6E8AA5X01` | `SweetFramebuffer.cpp`, Pixel MDP path | adapt | different MDP generation and panel init |
| Touch | Zinitix BT532 over BLSP I²C | `sweet_touch.c` (Goodix GENI), Pixel RMI4 | **new** | I²C + GPIO IRQ + regulator |
| Buttons | PMIC keys | `sweet_keys.c` (SPMI PMIC) | adapt | different PMIC (SM5708/PM8953) |
| Battery/charging | SM5708 fuel gauge + charger | `sweet` PM6150 battery, Pixel PMI8994 | **new** | Samsung-specific |
| USB | DWC3 + QUSB2 | patches 0072–0077 (SM7150 DWC3) | adapt | device mode for logs; host mode not needed initially |
| GPU | Adreno 506, SMMUv2, PIL zap | patches 0119–0150 + `sweetgl` | adapt/new | see GPU note |
| Wi-Fi | WCNSS/prima (SNOC) | `sailfish_wlan.c` (QCA6174 PCIe) | **new** | different bus, firmware load and WMI path |
| Bluetooth | WCNSS BT over UART/HS (unresolved) | none | **new** | no reference implementation |
| Cellular | QMI/GLINK modem + IPA | none | **new** | no reference implementation; iOS telephony integration unresolved |
| Audio | QDSP6 + TFA9896 | none | **new** | no reference implementation |
| Sensors | SSC/SLPI | none | **new** | not needed for first light |
| Security | no SEP, TrustZone present | `sweet_amfi.c`, AKS stub | reuse | already permissive in the reference; see `STATUS.md` |

## 8. What is *not* claimed

- No driver in this table has been executed on the J8 under XNU.
- Addresses are from the **Android** device tree; they are hardware facts, not
  proof that XNU's mapping/clock/power sequences are correct.
- The absence of a DT node (fingerprint, NFC, BT transport) is a research gap,
  not evidence that the hardware is absent.
