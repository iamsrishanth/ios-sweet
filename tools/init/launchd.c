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

#define SYS_exit    1
#define SYS_fork    2
#define SYS_wait4   7
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

static inline long
j8_syscall4(long n, long a, long b, long c, long d)
{
	register long x16 __asm__("x16") = n;
	register long x0 __asm__("x0") = a;
	register long x1 __asm__("x1") = b;
	register long x2 __asm__("x2") = c;
	register long x3 __asm__("x3") = d;

	__asm__ volatile("svc #0x80"
	    : "+r"(x0)
	    : "r"(x16), "r"(x1), "r"(x2), "r"(x3)
	    : "memory");
	return x0;
}

static const char init_message[] = "J8 init: hello from userland\n";
static const char child_message[] = "J8 init: fork child ran\n";
static const char parent_message[] = "J8 init: parent reaped child\n";

void
start(void)
{
	/* O_WRONLY = 1. /dev/console exists once devfs is mounted. */
	long fd = j8_syscall(SYS_open, (long)"/dev/console", 1, 0);
	if (fd >= 0) {
		(void)j8_syscall(SYS_write, fd, (long)init_message,
		    sizeof(init_message) - 1);
	}

	/* Exercise process creation and wait/reap before remaining resident. */
	long child = j8_syscall(SYS_fork, 0, 0, 0);
	if (child == 0) {
		/* Re-open in the child so this also tests post-fork fd setup. */
		long child_fd = j8_syscall(SYS_open, (long)"/dev/console", 1, 0);
		if (child_fd >= 0) {
			(void)j8_syscall(SYS_write, child_fd, (long)child_message,
			    sizeof(child_message) - 1);
		}
		(void)j8_syscall(SYS_exit, 42, 0, 0);
		for (;;) { }
	}
	if (child > 0) {
		int status = 0;
		(void)j8_syscall4(SYS_wait4, child, (long)&status, 0, 0);
		if (fd >= 0) {
			(void)j8_syscall(SYS_write, fd, (long)parent_message,
			    sizeof(parent_message) - 1);
		}
	}

	for (;;) {
		(void)j8_syscall(SYS_getpid, 0, 0, 0);
	}
}
