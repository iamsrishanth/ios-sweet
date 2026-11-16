/*
 * j8probe - read AArch64 identification registers from EL0 on the Galaxy J8.
 *
 * Purpose: find out whether the SoC implements the 16 KiB translation granule
 * (ID_AA64MMFR0_EL1.TGran16), which the reference XNU/iOS 16 ports assume.
 *
 * Result on SM-J810G (Havoc-OS 4.19, kernel 3.18.140), 2026-11-17:
 *
 *     j8probe: AArch64 EL0 identification probe
 *     uname: sysname=Linux machine=armv8l release=3.18.140
 *     os page size (sysconf): 4096
 *     AT_HWCAP=0xff AT_HWCAP2=0x0
 *     ELF0-ID-READ: TRAPPED (SIGILL) - cannot read ID registers at EL0
 *
 * Interpretation: 64-bit EL0 works on this kernel (this binary is AArch64 and
 * ran), but Samsung's kernel traps EL0 reads of the ID registers to EL1, so
 * TGran16 cannot be read from userspace. The value therefore has to come from
 * the documented core (Cortex-A53 has no 16K granule) or from an EL1 probe
 * (tools/j8/granule-probe-el1.S).
 *
 * Build: tools/j8/build-probe.sh
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <signal.h>
#include <setjmp.h>
#include <unistd.h>
#include <sys/utsname.h>
#include <sys/auxv.h>

static sigjmp_buf jb;

static void on_sigill(int sig)
{
	(void)sig;
	siglongjmp(jb, 1);
}

static uint64_t rd_mmfr0(void)
{
	uint64_t v = 0;
	__asm__ volatile("mrs %0, id_aa64mmfr0_el1" : "=r"(v));
	return v;
}
static uint64_t rd_pfr0(void)
{
	uint64_t v = 0;
	__asm__ volatile("mrs %0, id_aa64pfr0_el1" : "=r"(v));
	return v;
}
static uint64_t rd_midr(void)
{
	uint64_t v = 0;
	__asm__ volatile("mrs %0, midr_el1" : "=r"(v));
	return v;
}
static uint64_t rd_ctr(void)
{
	uint64_t v = 0;
	__asm__ volatile("mrs %0, ctr_el0" : "=r"(v));
	return v;
}

static const char *tg(uint64_t v, int shift)
{
	switch ((v >> shift) & 0xf) {
	case 0: return "not supported";
	case 1: return "supported";
	case 2: return "supported, 52-bit input (FEAT_LPA2)";
	default: return "reserved";
	}
}

int main(void)
{
	struct sigaction sa;
	struct utsname u;
	uint64_t mmfr0, pfr0, midr, ctr;

	setvbuf(stdout, NULL, _IONBF, 0);
	memset(&sa, 0, sizeof sa);
	sa.sa_handler = on_sigill;
	sa.sa_flags = SA_NODEFER;
	sigaction(SIGILL, &sa, NULL);

	printf("j8probe: AArch64 EL0 identification probe\n");
	if (uname(&u) == 0)
		printf("uname: sysname=%s machine=%s release=%s\n",
		       u.sysname, u.machine, u.release);
	printf("os page size (sysconf): %ld\n", sysconf(_SC_PAGESIZE));
	printf("AT_HWCAP=0x%lx AT_HWCAP2=0x%lx\n",
	       getauxval(AT_HWCAP), getauxval(AT_HWCAP2));

	if (sigsetjmp(jb, 1) != 0) {
		printf("EL0-ID-READ: TRAPPED (SIGILL) - cannot read ID registers at EL0\n");
		return 3;
	}
	mmfr0 = rd_mmfr0();
	pfr0 = rd_pfr0();
	midr = rd_midr();
	ctr = rd_ctr();

	printf("ID_AA64MMFR0_EL1 = 0x%016llx\n", (unsigned long long)mmfr0);
	printf("  TGran4 [15:12] = %s\n", tg(mmfr0, 12));
	printf("  TGran16[23:20] = %s\n", tg(mmfr0, 20));
	printf("  TGran64[27:24] = %s\n", tg(mmfr0, 24));
	printf("  PARange[3:0]   = %llu\n", (unsigned long long)(mmfr0 & 0xf));
	printf("ID_AA64PFR0_EL1  = 0x%016llx\n", (unsigned long long)pfr0);
	printf("  EL0[3:0]=%llu EL1[7:4]=%llu EL2[11:8]=%llu EL3[15:12]=%llu\n",
	       (unsigned long long)((pfr0 >> 0) & 0xf),
	       (unsigned long long)((pfr0 >> 4) & 0xf),
	       (unsigned long long)((pfr0 >> 8) & 0xf),
	       (unsigned long long)((pfr0 >> 12) & 0xf));
	printf("MIDR_EL1         = 0x%016llx (implementer 0x%02x part 0x%03x variant %u rev %u)\n",
	       (unsigned long long)midr, (unsigned)((midr >> 24) & 0xff),
	       (unsigned)((midr >> 4) & 0xfff), (unsigned)((midr >> 20) & 0xf),
	       (unsigned)(midr & 0xf));
	printf("CTR_EL0          = 0x%016llx (DminLine %u, IminLine %u)\n",
	       (unsigned long long)ctr, 4u << ((ctr >> 16) & 0xf),
	       4u << (ctr & 0xf));
	return 0;
}
