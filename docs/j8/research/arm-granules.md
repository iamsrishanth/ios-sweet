# Arm granules on Cortex-A53 r0p4: no 16 KiB granule

Status 2026-11-17. Supersedes the earlier draft of this file, which had TGran4 at
the wrong bits and left the A53 answer open. Basis for docs/j8/FEASIBILITY.md §2,
which has been updated to match these corrections. No other repository file was
changed by the research pass; the probe and document corrections are a separate
commit.

Verdict: in the Cortex-A53 r0p4 inside the Snapdragon 450, 4 KiB and 64 KiB
granules are implemented and 16 KiB is not — documented in the core's TRM, not
inferred from a SoC datasheet.

Labels: (documented) = primary source, quoted; (measured) = observed on this phone
or on a device in this workspace; (inferred) = derived from cited facts;
(unverified) = not checked in this pass.

## 1. What ARMv8-A defines, and how ID_AA64MMFR0_EL1 encodes it

AArch64 has three stage-1 granules — 4 KiB, 16 KiB, 64 KiB — and none is
individually mandatory: each implementation reports which it has, and software
selects one per translation regime (TCR_EL1.TG0 for TTBR0_EL1, TG1 for TTBR1_EL1,
VTCR_EL2 for stage 2) [3][4].

Encodings from the current register description (Arm ARM DDI 0601, 2026-09) [3]:

| Field | Bits | Values |
|---|---|---|
| TGran4 | [31:28] | 0b0000 = 4KB supported; 0b0001 = 4KB with 52-bit input/output (FEAT_LPA2); 0b1111 = 4KB not supported; rest reserved |
| TGran64 | [27:24] | 0b0000 = 64KB supported; 0b1111 = 64KB not supported; rest reserved |
| TGran16 | [23:20] | 0b0000 = 16KB NOT supported; 0b0001 = 16KB supported; 0b0010 = 16KB with 52-bit input/output (FEAT_LPA2); rest reserved |
| TGran16_2 | [35:32] | 0b0000 = as TGran16; 0b0001 = not supported at stage 2; 0b0010 = supported at stage 2; 0b0011 = 52-bit at stage 2 (FEAT_LPA2) |
| TGran64_2 | [39:36] | 0b0000 = as TGran64; 0b0001 = not supported at stage 2; 0b0010 = supported at stage 2 |
| TGran4_2 | [43:40] | 0b0000 = as TGran4; 0b0001 = not supported at stage 2; 0b0010 = supported at stage 2; 0b0011 = 52-bit at stage 2 (FEAT_LPA2) |

Three traps, all documented. (1) Polarity is not uniform: TGran4/TGran64 are
signed fields where 0x0 means implemented and 0xF means not ("Software that depends
on the feature can use the test: if value >= 0"), while TGran16 is unsigned, so 0x0
means not implemented [6, §D24.1.3]. (2) The *_2 fields are a third scheme ("The
value 0x0 indicates that support is identified by another field") and describe
stage-2 support for a hypervisor, not stage 1 [6, §D24.1.3.3]. (3) FEAT_LPA2 is
0b0010 in TGran16 but 0b0011 in TGran4_2/TGran16_2 — 0b0010 in those fields means
plain stage-2 support [3].

## 2. Cortex-A53 r0p4 (MIDR part 0xd03) does not implement 16 KiB

Cortex-A53 TRM, revision J = r0p4 (DDI 0500J), field by field [1]:

- TGran16 [23:20]: "Support for 16 KB memory translation granule size: 0x0
  Indicates that the 16KB granule is not supported."
- TGran4 [31:28]: "0x0 Indicates that the 4KB granule is supported."
- TGran64 [27:24]: "0x0 Indicates that the 64KB granule is supported."
- SNSMem [15:12] = 0b0001, BigEnd [11:8] = 0b0001, ASIDBits [7:4] = 0b0010,
  PARange [3:0] = 0b0010 (40 bits).

Composed, the documented value is 0x0000000000001122. The same TRM's TCR_EL1 page
lists the legal values for this core [2]: "TG0 — TTBR0_EL1 granule size. The
possible values are: 4KB. 64KB." The 16 KB encoding is not among them.

Independent cross-check [9] — a big.LITTLE part with an A53 cluster and an A73
cluster:

    CPU features: SANITY CHECK: Unexpected variation in SYS_ID_AA64MMFR0_EL1.
    Boot CPU: 0x00000000001122, CPU4: 0x00000000101122

The A53 cluster reads exactly the 0x1122 composed above; the A73 differs only in
bits [23:20] = 0b0001, i.e. 16KB supported.

Correction: the earlier draft quoted Arm's Cortex-A53/A57 programmer's guide
DEN0024A ("supports all three sizes") and concluded the documents conflict and the
answer is unknown. That quotation could not be re-opened here (unverified), and it
does not outrank the core-specific TRM for r0p4; the measured 0x1122 above matches
the TRM. The J8 reports part 0xd03, revision r0p4 (measured, 32-bit
/proc/cpuinfo) [15:55], and r0p4 is the last A53 revision, so DDI 0500J is the
right document for this handset.

## 3. Where the line falls

| Core | TGran16 | Evidence |
|---|---|---|
| Cortex-A53 r0p4 | 0b0000 (no 16K) | DDI 0500J, quoted above [1] |
| Cortex-A55 | 0x1 (16K supported) | A55 TRM: "0x1 Indicates that the 16KB granule is supported" [7] |
| Cortex-A73 | 0x1 (16K supported) | TRM quoted in [9]; cluster value 0x101122 in the same log [9] |
| Cortex-A76 | 0x1 (16K supported) | A76 TRM, same wording [8] |
| A75, A77, A78, X1-X4, A710/A715 | not checked | unverified here; do not quote this note for them |

Whole-device: the Redmi Note 10 Pro (Snapdragon 732G, two A76 + six A55) reads
ID_AA64MMFR0 = 0x101122 and runs a 16K-granule MMU, TCR 0x1409CB51C [15:375, 386].

The J8 is one core-family generation on the wrong side: the Armv8.0 "LITTLE" cores
of that era are 4K/64K only, and 16K appears on DynamIQ-era cores (A55 and A75/A76
and later). The generalisation to all later cores is inferred; only the four rows
above are documented here.

## 4. Why this is hardware, not a kernel constant

TCR_EL1.TG0 encodes 0b00 = 4KB, 0b01 = 64KB, 0b10 = 16KB [4]. The field is consumed
by the hardware page-table walker, and the whole table geometry follows from it:
table size (one granule), entries per table (512, 2048 or 8192), which levels may
hold block descriptors, and the legal T0SZ range.

Writing TG0 = 0b10 on a core with no TGran16 does not fault at write time. The Arm
ARM, TG0 field [4]:

    If the value is programmed to either a reserved value or a size that has not
    been implemented, then the hardware will treat the field as if it has been
    programmed to an IMPLEMENTATION DEFINED choice of the sizes that has been
    implemented for all purposes other than the value read back from this register.
    It is IMPLEMENTATION DEFINED whether the value read back is the value
    programmed or the value that corresponds to the size chosen.

On an A53 the walker silently picks 4KB or 64KB while software builds 16KB tables,
and the read-back may still say 16KB. That is worse than a fault: descriptors get
decoded with the wrong entry size and wrong block boundaries. Hence the kernel
validates the granule before enabling the MMU — Linux states it plainly: "We
already refuse to boot CPUs that don't support our configured page size" [13].

A 4KB kernel therefore cannot present 16KiB hardware pages. There is no override:
the granule is the unit the walker maps and the TLB caches. Software can only
emulate a bigger page with a contiguous, aligned run of 4KB pages sharing
attributes — exactly what XNU does: PAGE_RATIO = (1 << PAGE_SHIFT) >> ARM_PGSHIFT,
i.e. 4 for a 16KiB software page on 4KiB hardware, with pmap walking PAGE_RATIO PTEs
per software page [16]. The hardware page stays 4KB, so granule-level properties
(table geometry, block mappings, TLB and fault granularity) stay 4KB.

## 5. Can user space read it? No — and TID3 is not why

The architecture has no EL0 path that returns the value. Access pseudocode [6,
§D24.2.87]:

    elsif PSTATE.EL == EL0 then
        if IsFeatureImplemented(FEAT_IDST) then
            AArch64_SystemAccessTraptoEL1orEL2(0x18);
        else
            Undefined();
        end;
    elsif PSTATE.EL == EL1 then
        if EL2Enabled() && HCR_EL2().TID3 == '1' then
            AArch64_SystemAccessTrap(EL2, 0x18);

The A53 TRM's accessibility table agrees: EL0 is "-" (not accessible); EL1+ are RO
[1]. HCR_EL2.TID3 is the EL1 branch, not an EL0 gate: "Trap ID group 3. Traps EL1
reads of group 3 ID registers to EL2, when EL2 is enabled in the current Security
state", reported as EC 0x18, with ID_AA64MMFR0_EL1 in the enumerated group [5]. A
hypervisor sets it to synthesise the ID space for a guest instead of exposing the
host core's.

Device fact: a static AArch64 probe built for this study [17] ran on the J8 and the
MRS trapped with SIGILL. That is the required EL0 outcome and says nothing about
TID3 or about the value. The probe's header comment ("EL0 access ... only permitted
while HCR_EL2.TID3 == 0") has the mechanism backwards, and two decode bugs would
have produced wrong numbers had it printed: TGran4 is read from [15:12], which is
SNSMem (TGran4 is [31:28]), and its tg() helper decodes 0 as "not supported" for
every field, which is inverted for TGran4/TGran64 [17]. The value therefore comes
from documentation plus CPU identification (part 0xd03, r0p4, then DDI 0500J), or
from a probe at EL1/EL2: the EL1 probe already built
(tools/j8/granule-probe-el1.S) is now a confirmation, not the decision.

TID3 still matters as an escape route: an EL2 hypervisor can set it and hand a
guest a forged TGran16. The Snapintosh probe does exactly that — traps the guest
read, substitutes TGran16 = 0b0010, and notes that "Shadow paging keeps the guest's
16K tables away from the MMU" [14]. That is the only way a 16K-expecting guest
could run here, and it is a shadow-paging hypervisor, not a config flag.

## 6. /proc/cpuinfo and CONFIG_ARM64_4K_PAGES say nothing about capability

- arm64 /proc/cpuinfo prints processor, model name, BogoMIPS, Features (HWCAP
  names), CPU implementer, architecture, variant, part, revision — and no granule
  field; the 32-bit compat view this phone sees has the same fields [11:221-268].
- CONFIG_ARM64_4K_PAGES picks the kernel's own granule at build time. The Kconfig
  prompt is "Page size" and the help is "This feature enables 4KB pages support."
  [12]: a statement about the binary, not the CPU. The CPU is checked at boot
  against ID_AA64MMFR0_EL1 and a mismatch is fatal [13].
- Measured here: CONFIG_ARM64=y, CONFIG_ARM64_4K_PAGES=y, CONFIG_ARM64_VA_BITS=39,
  CONFIG_COMPAT=y from /proc/config.gz [15:56] — consistent with an A53, but not
  evidence about TGran16 either way.

## 7. Verdict

| Granule | Cortex-A53 r0p4 in this J8 | Basis |
|---|---|---|
| 4 KiB | supported | TRM TGran4 = 0x0 [1]; 4K-pages kernel boots today (measured) [15:56] |
| 16 KiB | NOT supported | TRM TGran16 = 0x0 [1]; TRM allows only 4KB/64KB for TCR_EL1.TG0 [2]; cluster value 0x1122 in [9] |
| 64 KiB | supported | TRM TGran64 = 0x0 [1] |

Residual uncertainty:

- The register was never read on this handset: EL0 cannot [1][6], and the EL1 probe
  is built but not flashed. The answer rests on TRM plus revision ID — sound, but
  documentation rather than a measurement of this unit.
- The chain assumes the phone's r0p4 is a stock A53 with unmodified ID values. No
  evidence contradicts it, and an SoC cannot add a granule the core's MMU lacks,
  but a fused-off or errata-affected configuration is not excluded by any source
  read here (inferred).
- "No 16K granule" becomes "build XNU without __ARM_16K_PG__" only if XNU's 4KiB
  granule path (ARM_PGSHIFT 12, PAGE_RATIO 4) is sound in xnu-8796.101.5. It exists
  in-tree [16], but neither reference port exercises it (unverified).

Consequence: the kernel config must drop __ARM_16K_PG__ and run 16KiB software pages
over 4KiB hardware pages, and that path needs QEMU validation before hardware. The
alternative — EL2 trapping ID reads and shadowing guest tables — is real but is a
much larger project [14].

## Sources (all accessed 2026-11-17)

1. Arm, Cortex-A53 MPCore Processor TRM r0p4 (DDI 0500J), AArch64 Memory Model Feature Register 0, EL1 — https://documentation-service.arm.com/documentation/ddi0500/j/System-Control/AArch64-register-descriptions/AArch64-Memory-Model-Feature-Register-0--EL1
2. Same TRM, Translation Control Register, EL1 — https://documentation-service.arm.com/documentation/ddi0500/j/System-Control/AArch64-register-descriptions/Translation-Control-Register--EL1
3. Arm, Arm A-profile Architecture Registers 2026-09 (DDI 0601), ID_AA64MMFR0_EL1 — https://documentation-service.arm.com/documentation/ddi0601/2026-09/AArch64-Registers/ID-AA64MMFR0-EL1--AArch64-Memory-Model-Feature-Register-0
4. Same document, TCR_EL1, Translation Control Register (EL1) — https://documentation-service.arm.com/documentation/ddi0601/2026-09/AArch64-Registers/TCR-EL1--Translation-Control-Register--EL1-
5. Same document, HCR_EL2, Hypervisor Configuration Register (TID3, bit 18) — https://documentation-service.arm.com/documentation/ddi0601/2026-09/AArch64-Registers/HCR-EL2--Hypervisor-Configuration-Register
6. Arm, Arm Architecture Reference Manual for A-profile, DDI 0487 M.d (sections D24.1.3, D24.1.3.3, D24.2.87) — https://documentation-service.arm.com/documentation/ddi0487/md
7. Arm, Cortex-A55 Core TRM 0200, ID_AA64MMFR0_EL1 — https://documentation-service.arm.com/documentation/100442/0200/Register-descriptions/AArch64-system-registers/ID-AA64MMFR0-EL1--AArch64-Memory-Model-Feature-Register-0--EL1
8. Arm, Cortex-A76 Core TRM 0401, ID_AA64MMFR0_EL1 — https://documentation-service.arm.com/documentation/100798/0401/AArch64-System-registers/ID-AA64MMFR0-EL1--AArch64-Memory-Model-Feature-Register-0--EL1
9. L. Yan et al., arm64: cpufeature: check translation granule size based on kernel config, linux-arm-kernel, 2017-05-18 (quotes DDI 0500F §4.3.21 and the A73 TRM) — https://lore.kernel.org/linux-arm-kernel/1495102873-10858-1-git-send-email-leo.yan@linaro.org/T/
10. Linux, arch/arm64/tools/sysreg (register description transcribed from the Arm ARM) — https://raw.githubusercontent.com/torvalds/linux/master/arch/arm64/tools/sysreg
11. Linux, arch/arm64/kernel/cpuinfo.c — https://raw.githubusercontent.com/torvalds/linux/master/arch/arm64/kernel/cpuinfo.c
12. Linux, arch/arm64/Kconfig (Page size choice) — https://raw.githubusercontent.com/torvalds/linux/master/arch/arm64/Kconfig
13. Linux, arch/arm64/kernel/cpufeature.c (ftr_id_aa64mmfr0 and the refuse-to-boot comment) — https://raw.githubusercontent.com/torvalds/linux/master/arch/arm64/kernel/cpufeature.c
14. Snapintosh, probes/tid3.S (EL2 probe: set HCR_EL2.TID3, trap the guest read, forge TGran16 = 0b0010) — https://github.com/Snapintosh/Snapintosh/blob/main/probes/tid3.S
15. This workspace: ios-sweet/docs/j8/WORKSPACE-EVIDENCE.md section 3 (J8 /proc/cpuinfo part 0xd03 r0p4; /proc/config.gz) and ios-sweet/NOTES.md lines 375, 384-386 (Redmi Note 10 Pro MMFR0 = 0x101122, 16K MMU, TCR 0x1409CB51C) — measured
16. This workspace: work/refs/xnu-8796 (xnu-8796.101.5) osfmk/arm64/proc_reg.h lines 814-826, 992-1000, 1146-1155; osfmk/arm/pmap/pmap.h line 256; osfmk/mach/arm/vm_param.h lines 59-73
17. This workspace: work/j8-probe/j8probe.c (EL0 probe; comments and decode table corrected by this note)
