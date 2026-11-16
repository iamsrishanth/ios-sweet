# XNU 8796 on arm64: does it support 4 KiB pages, and what does the iOS 16 userland need?

Task B of the SM-J810G (j8y18lte, msm8953, 8x Cortex-A53 r0p4) bring-up study.
Kernel under study: work/refs/xnu-8796, HEAD aca3beaa3dfbd42498b42c5e5ce20a938e6554e5, tag xnu-8796.101.5.
All XNU citations are file:line in that clone. Labels used throughout: documented (source cited), measured on this device (file cited), inferred (my reasoning over cited code), unverified (no evidence found). Nothing here was compiled; no build was run.

## 0. The one thing that changes the question

XNU arm64 has two independent page-size axes, and this report's whole answer hinges on keeping them apart:

- the translation granule (hardware page-table format), fixed at compile time by __ARM_16K_PG__ (documented: osfmk/arm64/proc_reg.h:1149-1155);
- the software page size (VM page), fixed at boot by PAGE_SHIFT_CONST (documented: osfmk/arm/arm_init.c:393-413).

A "4K build" of XNU is not a kernel with 4K VM pages. It is a 4K-granule kernel that still advertises 16K VM pages on any device with more than 1 GiB of RAM. On this phone (4 GB) the user-visible page size is 16384 in both configurations unless arm_init.c is patched. That is the single most load-bearing finding in this document.

## 1. How PAGE_SHIFT / PAGE_SIZE are actually selected

- osfmk/mach/arm/vm_param.h:59-74. For __arm64__ PAGE_SHIFT_CONST is declared extern int (line 62). PAGE_SHIFT is ARM_PGSHIFT only when KERNEL_PRIVATE && __ARM_16K_PG__ (line 67-68); otherwise PAGE_SHIFT is the runtime variable PAGE_SHIFT_CONST (line 70) and PAGE_SIZE is (1 << PAGE_SHIFT) (line 72). PAGE_MAX_SHIFT is 14, PAGE_MIN_SHIFT 12 (lines 100-106).
- osfmk/arm64/proc_reg.h:1149-1153. ARM_PGSHIFT is 14 if __ARM_16K_PG__, else 12. Same flag selects the whole L0..L3 geometry: native ARM_TT_Lx_* are aliased to ARM_16K_TT_Lx_* (lines 1280-1304) or ARM_4K_TT_Lx_* (lines 1306-1331). Both geometry sets are present and complete, e.g. 16K L2 covers 32 MiB per entry (line 1243) vs 4K L2 2 MiB (line 1249); 16K L3 has 2048 entries, 4K L3 has 512 (lines 1257-1278).
- __ARM_16K_PG__ is a platform-header define, not a kernel option: pexpert/pexpert/arm64/VMAPPLE.h:44, H13.h:71. It is absent from the in-tree Raspberry Pi 3 bring-up header pexpert/pexpert/arm64/BCM2837.h, which also has its own kernel config (config/MASTER.arm64.bcm2837), so the 4K-granule arm64 configuration is a first-class source alternative rather than an accidental ifdef - although section 2 shows it no longer preprocesses cleanly.
- osfmk/arm/arm_init.c:393-413 is the decisive block. If args->memSize > 1 GiB then PAGE_SHIFT_CONST = PAGE_MAX_SHIFT (16K, line 402); else PAGE_SHIFT_CONST = ARM_PGSHIFT, i.e. the hardware granule (line 409). Line 413 then forces page_shift_user32 = PAGE_MAX_SHIFT unconditionally: "32-bit apps always see 16KB page size".
- Consequence for the J8 (4 GB RAM): with __ARM_16K_PG__ set, PAGE_SIZE is 16384. With __ARM_16K_PG__ clear, PAGE_SHIFT_CONST is still set to 14 at line 402, so PAGE_SIZE is 16384 as well, on top of 4K page tables. 4096 appears only if memSize <= 1 GiB, or if arm_init.c:397 is patched. (documented code path, inferred conclusion.)
- There is also a mixed mode: __ARM_MIXED_PAGE_SIZE__ runs 4K-page *processes* on a 16K-page kernel, requires ARM_PARAMETERIZED_PMAP, and is mutually exclusive with __ARM_KERNEL_PROTECT__ (osfmk/arm64/proc_reg.h:358-384). It explicitly requires the kernel's default page size to stay 16K (osfmk/arm/pmap/pmap.c:11033-11037). So upstream's only 4K story is "4K user page tables", never "4K kernel VM pages".

So: is a 4K arm64 configuration supported upstream at all? Yes as a build configuration (BCM2837.h, proc_reg.h 4K geometry, a full ARM_4K translation-table maths set), and it is *programmed to still use 16K VM pages on >1 GiB devices*. A 4K VM page is a code path that exists but is reachable only on <=1 GiB hardware, and the 4K-granule build does not currently build (section 2). Neither of those is the same as "supported".

## 2. The arm64 pmap: parameterized vs. 16K-assuming

Parameterized (good news, this is most of it):

- pmap.c:141-219 defines pmap_table_level_info_16k[] and pmap_table_level_info_4k[] as data, one entry per level, each carrying .size/.offmask/.shift/.index_mask/.valid_mask/.type_mask.
- pmap.c:221-265 defines pmap_pt_attr_4k (pta_page_size 4096, .pta_page_shift 12, .pta_root_level = (T0SZ_BOOT - 16)/9, .pta_tcr_value = TCR_EL1_4KB) and pmap_pt_attr_16k (16384/14/TCR_EL1_16KB). native_pt_attr is chosen by __ARM_16K_PG__ at pmap.c:267-271; pmap_get_pt_attr() then drives nearly every mapping site (dozens of call sites, e.g. pmap.c:4004-4032, 5247-5263, 7064-7084, 10031-10068, 13976-13990).
- TTBR0/TTBR1 root geometry is derived, not hardcoded: pmap.c:371-374 compute arm64_root_pgtable_level and _num_ttes from ARM_PGSHIFT and PGTABLE_ADDR_BITS; PMAP_ROOT_ALLOC_SIZE is either ARM_PGBYTES or the sub-page L1 slice size (pmap.c:112-116).
- Per-level index/TTE count and L2 block size come from the level-info tables, and PTE count per table is table-size/8 (2048 vs 512), so no numeric 16K count survives in the mapping paths.
- Per-pmap TCR reprogramming exists but only under __ARM_MIXED_PAGE_SIZE__: pmap.c:4472-4473 and 7594-7597 compare pmap_get_pt_attr(pmap)->pta_tcr_value with get_tcr() and call set_tcr(). Without that flag the TCR is a compile-time constant (proc_reg.h:983-1000).
- The "one VM page can hold four page tables" bookkeeping is implemented: PT_INDEX_MAX is ARM_PGBYTES/4096 under mixed page size, else 1 for ARM_PGSHIFT==14 and 4 for ARM_PGSHIFT==12 (osfmk/arm/pmap/pmap_data.h:791-799), with the per-index arrays and asserts at pmap_data.h:887, 999-1015 and pmap_data.c:578, 2783-2789.
- The compressor already carries both kernels: WKdmCompress_4k.s / WKdmDecompress_4k.s exist next to the 16K variants, and vm_compressor_algorithms.c:245-249 and 275-279 pick 4K vs 16K at runtime when __ARM_16K_PG__ is clear.
- Low-global alias is granule-aware: LOWGLOBAL_ALIAS = LOW_GLOBAL_BASE_ADDRESS + 0x4000 at 16K, + 0x2000 at 4K (pmap.c:1274-1278).

Not parameterized (the cost of a 4K VM page):

- osfmk/arm/pmap/pmap.c:11054-11056 and 11148-11150: "#if _COMM_PAGE_AREA_LENGTH != PAGE_SIZE" then "#error We assume a single page", where _COMM_PAGE_AREA_LENGTH is (PAGE_SIZE) for KERNEL_PRIVATE (osfmk/arm/cpu_capabilities.h:172, 225). When __ARM_16K_PG__ is clear, PAGE_SIZE is (1 << PAGE_SHIFT_CONST) with PAGE_SHIFT_CONST an extern int; the C preprocessor evaluates that identifier as 0, so the constant is 1, and the check fails. The same pattern silently mis-evaluates in bsd/kern/uipc_mbuf.c:3088 and 7120 ("#if PAGE_SIZE == 4096" becomes false). Net effect: the 4K-*granule* build cannot preprocess pmap.c as shipped, i.e. the 4K path is source-complete but bit-rotted. (inferred from preprocessor semantics; no compiler available in this session, so unbuilt.)
- Note on terminology: there is no pmap_compute_subpage and no pgarray in 8796 (grep, 0 matches). The old sub-page machinery has been replaced by PT_INDEX_MAX plus the level-info tables; only __ARM64_PMAP_SUBPAGE_L1__ survives, and it is purely TTBR root-geometry (pmap.c:112-116, proc_reg.h:939-967, 1201-1217).
- kalloc counts: allocations are expressed as PAGE_SIZE (e.g. bsd/kern/mach_loader.c:1568, 1690), which is correct by construction but means "one kalloc page" is 16K, not 4K. Nothing here needed a per-granule constant.

Judgement on item 2: a 4K *granule* build is close to a config change (the pmap is genuinely parameterized). A 4K *VM page* build is a small patch set, not a config change: fix the PAGE_SIZE-in-#if sites, validate the PT_INDEX_MAX=4 path under a real workload, and re-check the commpage. It is not a rewrite, and the pmap is not the hard part.

## 3. Mach-O loader, user address space, commpage

- bsd/kern/mach_loader.c has no bare 0x4000 or 16384 literals (grep, 0 matches). It is PAGE_SIZE-parameterized: effective_page_mask starts as PAGE_MASK then narrows to vm_map_page_size(map) (lines 798-806), pagezero and segment checks use effective_page_size (949-951, 981), header reads use MIN(PAGE_SIZE, cmds_size) and step by PAGE_SIZE (1323, 1336), the user stack reserves 4 * PAGE_SIZE (2637) and allocates PAGE_SIZE extra (2647). The 4K checks at 2071-2175 are stricter than 16K, so they get easier, not harder, on a 4K kernel.
- bsd/kern/mach_loader.c:515 sets the 32-bit compat map's shift to page_shift_user32, which arm_init.c:413 pins at 14.
- Mapped address-space constants are granule-independent: VM_MAX_ADDRESS 0x00000000F0000000 and MACH_VM_MAX_ADDRESS 0x0000000FC0000000 (osfmk/mach/arm/vm_param.h:131-143).
- The commpage is the one place that hardcodes 16K for userland: osfmk/arm/commpage/commpage.c:172-179 writes KERNEL_PAGE_SHIFT and USER_PAGE_SHIFT_32 from variables, but USER_PAGE_SHIFT_64 from SIXTEENK_PAGE_SHIFT, unconditionally. commpage.c:243-255 then asserts the commpage text fits in one PAGE_SIZE page and memsets PAGE_SIZE.
- Commpage placement itself is 16K-aligned addresses in both granules (_COMM_PAGE64_BASE_ADDRESS 0x0000000FFFFFC000, _COMM_PAGE32_BASE_ADDRESS 0xFFFF4000, osfmk/arm/cpu_capabilities.h:162, 236), and the nesting region is reserved as 1 GiB (lines 192-197). I recomputed the two static_asserts at pmap.c:10997 and 11012 for both granules: 16K needs 0xFE000000 >= 0xF0000000 (true), 4K needs 0xFC0000000 >= 0xFC0000000 (true, exactly equal). So the commpage nesting asserts do *not* block a 4K granule - but the guard at 11054 does.

## 4. What the kernel reports, and what the userland expects

Channels that exist in 8796 (three, not four):

- hw.pagesize: bsd/kern/kern_mib.c:459-465 returns vm_map_page_size(get_task_map(current_task())), i.e. the current task map's shift, which defaults to PAGE_SHIFT (osfmk/vm/vm_map.c:1480, accessor at 21346-21376). This is 16384 for a 64-bit task on this 4 GB phone in either granule configuration.
- hw.pagesize32: kern_mib.c:646-657 returns 1 << page_shift_user32 = 16384 (both sysctls registered at 889-890, compat alias at 988).
- commpage USER_PAGE_SHIFT_64: hardcoded 14 (commpage.c:178-179).
- AT_PAGESZ: not present anywhere in this clone (grep, 0 matches). It cannot be cited as a kernel channel here; I did not establish where the shipped iOS userland gets its auxv value (unverified).

On a genuine 4K PAGE_SIZE kernel all three of the above would need attention at once: hw.pagesize would report 4096 while the commpage still says 14. That contradiction is the concrete reason "just patch arm_init.c:397" is not sufficient.

Userland dependence on 16K, strongest evidence first:

1. Empirical, and it is the strongest thing in this report: the sibling project runs the unmodified iOS 16.7.16 (20H392) iPhone 8 userland - launchd, backboardd, SpringBoard, stock apps - on XNU 8796 with 16K pages (documented: work/refs/ios-sailfish/README.md). That proves the pairing works at 16K. It does not prove it fails at 4K, and it was done on Cortex-A55/A76 hardware, not A53.
2. Apple's dyld cache format hardcodes 16K in cache metadata: DYLD_CACHE_PREWARMING_DATA_PAGE_SIZE is 0x4000 with the comment "16k pages", and dyld_prewarming_entry.numPages is documented as "assumes 16k pages" [1]. A 4K-page kernel reading that metadata would have to interpret it identically, which it can (it is just a VM offset/page count), but any tool that assumes kernel page size equals this constant is wrong.
3. The same header records page size per cache for slide info: dyld_cache_slide_info2/3/4/5 all carry "uint32_t page_size; // currently 4096 (may also be 16384)" [1]. So the shared-cache format is page-size-agnostic and versioned, which cuts against a hard 16K dependency at the format level. What page size the shipped iOS 16.7.16 cache was actually built with is measurable from the cache header but is unverified here - no IPSW or cache file is in this workspace.
4. Code signatures on iOS 16 use 4K code pages even on a 16K kernel: the prior art records that ubc_cs_convert_to_multilevel_hash takes the 4K-page path on a 16K kernel and that "ALL iOS 16 binaries (launchd, backboardd, SpringBoard) use 4K code pages" (documented: work/refs/ios-sweet-upstream/NOTES.md, the 0035 entry). If true, it is evidence that iOS 16 userland artefacts are already 4K-granular in the places that touch the kernel, and that the 16K assumption is a VM-layer assumption, not a per-binary one.
5. libSystem adapts at runtime rather than compile time: libsyscall/mach/mach_init.c:138-154 reads KERNEL_PAGE_SHIFT and USER_PAGE_SHIFT_64 out of the commpage and sets vm_kernel_page_size / vm_page_shift from them. So dyld and libsystem are built to consume whatever the kernel advertises, which is why (3) is plausible.
6. Segment alignment and libsystem_pthread stack sizing: I could not verify either from a primary source in this session. iOS arm64 Mach-O segments are widely described as 16K-aligned and the local tools assume it (tools/mkinit.py:26 "arm64 user segments are 16K aligned"), but 16K-aligned file offsets and vmaddrs are trivially valid on a 4K kernel, so I would not expect breakage there. Any claim that CoreFoundation or pthread specifically *requires* 16K is speculation and I am not making it.

## 5. This repo's own 16K assumptions

- shim/mmu.c:2-4, 18, 42-57: the EL1 stage-1 MMU is built for the 16K granule (TG0=16K, TG1=16K, T0SZ=28, PG_SIZE 16K, tables sized and aligned to 16K). This is the shim's declared contract with XNU: "the 16K granule XNU uses".
- shim/loader.c:16, 115-118: PAGE_16K 0x4000 used for ADT placement and zeroing.
- shim/shim.c:3 and 417: the shim already prints id_aa64mmfr0_el1 with a tg16 field. The granule question for this exact phone is therefore answerable from the next shim boot log, with no new probe (see section 7).
- tools/mkinit.py:26 (SEG 0x4000), tools/mkpayload.py:35 (PAGE 0x4000), tools/qemu-sweet.py:27 (PAGE 0x4000), and mkpayload.py:150 which places virt_base 32 MB below the Mach header "16K-granule L2 block" - i.e. the payload layout depends on the 16K L2 block size, not merely on alignment.
- shim/build.sh:31 passes --pagesize 4096 to mkimage. That is the Android boot.img page size (documented: repo/docs/device.md:98 lists 2048 for this phone's boot image), not the MMU granule. Do not let the two be confused in review.
- Nothing in shim/ or tools/ currently supports a 4K-granule build: the TCR value, the table geometry and the payload layout would all need a second configuration.

## 6. Verdict

(a) Can XNU 8796 boot with 4 KiB pages after a bounded patch set?
- 4K translation granule: yes, plausibly, and it is close to a config change. The pmap, compressor, loader and commpage placement are already parameterized or granule-neutral (sections 2-3). Bounded work: add a 4K machine config header without __ARM_16K_PG__, fix the PAGE_SIZE-in-#if sites (pmap.c:11054, 11148; uipc_mbuf.c:3088, 7120), and re-verify the PT_INDEX_MAX=4 paths. Unbuilt here, so this is inferred, not demonstrated.
- 4K software page (PAGE_SIZE == 4096): a small port, not a config change, and it is the option with no upside for this project: it would still not match what iOS 16 userland expects unless the commpage and both sysctls are changed coherently.

(b) Can the unmodified iOS 16.7.16 userland run against a 4K kernel?
- Not established, and the evidence leans no. Nothing in XNU 8796 forbids it in principle - libSystem reads the page shift at runtime (mach_init.c:146-154), dyld's cache header carries its own page_size [1], and the strongest counter-argument (segment alignment) does not survive scrutiny because 16K alignment is a superset of 4K alignment.
- What actually stands in the way is coherence of what the kernel advertises: USER_PAGE_SHIFT_64 is hardcoded to 14 (commpage.c:178), hw.pagesize follows the VM map (kern_mib.c:459-465), and 32-bit tasks are pinned to 16K (arm_init.c:413). A 4K kernel that reports 4096 to 64-bit tasks while the commpage says 14 is internally inconsistent, and that inconsistency, not any known userland bug, is the realistic failure mode.
- For this project the question is also moot in an important way: on a 4 GB phone the 4K-*granule* build still advertises 16K to userland (section 1), which is exactly what the iOS 16 userland expects and exactly what the sibling project proved works. If the A53 cannot do the 16K granule, that 4K-granule / 16K-VM-page configuration is the target, not a 4K-page kernel.

The one hardware fact that decides the shape of the port: does this A53 r0p4 implement the 16K granule? ARM's own guidance is that granule support is implementation-defined and must be read from ID_AA64MMFR0_EL1.TGran16; an ARM engineer states plainly that "by default A53 doesn't enable 16K support" and that a DB410C (also A53) was reported as not working with a 16K-page kernel [2], while the quoted ARM DEN0024A text says the A53 supports all three sizes [2]. I found no reading of ID_AA64MMFR0 for this handset: the boot dmesg contains no granule or MMFR0 line (measured on this device: repo/evidence/2026-10-30-first-boot/dmesg-boot.txt, searched for granule/MMFR/16K/pagesize - 0 matches). So TGran16 on this phone is unverified, and it is the highest-value unknown in the whole study.

Two readings in the sibling project must not be transferred to the J8. The "ID_AA64MMFR0 = ...101122: 16K granule YES, 4K YES" line (work/refs/ios-sweet-upstream/NOTES.md:375) is a shim measurement on the SM7150 handset with Kryo 470 cores, not on the A53. And the QEMU model deliberately reports that same register by running -cpu cortex-a76 "same ID_AA64MMFR0 as Kryo 470" (NOTES.md:343-344), so an emulator boot log proves nothing about j8y18lte granule support either.

## 7. The experiment that would settle it

1. Cheapest and safest: the next shim boot already prints tg16 (shim/shim.c:417). Read that one field. If it is non-zero, XNU's in-tree 16K path (what sweet and sailfish ship) is available and no page-size work is needed. This costs nothing and needs no new code.
2. If tg16 is 0: target the 4K-granule / 16K-VM-page configuration. Build a machine config without __ARM_16K_PG__ but with PAGE_SIZE pinned to 14, i.e. keep arm_init.c:402 semantics and do not touch the <=1 GiB branch. Fix the two pmap #errors and the two uipc_mbuf #ifs first, then compare the boot log against the sailfish 16K baseline.
3. Only if a 4096 report is genuinely wanted: boot a PAGE_SIZE==4096 kernel and run one 16K-built dylib under it, having first made USER_PAGE_SHIFT_64 and hw.pagesize agree. Whether dyld's cache mapping survives is then a measurement, not an argument. Do not attempt this before (1) and (2), because (2) is strictly more likely to give a bootable phone and (3) has no demonstrated benefit.

Note for the recovery plan: the power button on this unit is dead (repo/HANDOVER-2026-11-15.md:23, section 7). Any experiment that powers the phone off rather than rebooting it is a recovery risk, which is another reason to prefer reading tg16 from the shim log over running the standalone granule-probe that answers via SYSTEM_OFF (work/refs/ios-sailfish/tools/granule-probe.S:1-6).

## Sources

1. Apple, dyld source, include/mach-o/dyld_cache_format.h (DYLD_CACHE_PREWARMING_DATA_PAGE_SIZE 0x4000; dyld_cache_slide_info page_size "currently 4096 (may also be 16384)"), https://raw.githubusercontent.com/apple-oss-distributions/dyld/main/include/mach-o/dyld_cache_format.h - accessed 2026-11-17.
2. linux-arm-kernel, "16KB PAGE_SIZE not work on DB410C", Suzuki K. Poulose (ARM), 2015-12-26, quoting ARM DEN0024A on Cortex-A53 granule support, https://lore.kernel.org/all/567E7F63.80707@arm.com/ - accessed 2026-11-17.
