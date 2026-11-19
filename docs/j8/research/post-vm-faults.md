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

## LSE emulation (0099) result

Patch 0099 adds J8-only LSE emulation to `handle_uncategorized`. With it the A53
no longer traps on `ldadd`: the kernel reaches the **same** `Kernel data abort`
as `-cpu max` (`pc 0xfffffff0077f5f74`, ESR `0x96000005`, FAR `0x28`). The
faulting sequence is:

```
ldr x22, [x9, #0x680]    ; global pointer at 0xfffffff00713e680 is NULL
ldr x20, [x22, #0x28]    ; data abort at 0x28
```

The caller references the strings `chip-id` and `IOPlatformExpert`, so this is
the **platform-expert path**, not an ISA problem. Next: instantiate/populate the
J8 platform object (platform expert / ADT).

## Platform expert attempt (0100): compiled in, still not instantiated

Patch 0100 compiles the existing `SweetPlatformExpert` (an
`IODTPlatformExpert` subclass) for J8 and adds a J8 match entry for
`'xiaomi,sweet'`. The RELEASE kernel built by run **44279342492** contains both
`SweetPlatformExpert` and `xiaomi,sweet`, but the boot result is **unchanged**:
the same `NULL+0x28` data abort at `pc 0xfffffff0077f5f74`.

So the platform expert class is present, but no instance is matched/started
(`gIOPlatformExpert` remains NULL). The likely causes are that the device-tree
root nub is not created because `PE_state.deviceTreeHead` is unset/unparsed, or
that the match provider/name differs from `xiaomi,sweet`.

Next diagnostic: print `PE_state.deviceTreeHead`, `deviceTreeSize` and
`PE_state.bootArgs` in the early platform init (J8 only), and the
`IOPlatformExpert` match/provider path, then fix the ADT pointer or the match.

## DT diagnostics (0101-0103): console not available

Patch 0101 printed the DT pointers in `PE_init_platform` and patch 0102 moved the
print to `PE_lockdown_iokit`; both produced a **black screen**. QEMU `-d int`
shows an early data abort at `FAR 0x1b0` (ELR `0xfffffff007346df4`) in the
console/print path, before the framebuffer can show text. Replacing the print
with a `panic` (0103) also stays black for the same reason.

So the DT head/size could not be read this way, and the platform expert is still
not instantiated. Next approaches:

1. store `PE_state.deviceTreeHead`/`deviceTreeSize` into a fixed mapped page and
   read it with QEMU `pmemsave` (no console dependency), or
2. use the QEMU gdb stub to read `PE_state` directly, or
3. instantiate the platform expert unconditionally in `StartIOKitMatching` to
   see whether the fault is the NULL global or the DT data.

## Root-name match fix (0104)

`IODeviceTreeAlloc` renames the root registry entry to the **first compatible
string**:

```c
// make root name first compatible entry (purely cosmetic)
parent->setName((const char *) prop->getBytesNoCopy());
```

For our ADT that string is `D22AP`, not `xiaomi,sweet`. Since
`IODTPlatformExpert::probe` compares `IONameMatch` with `provider->copyName()`,
the platform expert never matched. Patch 0104 uses `IONameMatch = 'D22AP'` and
removes the temporary DT panic.

Result (run **44397240748**): the old `NULL+0x28` fault at
`pc 0xfffffff0077f5f74` is **gone**. The kernel now panics earlier, with an
unprintable console (black screen; QEMU `-d int` shows an exception loop with
ELRs `0x733e3ec`, `0x78fecec`, `0x73370ac` and a first `0x7346df4`/`FAR 0x1b0`).
So the platform expert now matches, but its start/`processTopLevel` fails before
the console is available.

Next: instrument `SweetPlatformExpert::start`/`processTopLevel` through a
non-console channel (a fixed memory store read via QEMU), or simplify `start`
to isolate the failing call.

## Stage markers (0105): start not reached

Patch 0105 writes a magic/stage pair from `SweetPlatformExpert::start` and
`processTopLevel` into a static array. After booting the panic state and
scanning the whole loaded image (0xb0000000 + 0x1c00000) with QEMU
`pmemsave`, **no marker was found**. So `SweetPlatformExpert::start` is never
entered: matching fails (or panics) earlier than the old `chip-id` query.

This also means the `D22AP` match change did not instantiate the class; it
removed the earlier NULL query only because the kernel fails at a still earlier
step in `StartIOKitMatching`.

Next: put stage markers in `StartIOKitMatching` and
`IOPlatformExpertDevice::startIOServiceMatching` (workLoop acquisition and
registerService) to find the exact earlier failure.

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
