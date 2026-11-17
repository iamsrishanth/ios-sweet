# 4 KiB bootstrap vs the macOS-platform kernel VA layout

Status: **found by the first QEMU J8 boot attempt (2026-11-17); root cause
identified from source; no fix landed yet.** This note records the evidence so
the next change is not guesswork.

## Observation

The QEMU harness (`tools/qemu-sweet.py`, `-M virt,gic-version=2`,
`-cpu cortex-a53`) loaded the linked J8 kernel (`kernel.development.j8`,
SHA-256 `f73a0b19…a06e0b`) at `phys_base = 0xb0000000`, with the ADT from
`mkadt.py --qemu --gicv2` (GICD `0x08000000`, GICC `0x08010000`, timer INTID
27). The stub entered the kernel at its physical entry with `x0 = boot_args`
and the MMU off, as for the SWEET payload.

QEMU then logged, before any serial output:

```
Taking exception 3 [Prefetch Abort] on CPU 0
...from EL1 to EL1
...with ESR 0x21/0x86000004
...with FAR 0xfffffe0007a4640c
...with ELR 0xfffffe0007a4640c
...to EL1 PC 0xfffffe0007412000 PSTATE 0x3c5
```

followed by an endless prefetch-abort loop at `0xfffffe0007412200`. The high
addresses are kernel VAs (`__TEXT` is linked at `0xfffffe0007004000`), so the
kernel did start executing and then faulted on its own high mapping.

## Why

Two constants are inconsistent for a 4 KiB-granule, macOS-platform kernel:

| Item | Value for this build | Source |
|---|---|---|
| Kernel link address | `0xfffffe0007004000` (-2 TiB) | `makedefs/MakeInc.def` `ARM64_LINK_ADDR` under `PLATFORM=MacOSX` |
| Kernel VA base | `VM_MIN_KERNEL_ADDRESS = 0 - TiB(2)`, 41 significant bits | `osfmk/mach/arm/vm_param.h`, `ARM_LARGE_MEMORY` branch |
| `T1SZ_BOOT`, 4 KiB, no subpage L1 | **25** (64 - 25 = 39-bit VA, base `0xffffff8000000000`) | `osfmk/arm64/proc_reg.h` |
| `T1SZ_BOOT`, 16 KiB, no subpage L1 | **17** (47-bit VA, base `0xffff800000000000`) | same file |

With `T1SZ_BOOT = 25`, the TTBR1 region starts at `0xffffff8000000000`. The
kernel address `0xfffffe0007004000` is **below** that base, so it falls in the
TTBR0 (user) half and is not mapped by the kernel's bootstrap tables: the first
high-VA fetch faults, and the EL1 vector (also a high VA) faults on entry.

The SWEET 16 KiB port does not hit this because `T1SZ_BOOT = 17` maps
`0xffff800000000000..`, which contains `0xfffffe...`. The BCM2837 4 KiB config
does not hit it either because it is not a macOS-platform build: it uses the
non-large-memory layout (`ARM64_LINK_ADDR = 0xfffffff007004000`, kernel base
`0xfffffff0...`, 37 significant bits), which is inside the 39-bit TTBR1 window.

So the failure is a **pre-existing 4 KiB + `ARM_LARGE_MEMORY` gap in
xnu-8796**, not a J8 driver bug. The earlier commpage static assertion was a
second symptom of the same 4K/large-memory mismatch (fixed by patch 0084).

## Options

1. **Add an L0 level to the bootstrap tables and set `T1SZ_BOOT = 23` for
   J8.** 41-bit kernel VA, which is exactly `VM_MIN_KERNEL_ADDRESS = -2 TiB`.
   With 4 KiB tables the walk then starts at L0; `start.s`'s
   `create_bootstrap_mapping` currently allocates only an L1 root and L2
   tables, so it needs one L0 table per mapping (V=P and KVA). `BOOTSTRAP_TABLE_SIZE`
   is 8 pages for 4 KiB non-subpage; for a 2 GiB image the current use is
   2 L1 + up to 4 L2 pages, leaving room for 2 L0 pages, but this needs to be
   checked for the J8's 4 GiB layout. This preserves the macOS-platform ABI
   that the ios-sweet component set expects. **Recommended.**
2. **Build J8 on a non-large-memory platform/link base** (`0xfffffff007004000`).
   Avoids the L0 work but changes `VM_MIN_KERNEL_ADDRESS`, pmap constants and
   the link address; the ios-sweet patches and KDK linkage assume the macOS
   platform, so this is the riskier path.
3. Linking the kernel below `0xffffff8000000000` is not an option: the
   macOS-platform layout is fixed at -2 TiB by `ARM_LARGE_MEMORY`.

The pmap side (`arm_vm_init`, `pmap_pt_attr_4k`) must be checked in the same
change: it builds the runtime kernel tables, and its `T1SZ` must match the
bootstrap's 23. The static assertion in `pmap.c` around the commpage already
runs under this layout and passes with the iOS VM limit (patch 0084).

## Attempt 2: L0 bootstrap (patch 0089)

`T1SZ_BOOT` is now 23 for J8 and `start.s` builds an L0 table for the KVA
mapping; CI run **44040806897** built it successfully. QEMU still faults, now
at `_arm_init` (`0xfffffe0007a464cc`), ESR `0x86000004` (translation fault,
level 0).

The bootstrap tables themselves are valid. Read from guest physical memory with
QMP `pmemsave` while stopped at the fault:

| Entry | Value |
|---|---|
| `L0[0x1fc]` (index for `-2 TiB`) | `0xb1173003` |
| `L1[0]` | `0xb1178003` |
| `L1[1]` | `0xb1179003` |
| `L2[0x3d]` (covers `0x7a464cc`) | `0xb1a00601` |

The compiled `sysreg_restore.tcr_el1` is `0x22a5172519`: T0SZ 25, T1SZ 23, both
4 KiB granules. So the tables and the TCR value are what patch 0089 intends.

The remaining fault therefore happens with a TTBR1/TCR state that does not
match those tables, or after a later switch. `_arm_init` is the first C entry,
so the next step is to record TTBR1/TCR immediately before the MMU enable (store
them to a fixed RAM address and read with QMP) and confirm whether TTBR1 is
`bootstrap_pagetables + 2 pages`, then check whether the `arm_vm_init` pmap
switch is where it diverges.

## Attempt 3: non-large-memory layout + no-LSE (reaches VM init)

Patch 0092 moved J8 to the non-large-memory layout (link `0xfffffff007004000`,
T1SZ 25, no L0 table). The `_arm_init` translation fault disappeared.

The next fault was an undefined instruction in `lck_grp_reference`: clang had
emitted `ldadd w8, w8, [x1]` (LSE) despite `-march=armv8-a+crypto`, because the
macOS arm64 target CPU still implies LSE. Patch 0093 builds with
`-mcpu=cortex-a53+crypto`; the assembler then rejected two `casa` instructions
in `commpage_asm.s`, replaced for J8 with `ldaxr`/`stxr` loops (patch 0094).

With patch 0094 (CI run **44074310435**), QEMU reaches XNU VM initialization:

```
Darwin Kernel Version 22.6.0 ... xnu-8796.141.3
pmap_startup() ...
vm_page_bootstrap: 76367 free pages, 2417 wired pages, ...
VM bootstrap: 97 maps, 204 entries and 512 holes available
panic(cpu 0 caller 0xfffffff007a72ed4): kmem_range_init:
vm_map_locate_space failing for claim kmem_ptr_range @vm_kern.c:3645
```

Screenshot: `docs/j8/evidence/qemu-2026-11-17-kmem-range-panic.png`.

So the 4 KiB kernel now executes XNU C code and initializes the VM, but the
non-large-memory VA layout does not satisfy `kmem_range_init`'s claim. Next:
analyse that claim against `VM_MIN_KERNEL_ADDRESS`, `LOW_GLOBAL_BASE_ADDRESS`
and the kernel image span (or fix the large-memory walk).

## Test

The QEMU harness is ready and does not need the phone:

```sh
python tools/mkadt.py j8.adt --qemu --gicv2
python tools/mkpayload.py kernel.development.j8 j8.adt j8-payload.bin
python tools/qemu-sweet.py j8-payload.bin out --gic-version 2 --cpu cortex-a53 --secs 20
```

Success criterion for the next step is serial output past `arm_vm_init` and a
`J8 GICv2: virtual timer INTID 27` line, then timer ticks. A panic is progress
compared with the current silent prefetch loop.
