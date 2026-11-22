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
#define SYS_read    3
#define SYS_wait4   7
#define SYS_write   4
#define SYS_open    5
#define SYS_close   6
#define SYS_getpid  20
#define SYS_pipe    42

typedef unsigned long long uint64_t;
typedef unsigned int uint32_t;

#define MACH_TRAP_PORT_ALLOCATE   (-16L)
#define MACH_TRAP_PORT_INSERT     (-21L)
#define MACH_TRAP_TASK_SELF       (-28L)
#define MACH_TRAP_MSG2            (-47L)
#define MACH_TRAP_MSG             (-31L)
#define MACH_PORT_RIGHT_RECEIVE   1L
#define MACH_MSG_TYPE_COPY_SEND   19U
#define MACH_MSG_TYPE_MAKE_SEND   20L
#define MACH_MSGH_BITS(remote, local) ((remote) | ((local) << 8))
#define MACH_SEND_MSG              1L
#define MACH_RCV_MSG               2L
#define MACH64_MACH_MSG2           0x8000000000000000ULL
#define MACH64_SEND_MQ_CALL        0x0000000400000000ULL
#define MACH_RCV_TIMEOUT           0x00000100L
#define KERN_SUCCESS               0L

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

struct syscall_pair {
	long x0;
	long x1;
};

static inline struct syscall_pair
j8_syscall_pair(long n)
{
	register long x16 __asm__("x16") = n;
	register long x0 __asm__("x0") = 0;
	register long x1 __asm__("x1") = 0;

	__asm__ volatile("svc #0x80"
	    : "+r"(x0), "+r"(x1)
	    : "r"(x16)
	    : "memory");
	struct syscall_pair result = {x0, x1};
	return result;
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
static const char pipe_create_message[] = "J8 init: pipe created\n";
static const char pipe_write_message[] = "J8 init: pipe write ok\n";
static const char pipe_read_message[] = "J8 init: pipe read ok\n";
static const char pipe_message[] = "J8 init: pipe roundtrip ok\n";
static const char pipe_fail_message[] = "J8 init: pipe test failed\n";
static const char mach_port_message[] = "J8 init: mach port allocated\n";
static const char mach_ipc_message[] = "J8 init: mach ipc roundtrip ok\n";
static const char mach_fail_message[] = "J8 init: mach ipc test failed\n";

struct mach_msg_header {
	unsigned int bits;
	unsigned int size;
	unsigned int remote;
	unsigned int local;
	unsigned int voucher;
	int id;
};

struct mach_probe_message {
	struct mach_msg_header header;
	char payload[16];
};

static inline long
j8_mach_trap3(long trap, long a, long b, long c)
{
	register long x16 __asm__("x16") = trap;
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
j8_mach_trap4(long trap, long a, long b, long c, long d)
{
	register long x16 __asm__("x16") = trap;
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

static inline long
j8_mach_trap8(long trap, long a, long b, long c, long d, long e, long f, long g, long h)
{
	register long x16 __asm__("x16") = trap;
	register long x0 __asm__("x0") = a;
	register long x1 __asm__("x1") = b;
	register long x2 __asm__("x2") = c;
	register long x3 __asm__("x3") = d;
	register long x4 __asm__("x4") = e;
	register long x5 __asm__("x5") = f;
	register long x6 __asm__("x6") = g;
	register long x7 __asm__("x7") = h;
	__asm__ volatile("svc #0x80"
	    : "+r"(x0)
	    : "r"(x16), "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x5), "r"(x6), "r"(x7)
	    : "memory");
	return x0;
}

static inline long
j8_mach_trap7(long trap, long a, long b, long c, long d, long e, long f, long g)
{
	register long x16 __asm__("x16") = trap;
	register long x0 __asm__("x0") = a;
	register long x1 __asm__("x1") = b;
	register long x2 __asm__("x2") = c;
	register long x3 __asm__("x3") = d;
	register long x4 __asm__("x4") = e;
	register long x5 __asm__("x5") = f;
	register long x6 __asm__("x6") = g;
	__asm__ volatile("svc #0x80"
	    : "+r"(x0)
	    : "r"(x16), "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x5), "r"(x6)
	    : "memory");
	return x0;
}

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

	/* Exercise a kernel pipe and both read/write paths. */
	int pipefd[2] = {-1, -1};
	struct syscall_pair pipe_pair = j8_syscall_pair(SYS_pipe);
	long pipe_result = pipe_pair.x0;
	if (pipe_result >= 0) {
		pipefd[0] = (int)pipe_pair.x0;
		pipefd[1] = (int)pipe_pair.x1;
	}
	if (pipe_result >= 0 && fd >= 0) {
		(void)j8_syscall(SYS_write, fd, (long)pipe_create_message,
		    sizeof(pipe_create_message) - 1);
	}
	if (pipe_result >= 0) {
		static const char token[] = "j8-pipe";
		char roundtrip[8] = {0};
		long nw = j8_syscall(SYS_write, pipefd[1], (long)token, sizeof(token) - 1);
		if (nw == (long)(sizeof(token) - 1) && fd >= 0) {
			(void)j8_syscall(SYS_write, fd, (long)pipe_write_message,
			    sizeof(pipe_write_message) - 1);
		}
		long nr = j8_syscall(SYS_read, pipefd[0], (long)roundtrip, sizeof(token) - 1);
		if (nr == (long)(sizeof(token) - 1) && fd >= 0) {
			(void)j8_syscall(SYS_write, fd, (long)pipe_read_message,
			    sizeof(pipe_read_message) - 1);
		}
		(void)j8_syscall(SYS_close, pipefd[0], 0, 0);
		(void)j8_syscall(SYS_close, pipefd[1], 0, 0);
		if (nr == (long)(sizeof(token) - 1) && roundtrip[0] == 'j' && fd >= 0) {
			(void)j8_syscall(SYS_write, fd, (long)pipe_message,
			    sizeof(pipe_message) - 1);
		}
	} else if (fd >= 0) {
		(void)j8_syscall(SYS_write, fd, (long)pipe_fail_message,
		    sizeof(pipe_fail_message) - 1);
	}

	/* Exercise the basic Mach port and same-task message path. */
	unsigned int self = (unsigned int)j8_mach_trap3(MACH_TRAP_TASK_SELF, 0, 0, 0);
	unsigned int port = 0;
	long kr = j8_mach_trap3(MACH_TRAP_PORT_ALLOCATE, self,
	    MACH_PORT_RIGHT_RECEIVE, (long)&port);
	if (kr == KERN_SUCCESS && fd >= 0) {
		(void)j8_syscall(SYS_write, fd, (long)mach_port_message,
		    sizeof(mach_port_message) - 1);
	}
	if (kr == KERN_SUCCESS) {
		long insert_result = j8_mach_trap4(MACH_TRAP_PORT_INSERT, self, port,
		    port, MACH_MSG_TYPE_MAKE_SEND);
		struct mach_probe_message sent = {
			.header = {MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND, 0), sizeof(struct mach_msg_header) + 8,
			    port, 0, 0, 0x4a38},
			.payload = "j8-mach"
		};
		struct mach_probe_message received = {{0}, {0}};
		uint64_t send_bits_size = ((uint64_t)sent.header.size << 32) | sent.header.bits;
		uint64_t send_ports = (uint64_t)port;
		uint64_t send_voucher_id = ((uint64_t)(uint32_t)sent.header.id << 32);
		uint64_t send_desc_rcv = 0;
		uint64_t send_rcv_pri = 0;
		long send_result = j8_mach_trap8(MACH_TRAP_MSG2, (long)&sent,
		    MACH_SEND_MSG | MACH64_MACH_MSG2 | MACH64_SEND_MQ_CALL, (long)send_bits_size,
		    (long)send_ports, (long)send_voucher_id, (long)send_desc_rcv,
		    (long)send_rcv_pri, 0);
		uint64_t recv_bits_size = (uint64_t)sizeof(received) << 32;
		uint64_t recv_ports = 0;
		uint64_t recv_voucher_id = 0;
		uint64_t recv_desc_rcv = (uint64_t)port << 32;
		uint64_t recv_rcv_pri = (uint64_t)sizeof(received);
		long receive_result = j8_mach_trap8(MACH_TRAP_MSG2, (long)&received,
		    MACH_RCV_MSG | MACH_RCV_TIMEOUT | MACH64_MACH_MSG2 | MACH64_SEND_MQ_CALL,
		    (long)recv_bits_size, (long)recv_ports, (long)recv_voucher_id,
		    (long)recv_desc_rcv, (long)recv_rcv_pri, 1);
		if (insert_result == KERN_SUCCESS && send_result == KERN_SUCCESS && receive_result == KERN_SUCCESS &&
		    received.header.id == 0x4a38 && received.payload[0] == 'j' && fd >= 0) {
			(void)j8_syscall(SYS_write, fd, (long)mach_ipc_message,
			    sizeof(mach_ipc_message) - 1);
		} else if (fd >= 0) {
			(void)j8_syscall(SYS_write, fd, (long)mach_fail_message,
			    sizeof(mach_fail_message) - 1);
		}
	}

	for (;;) {
		(void)j8_syscall(SYS_getpid, 0, 0, 0);
	}
}
