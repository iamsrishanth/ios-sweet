/*
 * Minimal J8 init for bring-up.
 *
 * This is not launchd: it is a freestanding Mach-O executable that proves the
 * XNU userland path works on the J8 bring-up kernel. It opens /dev/console,
 * writes one line, and then stays alive so the kernel does not panic with
 * "init died".
 *
 * Syscall ABI (XNU arm64): x16 = syscall number, x0..x5 = args, svc #0x80,
 * return in x0.
 */

#define SYS_write   4
#define SYS_open    5
#define SYS_getpid  20

static inline long
j8_syscall(long n, long a, long b, long c)
{
	register long x16 __asm__("x16") = n;
	register long x0 __asm__("x0") = a;
	register long x1 __asm__("x1") = b;
	register long x2 __asm__("x2") = c;

	__asm__ volatile("svc #0x80"
	    : "+r"(x0)
	    : "r"(x16), "r"(x1), "r"(x2)
	    : "memory");
	return x0;
}

static const char init_message[] = "J8 init: hello from userland\n";

void
_start(void)
{
	/* O_WRONLY = 1. /dev/console exists once devfs is mounted. */
	long fd = j8_syscall(SYS_open, (long)"/dev/console", 1, 0);
	if (fd >= 0) {
		(void)j8_syscall(SYS_write, fd, (long)init_message,
		    sizeof(init_message) - 1);
	}

	for (;;) {
		(void)j8_syscall(SYS_getpid, 0, 0, 0);
	}
}
