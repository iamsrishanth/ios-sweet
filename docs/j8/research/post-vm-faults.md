# After VM init: the remaining LSE and a data abort

Status: 2026-11-18. With patch 0098 (2 GiB J8 zone map), `kmem_range_init`
passes and the kernel completes VM initialization in QEMU.

## A53 (cortex-a53): second LSE instruction

```
VM bootstrap done: 86 maps, 166 entries and 500 holes left
Long logs support configured ...
Firehose configured: 16 chunks, 8 io pages
panic(cpu 0 caller 0xfffffff0078feaa8): Undefined kernel instruction:
  pc=0xfffffff0071d0a54 instr=f8280128 @sleh.c:951
```

QEMU's ESR is `0x02000000` (EC=0, undefined, IL=1). Disassembly:

```
0xfffffff0071d0a48  and  x9, x8, #0xfffffffffffff000
0xfffffff0071d0a4c  stlr x1, [x8]
0xfffffff0071d0a50  mov  x8, #-0x100000000
0xfffffff0071d0a54  ldadd x8, x8, [x9]      <-- LSE
```

So the `-mcpu=cortex-a53+crypto` fix covered the C code, but this function still
contains an LSE atomic. It has no nearby symbol (nearest is ~0x3c000 away), so
it is likely provided by a prebuilt KDK library (`libcc_kext` or
`libfirehose_kernel`), which is built for LSE-capable Apple CPUs. The J8
Cortex-A53 cannot execute it.

Screenshot: `docs/j8/evidence/qemu-2026-11-18-lse-undefined-a53.png`.

## LSE-capable CPU (`-cpu max`): kernel data abort

Running the same kernel on QEMU's `max` CPU (which implements LSE) gets much
further:

- zone map regions (VM, RO, GEN0-3, DATA, Metadata, Bitmaps) are printed;
- compressor info and 10 kernel threads exist;
- then:

```
panic(cpu 0 caller 0xfffffff0078fe80c): Kernel data abort
  at pc 0xfffffff0077f5f74, lr 0xfffffff0078604dc
  esr: 0x96000005  far: 0x0000000000000028
```

`ESR 0x96000005` is a translation fault at level 1 for `NULL + 0x28`: a
platform/driver object pointer is still NULL, which is expected at this stage
(no J8 platform expert or device tree population yet).

Screenshot: `docs/j8/evidence/qemu-2026-11-18-data-abort-maxcpu.png`.

## Meaning

- The 4 KiB non-large-memory kernel now executes deep into XNU VM and platform
  initialization.
- On the real J8 the immediate blocker is the remaining LSE instruction(s) in
  the prebuilt KDK code; QEMU `max` shows the next functional blocker is a NULL
  platform pointer, not the ISA.

## Next

1. Remove or avoid the remaining LSE: identify the providing library/function,
   then either stop linking that prebuilt object for J8 or post-process the
   kernel text into LL/SC sequences.
2. Fix the NULL+0x28 data abort (backtrace PCs `0x...7f5f74`, `0x...8604dc`)
   once the LSE is gone.
