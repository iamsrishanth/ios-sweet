/*
 * Milestone 3: EL1 stage-1 MMU with the 16K granule XNU uses.
 *
 * T0SZ=28 gives a 36-bit VA whose walk starts at level 2: one 16K table of
 * 2048 x 32MB blocks covers 64GB, identity mapped (VA == PA).
 *
 * Qualcomm XPUs fault on speculative reads of protected carve-outs (TZ,
 * hyp, modem...), so nothing is mapped cacheable except our own image:
 *   - default: Device-nGnRnE, execute-never (same as MMU off)
 *   - the 32MB block(s) holding the image get an L3 table: image pages are
 *     Normal write-back, the rest of the block stays Device
 *   - the splash framebuffer block is Normal non-cacheable (write-combining)
 */
#include <stdint.h>
#include "mmu.h"
#include "console.h"

#ifndef J8_4K
#define PG_SHIFT	14
#define PG_SIZE		(1UL << PG_SHIFT)		/* 16K */
#define BLK_SHIFT	25
#define BLK_SIZE	(1UL << BLK_SHIFT)		/* 32MB level-2 block */
#define NENT		2048
#else
#define PG_SHIFT	12
#define PG_SIZE		(1UL << PG_SHIFT)		/* 4K */
#define BLK_SHIFT	21
#define BLK_SIZE	(1UL << BLK_SHIFT)		/* 2MB level-2 block */
#define NENT		512
#endif

#define ATTR_DEV	0	/* MAIR attr0 = 0x00 Device-nGnRnE */
#define ATTR_WB		1	/* MAIR attr1 = 0xff Normal WB RW-alloc */
#define ATTR_NC		2	/* MAIR attr2 = 0x44 Normal non-cacheable */
#define MAIR_VAL	0x44ff00UL

#define D_BLOCK		1UL
#define D_TABLE		3UL
#define D_PAGE		3UL
#define D_ATTR(i)	((uint64_t)(i) << 2)
#define D_SH_OUTER	(2UL << 8)
#define D_SH_INNER	(3UL << 8)
#define D_AF		(1UL << 10)
#define D_PXN		(1UL << 53)
#define D_UXN		(1UL << 54)

#define DEV_FLAGS	(D_ATTR(ATTR_DEV) | D_AF | D_PXN | D_UXN)
#define WB_FLAGS	(D_ATTR(ATTR_WB) | D_SH_INNER | D_AF | D_UXN)
#define NC_FLAGS	(D_ATTR(ATTR_NC) | D_SH_OUTER | D_AF | D_PXN | D_UXN)

#ifdef J8_4K
/* 39-bit TTBR0, 4K TG0; TTBR1 is disabled but encode its 4K TG correctly. */
#define TCR_VAL		(25UL | (1UL << 8) | (1UL << 10) | (3UL << 12) | \
			 (25UL << 16) | (1UL << 23) | (1UL << 24) | (1UL << 26) | \
			 (3UL << 28) | (2UL << 30) | (1UL << 32))
#else
/* T0SZ=28, IRGN0/ORGN0=WBWA, SH0=inner, TG0=16K, EPD1, TG1=16K, IPS=36 bit */
#define TCR_VAL		(28UL | (1UL << 8) | (1UL << 10) | (3UL << 12) | (2UL << 14) | \
			 (28UL << 16) | (1UL << 23) | (1UL << 30) | (1UL << 32))
#endif

#define SCTLR_M		(1UL << 0)
#define SCTLR_A		(1UL << 1)
#define SCTLR_C		(1UL << 2)
#define SCTLR_I		(1UL << 12)
#define SCTLR_WXN	(1UL << 19)

#define rsr(r) ({ uint64_t _v; __asm__ volatile("mrs %0, " r : "=r"(_v)); _v; })
#define wsr(r, v) __asm__ volatile("msr " r ", %0" : : "r"((uint64_t)(v)))

extern char _head[], _end[];

#ifdef J8_4K
/* L1 + eight L2 tables cover the first 8 GiB; up to two image L3s and
 * ten splash-reservation L3s. Round up with spare pages for future changes. */
#define PT_POOL_PAGES	24
static uint64_t pt_pool[PT_POOL_PAGES * NENT] __attribute__((aligned(4096)));
#else
/* One L2 and up to two L3 tables for the legacy 16 KiB path. */
#define PT_POOL_PAGES	4
static uint64_t pt_pool[PT_POOL_PAGES * NENT] __attribute__((aligned(4096)));
#endif

static void dcache_inval_range(uintptr_t start, uintptr_t end)
{
	uint64_t line = 4UL << ((rsr("ctr_el0") >> 16) & 0xf);

	for (uintptr_t a = start & ~(line - 1); a < end; a += line)
		__asm__ volatile("dc ivac, %0" : : "r"(a) : "memory");
	__asm__ volatile("dsb sy" ::: "memory");
}

static void zero_table(uint64_t *t)
{
	for (int i = 0; i < NENT; i++)
		t[i] = 0;
}

int mmu_enable(struct mmu_info *info)
{
	uintptr_t img_lo = (uintptr_t)_head & ~(PG_SIZE - 1);
	uintptr_t img_hi = ((uintptr_t)_end + PG_SIZE - 1) & ~(PG_SIZE - 1);
	uintptr_t pool = ((uintptr_t)pt_pool + PG_SIZE - 1) & ~(PG_SIZE - 1);
	uint64_t *l2 = (uint64_t *)pool;
	int n_l3 = 0;

	info->img_lo = img_lo;
	info->img_hi = img_hi;
	info->ttbr = pool;
	if (((img_hi - 1) >> BLK_SHIFT) - (img_lo >> BLK_SHIFT) > 1)
		return -1;	/* only room for two L3 tables */

#ifdef J8_4K
	/* A 39-bit VA with 4 KiB pages walks L1 -> L2 -> L3. Build identity
	 * mappings for [0,8 GiB): eight L1 entries point to eight 2 MiB L2 maps. */
	zero_table(l2); /* page 0 is the TTBR0 L1 root */
	uint64_t *l2tab[8];
	for (uint64_t g = 0; g < 8; g++) {
		l2tab[g] = (uint64_t *)(pool + (1 + g) * PG_SIZE);
		zero_table(l2tab[g]);
		l2[g] = (uintptr_t)l2tab[g] | D_TABLE;
		for (uint64_t j = 0; j < NENT; j++) {
			uint64_t pa = (g << 30) | (j << BLK_SHIFT);
			l2tab[g][j] = pa | DEV_FLAGS | D_BLOCK;
		}
	}

	/* Split only blocks that contain our shim image or the visible framebuffer.
	 * This avoids accidental table spill across the gap from 0x80000000 to
	 * 0x90000000. */
	uint64_t fb_lo = FB_BASE & ~(PG_SIZE - 1);
	uint64_t fb_hi = (FB_BASE + FB_MAP_SIZE + PG_SIZE - 1) & ~(PG_SIZE - 1);
	uint64_t fb_first = fb_lo >> BLK_SHIFT;
	uint64_t fb_last = (fb_hi - 1) >> BLK_SHIFT;
	if (img_hi > (8UL << 30) || fb_hi > (8UL << 30) || img_hi <= img_lo)
		return -3; /* outside the mapped identity window or empty image */
	uint64_t img_first = img_lo >> BLK_SHIFT;
	uint64_t img_last = (img_hi - 1) >> BLK_SHIFT;
	for (uint64_t blk = 0; blk < (8UL << 30) / BLK_SIZE; blk++) {
		int need_l3 = (blk >= img_first && blk <= img_last) ||
		    (blk >= fb_first && blk <= fb_last);
		if (!need_l3)
			continue;
		if (n_l3 >= PT_POOL_PAGES - 9)
			return -2; /* L1 + eight L2 tables occupy the first 9 pages */
		uint64_t *l3 = (uint64_t *)(pool + (9 + n_l3++) * PG_SIZE);
		uint64_t base = blk << BLK_SHIFT;
		for (uint64_t i = 0; i < NENT; i++) {
			uint64_t pa = base + (i << PG_SHIFT);
			int ours = pa < img_hi && pa + PG_SIZE > img_lo;
			int fb = pa < fb_hi && pa + PG_SIZE > fb_lo;
			l3[i] = pa | (fb ? NC_FLAGS : (ours ? WB_FLAGS : DEV_FLAGS)) | D_PAGE;
		}
		l2tab[blk >> 9][blk & (NENT - 1)] = (uintptr_t)l3 | D_TABLE;
	}
#else
	/* Legacy 16 KiB identity map (existing SWEET path). */
	zero_table(l2);
	for (uint64_t i = 0; i < (8UL << 30) >> BLK_SHIFT; i++)
		l2[i] = (i << BLK_SHIFT) | DEV_FLAGS | D_BLOCK;
	l2[FB_PHYS >> BLK_SHIFT] = (FB_PHYS & ~(BLK_SIZE - 1)) | NC_FLAGS | D_BLOCK;
	uint64_t first_blk = img_lo >> BLK_SHIFT;
	uint64_t last_blk = (img_hi - 1) >> BLK_SHIFT;
	for (uint64_t blk = first_blk; blk <= last_blk; blk++) {
		uint64_t *l3 = (uint64_t *)(pool + (1 + n_l3++) * PG_SIZE);
		uint64_t base = blk << BLK_SHIFT;
		for (int i = 0; i < NENT; i++) {
			uint64_t pa = base + ((uint64_t)i << PG_SHIFT);
			int ours = pa >= img_lo && pa < img_hi;
			l3[i] = pa | (ours ? WB_FLAGS : DEV_FLAGS) | D_PAGE;
		}
		l2[blk] = (uintptr_t)l3 | D_TABLE;
	}
#endif
	info->n_l3 = n_l3;

	/* Image lines may be stale in the cache from ABL; we wrote memory directly. */
	dcache_inval_range(img_lo, img_hi);
	__asm__ volatile("ic iallu; dsb sy; isb" ::: "memory");

	wsr("mair_el1", MAIR_VAL);
	wsr("tcr_el1", TCR_VAL);
	wsr("ttbr0_el1", pool);
	__asm__ volatile("isb; tlbi vmalle1; dsb nsh; isb" ::: "memory");

	uint64_t sctlr = rsr("sctlr_el1");
	info->sctlr_before = sctlr;
	sctlr &= ~(SCTLR_A | SCTLR_WXN);
	sctlr |= SCTLR_M | SCTLR_C | SCTLR_I;
	wsr("sctlr_el1", sctlr);
	__asm__ volatile("isb" ::: "memory");
	info->sctlr_after = rsr("sctlr_el1");
	return 0;
}

/* Return the leaf descriptor from the identity map for a self-test VA. */
uint64_t mmu_leaf_desc(uintptr_t va)
{
#ifdef J8_4K
	uintptr_t pool = ((uintptr_t)pt_pool + PG_SIZE - 1) & ~(PG_SIZE - 1);
	uint64_t *l1 = (uint64_t *)pool;
	uint64_t d1 = l1[(va >> 30) & 0x1ff];
	if ((d1 & 3) != D_TABLE) return d1;
	uint64_t *l2 = (uint64_t *)(d1 & 0x0000fffffffff000ULL);
	uint64_t d2 = l2[(va >> 21) & 0x1ff];
	if ((d2 & 3) != D_TABLE) return d2;
	uint64_t *l3 = (uint64_t *)(d2 & 0x0000fffffffff000ULL);
	return l3[(va >> 12) & 0x1ff];
#else
	(void)va;
	return 0;
#endif
}

/* Translate a VA with AT S1E1R; returns PAR_EL1. */
uint64_t mmu_probe(uintptr_t va)
{
	__asm__ volatile("at s1e1r, %0; isb" : : "r"(va) : "memory");
	return rsr("par_el1");
}
