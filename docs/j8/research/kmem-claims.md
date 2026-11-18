# J8 kernel-map claims (`kmem_range_init`)

Status: 2026-11-18. The J8 kernel reaches XNU VM initialization in QEMU and
fails while laying out the kernel-map claims. This note records the measured
values, the algorithm and the candidate fixes.

## Measured

- Kernel map: `VM_MIN_KERNEL_ADDRESS = 0xffffffe000000000`,
  `VM_MAX_KERNEL_ADDRESS = 0xfffffffbffffffff` (112 GiB), non-large-memory
  layout.
- RELEASE (no KASAN), CI run **44162509756**:

  ```
  zones size=0x200000000 (8 GiB) start=0xfffffff597a18000
  min=0xffffffe000000000 max=0xfffffffbffffffff
  largest=0xb77dfc000 (45.9 GiB) ptrs=1
  ```

- DEVELOPMENT + diagnostics, CI run **44098877305**: `zones` 8 GiB at
  `start=0xfffffff0840e0000`, largest hole 26 GiB.
- The RELEASE result proves KASAN is **not** the cause.

## Algorithm

`kmem_add_extra_claims()` sizes the ptr, sprayqtn and data claims from the
largest free hole, so that **all claims together fill that hole exactly**.
`kmem_range_init()` then walks the shuffled claims with a running `start`
cursor and calls `vm_map_locate_space(..., &start, ...)`.

If the cursor does not begin at the start of the largest hole, the pack crosses
into smaller holes and a later claim (the 8 GiB `zones` claim) has no contiguous
space left. Starting at the map base (patch 0096) made the DEVELOPMENT run
underflow `kmem_data_range`, confirming the placement sensitivity.

## Candidate fixes

1. **Patch 0097 (pending build, commit `42addf2`)**: start the cursor at the
   largest hole (`kmem_largest_hole_start()`) and reserve one guard page from
   `data_range_size` so the final claim does not end exactly at
   `kernel_map->max_offset`. Low risk, J8-only.
2. Enlarge the non-large map (raise `VM_MAX_KERNEL_ADDRESS` / pointer width).
   Risk: KASAN shadow and top-byte assumptions.
3. Shrink individual claims (zones, ptr ranges). Not sufficient alone: the
   claim sizes are recomputed to fill the hole, so the total stays the same.
4. Return to the large-memory walk, which was abandoned at `_arm_init`.

## Build blocker (2026-11-18)

`gh workflow run` returns HTTP 422 "Actions has been disabled for this
repository" even though `GET /actions/permissions` reports `enabled: true`.
The repository is a fork (`dipa-studio/ios-sweet`); forked repositories start
with Actions disabled, and the API toggle / disable-enable cycle did not clear
it. A push-triggered run also did not appear. Patch 0097 is pushed but unbuilt.
