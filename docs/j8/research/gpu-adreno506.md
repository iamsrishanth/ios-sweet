# Task C — Adreno 506 GPU acceleration: what can be reused from the Pixel 1 port?

Target: SM-J810G (j8y18lte, msm8953 / Snapdragon 450), Adreno 506, iOS 16.7.16 userland on XNU 8796.
Tags: [documented] = cited source; [local] = file in this workspace; [brief] = stated in the task brief but not re-measured; [inferred]; [unverified].
Log reality check: the supplied boot dmesg starts at kernel time 30.944 s and contains only two KGSL lines ("Trying to detach context 3/4 multiple times") — no kgsl probe, SMMU, zap or firmware-load messages [local: repo/evidence/2026-10-30-first-boot/dmesg-boot.txt]. It neither confirms nor denies that the shipped Android kernel brings the GPU up; the statements about the shipped stack below come from the device's own kernel source, not from the log.

## 1. Which GPU this is, and what freedreno supports

1. Mainline msm8953 describes gpu@1c00000 as compatible "qcom,adreno-506.0", reg 0x01c00000 + 0x40000, IRQ GIC SPI 33, power-domain OXILI_GX_GDSC, iommus gpu_iommu@1c48000 [documented: 3]. The pinned Samsung tree says the same downstream: qcom,kgsl-3d0@1c00000, chipid 0x05000600, IRQ 33, vddcx = gdsc_oxili_cx, vdd = gdsc_oxili_gx, qcom,highest-bank-bit = <14> [local: 7].
2. freedreno knows 5xx variants 505, 506, 508, 509, 510, 512, 530, 540 — there is no separate a506 driver: 506 is "A5xx" and only device-info parameters differ [documented: 2]. Turnip (Vulkan) is a6xx+ and the Mesa docs state there are no plans to port it to a5xx [documented: 1].
3. Device-info, a506 vs a530 [documented: 2]:

| Parameter | a506 | a530 | Consequence for the port |
|---|---|---|---|
| Generation | CHIP.A5XX | CHIP.A5XX | same command stream, registers, ir3 ISA |
| num_sp_cores | 1 | 4 | a506 is a "1-SP GPU"; occupancy/threadsize differ [documented: 4] |
| highest_bank_bit | 14 | 15 | equals the J8 dtsi value [local: 7]; affects CMDBUF/IB addressing |
| reg_size_vec4 | 64 | 64 | no change |
| wave_granularity | 1 | 2 | a506 register-allocation granularity |

4. GLES level: the freedreno driver as a whole reaches "up to OpenGL ES 3.2 and desktop OpenGL 4.5" [documented: 1]; that ceiling is a6xx+, and a5xx stops at GLES 3.1. Evidence: the sailfish port creates a GLES 3.1 context (.major=3, .minor=1) and reports it working [local: work/refs/ios-sailfish/tools/sweetgl/sweetgl.c, xnu/patches/0119-0164]; treat "a5xx = GLES 3.1" as [inferred] from those two facts (no single Mesa line was found that prints it). This is a regression against the shipped blob, which advertises GLES 3.2 on the same silicon [local: repo/docs/device.md §2]; CoreAnimation's GL renderer needs only GLES 2.0/3.0-class features, so it is not a blocker.
5. a506 vs a530 in programming — same map, different init values [documented: 4, local: 8]:

| Area | a506 | a530 | Source |
|---|---|---|---|
| HW clock-gating table | a50x_hwcg | a5xx_hwcg | 4 |
| CP_ME_INIT workaround word | 0x0B | 0x0B (a510 differs) | 4 |
| CP_MEQ_THRESHOLDS / MERCIU_SIZE | 0x20 / 0x400 | 0x40 / 0x40 | 4 |
| CP_ROQ_THRESHOLDS_2 / _1 | 0x40000030 / 0x20100D0A | 0x80000060 / 0x40201B16 | 4 |
| PC_DBG_ECO_CNTL | (0x100<<11 \| 0x100<<22) | (0x400<<11 \| 0x300<<22) | 4 |
| RB_DBG_ECO_CNTL bit 9 | cleared (1-SP DP2 gating) | untouched | 4 |
| VBIF XIN halt mask (ports) | 0x7 (3) | 0xf (4) | 4 |
| GMEM | 128K + 8K = 136 KiB | 1 MiB | local: 6 |
| GPMU block | absent (no ADRENO_GPMU) | present, a530_gpmu.fw2 | local: 6 |
| Zap resume call | skipped (ADRENO_CPZ_RETENTION) | SCM resume | local: 8 |
| Clock corners | 133.3/216/320/400/510/560/650 MHz in the pinned dtsi | 624 MHz top corner (8996 Pro) | local: 9 |

   The brief says "600 MHz max" while the pinned dtsi's top gfxfreq-corner is 650 MHz [local: 9]; which the shipped board uses is [unverified] and irrelevant to bring-up at 320-400 MHz.

## 2. Firmware: what an a506 actually loads, and the zap shader

The phone's /vendor/firmware reportedly holds a530_pfp.fw, a530_pm4.fw, a530_gpmu.fw2, a530v1_pfp.fw, a530v1_pm4.fw, a530v2_seq.fw2, a530v3_gpmu.fw2 [brief]. Against the J8's own kgsl GPU table [local: 6]:

| File | Loaded on this device? | Why |
|---|---|---|
| a530_pfp.fw, a530_pm4.fw | yes | the A506 entry uses exactly these two names; no a506_* filename appears anywhere in this kernel's GPU table |
| a530v1_pfp.fw, a530v1_pm4.fw | no | ADRENO_REV_A530 rev 0 only (1 MiB GMEM, no GPMU) |
| a530_gpmu.fw2, a530v2_seq.fw2 | no | a530 rev 1 only; the a506 entry has no ADRENO_GPMU and no gpmufw_name |
| a530v3_gpmu.fw2 | no | a530 patchid ANY (which pairs with a530v3_seq.fw2, itself absent from the list) |

So the firmware story is cheap: two files, both already on the phone, and the sailfish step of stripping the 4-byte header and packing them as GPU-PM4/GPU-PFP blobs transfers unchanged [local: ios-sailfish README step 4, patch 0120].

The zap shader is required and is the one firmware item that is not in that list:

- The pinned msm8953-gpu.dtsi declares pil_gpu as "qcom,pil-tz-generic", qcom,pas-id = <13>, qcom,firmware-name = "a506_zap", memory-region gpu_mem [local: 7]; the A506 gpucore carries .zap_name = "a506_zap" [local: 6] and kgsl loads it with subsystem_get(), i.e. the PIL path that pulls a506_zap.mdt + segments from /vendor/firmware and authenticates through TZ [local: 8]. Mainline needs the same shader on msm8953 (zap-shader child node, region at 0x81800000) [documented: 3].
- Its presence and exact name on this phone are [unverified]: no a506_zap.* appears among the seven files in the brief, so either the list is partial or the file is elsewhere. Answer this before any GPU work — sailfish proved the GPU starts in secure mode and that writing RBBM_SECVID_TRUST_CNTL = 0 without a zap shader reset the phone (patch 0125 exists because of it).
- a506 simplification: ADRENO_CPZ_RETENTION skips the SCM resume (SCM_SVC_BOOT fn 0xA, arg 13) that a530 needs on each restart [local: 8]. Load once, then leave it alone.
- Placement differs: msm8996 uses a fixed 1 MiB carveout at 0x90f00000 (patch 0130); msm8953's gpu_mem is a reusable 8 MiB shared-dma-pool anywhere in 0x80000000-0x90000000, 4 MiB aligned [local: 7]. XNU must reserve such a region itself instead of hardcoding an address.

## 3. What the sailfish GPU work is, and how much is 8996-only

The Pixel stack is patches 0119-0164 [local] in three layers: a kernel bring-up driver (bsd/dev/sweet_gpu.c), a user client (iokit/Kernel/arm/SweetGPU.cpp, patch 0138, selectors 10-16), and userspace — Mesa gallium freedreno as sweetgl.dylib over a libdrm/msm emulation (tools/sweetgl/sweet_drm.c) plus an EAGL shim (eagl.c) so CoreAnimation's own GL renderer uses it.

MSM8996-specific, must be rewritten:

- Power rail: PMI8994 S2 (ppid 0x317) at 745/825/1015 mV, gfx_mem_acc at 0x7af000, plus an RPM MX corner vote above 510 MHz (patches 0119, 0144, 0164). The J8's GPU rail is the RPM regulator gfx_vreg_corner attached to gcc-gfx-8953 [local: 9]; the J8 GPU dtsi has no mem-acc step.
- Clocks: 8996 MMCC at 0x8c0000 — GPU_GX_GDSC 0x4024, GPU_GDSC 0x4034, clamp 0x4300, gfx3d RCG 0x4000 with GPLL0 parents, rbbmtimer RCG 0x4090 (patch 0119). msm8953 has no MMCC: the clock controller is GCC at 0x1800000 ("qcom,gcc-gfx-8953"), GDSCs at 0x185901c (oxili_gx) and 0x185904c (oxili_cx) [local: 9, 10]. Only the GDSC idiom (clear SW_COLLAPSE, poll bit 31) transfers; every address, parent and divider does not.
- GPMU: patch 0127 powers RBCCU/SP through 0xa887/0xa881 and polls bit 20 of 0xa88d/0xa88b. a506 has no GPMU; downstream kgsl's non-a530 path is different — write RBBM_CLOCK_CNTL 0x55, enable HWCG, then clear the low byte of RBBM_CLOCK_CNTL [local: 8]. Replace, do not retarget.
- SMMU: patch 0121 identity-maps an ARM-SMMUv2-style block at 0xb40000 with GR0 at +0x0, CB at +0x8000, 1 GiB blocks. The J8 uses kgsl-iommu@1c40000 ("qcom,kgsl-smmu-v2"), 0x1c40000 + 0x10000, qcom,protect = <0x40000 0x10000>, qcom,micro-mmu-control = <0x6000>, gfx3d_user CB with qcom,gpu-offset = <0x48000>, plus a second gfx3d_secure CB (id 2) [local: 7, 11]. Same architecture, different base, different CB offset, an extra micro-MMU register, and a secure CB the Pixel code never touches.
- Zap: patch 0130's TZ PAS 13 and fixed 0x90f00000; see §2 for the 8953 variant.
- Panel/present: patches 0164/0167/0168 scale iPhone-8 750x1334 to the Pixel's 1080x1920 panel and drive AppleCLCD/MDP. The J8 is a native 720x1480 18.5:9 Samsung AMOLED behind MDSS/DSI: different scale factors, different display driver, and the UI aspect ratio no longer matches.

Generic a5xx, directly reusable:

- CP bring-up: CP_PFP/CP_PM4 instruction base, a5xx_me_init's CP_ME_INIT, 16 KiB ring, CP_MEM_WRITE/CP_EVENT_WRITE CACHE_FLUSH_TS fences and scratch verification (patch 0120). Only the MEQ/MERCIU/ROQ/DBG_ECO constants differ (§1.5).
- Secure-mode semantics of RBBM_SECVID_TRUST_CNTL (patch 0125) — an a5xx property, same on 506.
- 2D blit engine: RB copy and BLIT_OP_SCALE packets (patch 0126), used for CPU-checked copies and for the panel scale.
- Shader path: hand-written a5xx ir3 assembly (tools/gpu-shaders, tools/ir3asm), compute dispatch via SP_CS_*/HLSQ_* after Mesa's computerator a5xx backend (patch 0131), the blend kernel (0133) and textured quads (0136/0141/0149). ir3 is common to all a5xx; only 1-SP wave/thread geometry needs re-tuning and re-verification.
- VA space and submission: 4 KiB SMMU tables under 16 KiB-page allocation, per-mapping guard page, softpin IOVA, and the GEM_NEW/SUBMIT/FENCE/CPU_PREP emulation of msm UAPI 1.7 (patches 0137, 0150, 0151, sweet_drm.c). The design transfers; page arithmetic and allocator sizes do not.
- Screen setup: fd_drm_screen_create_renderonly with FD_MESA_DEBUG=sysmem and IR3_SHADER_DEBUG=nofp16 (sweetgl.c). sysmem-only matters more on 1 SP: it never uses GMEM, sidestepping a506's 136 KiB.
- One hardcoded value to fix: sweet_drm.c answers MSM_PARAM_GPU_ID with 530 ("p->value = 530"), which would make freedreno pick a530 device info (4 SP, bank bit 15). It must return 506, and MSM_PARAM_GMEM_SIZE/CHIP_ID/MAX_FREQ/TIMESTAMP must come from the real a506 [local: sweet_drm.c].

## 4. Minimum J8 GPU bring-up sequence

Each step is a go/no-go checkpoint; do not start the next before the previous one reads back.

1. Power/clock (boot-arg gated kernel thread, as patch 0119): enable vddcx (gdsc_oxili_cx, GCC+0x185904c), then vdd (gdsc_oxili_gx, GCC+0x185901c) after releasing any GX clamp; set gfx3d_clk_src through gcc-gfx-8953 to the 320 MHz corner; enable oxili_ahb, bimc_gfx, bimc_gpu, rbbmtimer, alwayson; vote gfx_vreg_corner through the RPM at that corner's voltage. Check: RBBM_VERSION/VBIF_VERSION readable, RBBM_ALWAYSON_COUNTER counting.
2. GPU SMMU: take gfx3d_user (gpu-offset 0x48000) out of bypass, program micro-MMU control at +0x6000, identity-map the kernel's low DRAM with 1 GiB blocks for the CP's early fetches, TLB-flush, verify a fault-free read. Leave gfx3d_secure alone.
3. Firmware: load a530_pfp.fw and a530_pm4.fw (4-byte header stripped) into GPU-visible memory and program CP_PFP/CP_PM4 instruction base.
4. Zap: obtain a506_zap.mdt (+ segments), place it in an 8 MiB, 4 MiB-aligned carveout inside 0x80000000-0x90000000, authenticate through TZ with PAS 13 as the PIL does, skip the SCM resume (CPZ retention). Check: GPU leaves secure mode, RBBM_SECVID_TRUST_CNTL reads non-secure.
5. CP init: a5xx_me_init with a506 values (CP_ME_INIT 0x2F / 3 / 0x20000000 / 0 / 0 / 0x0B / 0 / 0), MEQ/MERCIU/ROQ from §1.5, then the rest of a5xx_hw_init minus GPMU (HWCG, VBIF RR/QOS, UCHE GMEM range 0x100000..0x100000+136K-1). Check: CP_MEM_WRITE sentinel lands, CP_WAIT_FOR_IDLE retires.
6. Ring/IB: 16 KiB ring, IBs at BO VAs, CACHE_FLUSH_TS fences; a CPU-checked 2D blit proves the memory path before any shader runs.
7. VA space and BOs: page-granular GPU VA with guard pages, then the msm UAPI subset (GET_PARAM, GEM_NEW, GEM_INFO, GEM_SUBMIT, WAIT_FENCE, CPU_PREP) behind the IOKit user client (SweetGPU.cpp selectors 10-16 unchanged).
8. Shaders: compute kernel (SSBO write, CPU verify), then textured quad and blend; re-tune thread geometry for 1 SP.
9. User space: build Mesa freedreno against the libdrm shim, fix GPU_ID to 506 and GMEM to the real value, run the sweetgl selftest (offscreen triangle readback).
10. Present: GPU bilinear scale from the iOS framebuffer geometry into double-buffered 720x1480 panel memory, flip on vsync, then hand the GLES surface to CoreAnimation via the EAGL shim. This needs the J8 display path (MDSS/DSI, command mode) to exist first — separate work item.

Kernel services XNU needs beyond the Pixel port: mmio mapping (ml_io_map), GIC SPI 33, an RPM client (sweet_rpm.c is the right shape; resource IDs differ), a new GCC-gfx clock/GDSC driver (the Pixel's direct MMCC pokes do not apply), an SMMUv2 context-bank driver for the kgsl layout, a TZ/PIL caller for the zap image, and the IOKit user client. User space: Mesa freedreno (gallium + ir3) behind sweetgl, the libdrm/msm emulation, the EAGL shim — the sailfish shape with a506 parameters.

## 5. Effort estimate

| Workstream | Engineer-weeks | Reasoning |
|---|---|---|
| 8953 power/clock/regulator/GDSC | 3 | new GCC-gfx clock and GDSC drivers plus RPM regulator vote; no reusable code in either port |
| GPU SMMU (kgsl-smmu-v2) + VA space | 2 | base, CB offset and micro-MMU all differ, and a secure CB exists |
| CP init with a506 values + ring/IB | 2 | transcription from a5xx_hw_init's a506 branches, but every hang costs a reboot on a phone with a dead power button |
| Zap via TZ/PIL | 1-2 | mechanism proven on Pixel; new carveout, new image, file presence unknown |
| BO/submit UAPI + user client | 2 | design done; page-size and API-surface rework |
| Mesa bring-up on a506 (1 SP, bank bit 14, GPU_ID/GMEM) | 2-3 | freedreno lists 506 but its device info is marked "FIXME: measure on the hardware" [documented: 2] |
| EAGL/CoreAnimation integration + present | 3-4 | present depends on the J8 display path; scale factors and vsync differ |
| Debugging hangs, SMMU faults, tuning | 3-4 | the dominant cost on sailfish; a dead power button and no fastboot raise it |
| Total | 15-21 (16 typical) | one experienced engineer; split kernel vs userspace across two: 10-13 calendar weeks |

Confidence: the mechanism is proven on another a5xx core, so this is retargeting, not research. Three things can break the estimate: a506_zap being absent from the phone; the 8953 rail/clock sequencing differing from both the dtsi and the vendor kernel in ways only hardware shows; the display path not being ready to receive frames.

## 6. What "GLES working" does not imply

- Vulkan: Turnip is a6xx+ and the Mesa docs say there are no plans to port it to a5xx [documented: 1]. Anything asking for Metal gets nothing — the sailfish README lists "No Metal" and says CoreAnimation falls back to its GL renderer, which is what actually runs [local: ios-sailfish README]. A working GLES driver creates no Metal driver.
- Hardware video decode: sailfish has none (HEIC and video do not decode; wallpapers are pre-converted) [local: ios-sailfish README]. Venus on msm8953 is a separate firmware/SMMU bring-up, out of scope here.
- DRM/FairPlay: no Widevine, PlayReady or FairPlay path. FairPlay and protected media rely on Apple's SEP/TEE integration, which these ports do not have (no SEP, stub AppleKeyStore, security off). The J8's shipped Widevine blobs are 32-bit Android components [local: repo/docs/device.md §2] and are irrelevant to an XNU userland.
- Also not implied: GMEM-tiled rendering (this design is sysmem only), UBWC, headroom typical of a multi-SP GPU, thermal sustainability (the port has no thermal management), camera/ARKit-class GPU features.

## 7. Reuse table

| Component | Sailfish implementation | Reusable for a506? | What must change | Evidence |
|---|---|---|---|---|
| GFX rail / voltage | PMI8994 S2 ppid 0x317, 745-1015 mV, mem-acc corner | no | RPM regulator gfx_vreg_corner + gcc-gfx-8953 corner table; drop mem-acc | local: 0119/0144/0164; local: 9 |
| GPU clocks + GDSC | MMCC 0x8c0000 RCG/branch pokes, GPLL0 parents, GX 0x4024 / CX 0x4034, clamp 0x4300 | idiom only | GCC 0x1800000, gfx3d_clk_src + oxili clocks, GX 0x185901c / CX 0x185904c, CX before GX | local: 0119; local: 9, 10 |
| GPMU RBCCU/SP power | 0xa887/0xa881, poll 0xa88d/0xa88b bit 20 | no | a506 has no GPMU; use RBBM_CLOCK_CNTL 0x55 + HWCG path | local: 0127; local: 8 |
| GPU SMMU | SMMUv2 at 0xb40000, CB +0x8000, identity 1 GiB blocks | partly | base 0x1c40000, CB 0x48000, micro-MMU 0x6000, secure CB id 2 | local: 0121; local: 7/11 |
| CP microcode | a530_pfp.fw / a530_pm4.fw, header stripped, payload blobs | yes, same files | only the load path (payload vs /vendor) | local: README/0120; local: 6 |
| Zap shader | a530_zap.mdt/.b02 via TZ PAS 13 into 0x90f00000 | mechanism yes | a506_zap image, 8 MiB 4 MiB-aligned carveout, no SCM resume | local: 0130; local: 7/8 |
| CP init values | A530 subset of a5xx_hw_init | partly | a506 MEQ/MERCIU/ROQ/DBG_ECO/HWCG values | local: 0120; documented: 4 |
| Blit / shader path | RB copy + BLIT_OP_SCALE, SP_CS_* compute, ir3 quad and blend kernels | yes | verify offsets on 506; 1-SP thread geometry; nofp16 | local: 0126/0131/0133/0136/0141/0149 |
| GPU VA space + UAPI shim | L1/L2/L3 4K tables, guard pages, softpin, selectors 10-16 | yes | page arithmetic; GPU_ID 506, real GMEM 136 KiB, real chip id/freq | local: 0137/0150, sweet_drm.c; local: 6 |
| Mesa profile | gallium freedreno, FD_MESA_DEBUG=sysmem, IR3 nofp16 | yes | a506 device-info path (1 SP, bank bit 14) | local: sweetgl.c/mesa.patch; documented: 2 |
| EAGL / CoreAnimation | eagl.c shim + surfaceless st contexts | yes | nothing (GPU-agnostic) | local: sweetgl.c/eagl.c |
| Present / panel | GPU bilinear scale to 1080x1920, AppleCLCD, 600 MHz | concept only | 720x1480 AMOLED, MDSS/DSI, different scale, vsync | local: 0164/0167; local: repo/docs/device.md §2 |

## Sources

1. Mesa — Freedreno driver documentation. https://docs.mesa3d.org/drivers/freedreno.html (accessed 2026-11-17)
2. Mesa — src/freedreno/common/freedreno_devices.py. https://gitlab.freedesktop.org/mesa/mesa/-/raw/main/src/freedreno/common/freedreno_devices.py (accessed 2026-11-17)
3. Linux mainline — arch/arm64/boot/dts/qcom/msm8953.dtsi. https://raw.githubusercontent.com/torvalds/linux/master/arch/arm64/boot/dts/qcom/msm8953.dtsi (accessed 2026-11-17)
4. Linux mainline — drivers/gpu/drm/msm/adreno/a5xx_gpu.c. https://raw.githubusercontent.com/torvalds/linux/master/drivers/gpu/drm/msm/adreno/a5xx_gpu.c (accessed 2026-11-17)
5. Linux mainline — drivers/gpu/drm/msm/adreno/adreno_gpu.c (zap firmware-name handling). https://raw.githubusercontent.com/torvalds/linux/master/drivers/gpu/drm/msm/adreno/adreno_gpu.c (accessed 2026-11-17)
6. goldfish07/android_kernel_samsung_j8y18lte, pin 3a7e80328b108097e984c028c0fd1f9e65678e31 — drivers/gpu/msm/adreno-gpulist.h (A506/A530 firmware, GMEM, feature flags). https://raw.githubusercontent.com/goldfish07/android_kernel_samsung_j8y18lte/3a7e80328b108097e984c028c0fd1f9e65678e31/drivers/gpu/msm/adreno-gpulist.h (accessed 2026-11-17)
7. Same pin — arch/arm/boot/dts/qcom/msm8953-gpu.dtsi (kgsl node, pil_gpu a506_zap, kgsl-smmu-v2). https://raw.githubusercontent.com/goldfish07/android_kernel_samsung_j8y18lte/3a7e80328b108097e984c028c0fd1f9e65678e31/arch/arm/boot/dts/qcom/msm8953-gpu.dtsi (accessed 2026-11-17)
8. Same pin — drivers/gpu/msm/adreno_a5xx.c (regulator/GPMU and zap paths). https://raw.githubusercontent.com/goldfish07/android_kernel_samsung_j8y18lte/3a7e80328b108097e984c028c0fd1f9e65678e31/drivers/gpu/msm/adreno_a5xx.c (accessed 2026-11-17)
9. Same pin — arch/arm/boot/dts/qcom/msm8953.dtsi (gcc-gfx-8953, gfxfreq-corner, GDSC hooks). https://raw.githubusercontent.com/goldfish07/android_kernel_samsung_j8y18lte/3a7e80328b108097e984c028c0fd1f9e65678e31/arch/arm/boot/dts/qcom/msm8953.dtsi (accessed 2026-11-17)
10. Same pin — arch/arm/boot/dts/qcom/msm-gdsc-8916.dtsi (gdsc_oxili_gx 0x185901c, gdsc_oxili_cx 0x185904c). https://raw.githubusercontent.com/goldfish07/android_kernel_samsung_j8y18lte/3a7e80328b108097e984c028c0fd1f9e65678e31/arch/arm/boot/dts/qcom/msm-gdsc-8916.dtsi (accessed 2026-11-17)
11. Same pin — drivers/gpu/msm/kgsl_iommu.c (qcom,gpu-offset, qcom,micro-mmu-control, kgsl-smmu-v2). https://raw.githubusercontent.com/goldfish07/android_kernel_samsung_j8y18lte/3a7e80328b108097e984c028c0fd1f9e65678e31/drivers/gpu/msm/kgsl_iommu.c (accessed 2026-11-17)
