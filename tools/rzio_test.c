// SPDX-License-Identifier: GPL-2.0
/*
 * Smoke test for /dev/rzio: INFO, PING, per-open state, unknown commands.
 * Exits 0 when everything checks out.
 *
 * Usage: rzio_test [device]
 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "rzio.h"

#define FAIL(...)						\
	do {							\
		fprintf(stderr, "FAIL: " __VA_ARGS__);		\
		fputc('\n', stderr);				\
		return 1;					\
	} while (0)

int main(int argc, char **argv)
{
	const char *dev = argc > 1 ? argv[1] : "/dev/rzio";
	struct rzio_info a, b;
	__u64 pings;
	int fa, fb;

	fa = open(dev, O_RDWR | O_CLOEXEC);
	if (fa < 0)
		FAIL("open %s: %s", dev, strerror(errno));
	if (ioctl(fa, RZIO_IOC_INFO, &a))
		FAIL("INFO: %s", strerror(errno));
	if (a.abi_version != RZIO_ABI_VERSION)
		FAIL("module speaks abi %u, this tool abi %u",
		     a.abi_version, RZIO_ABI_VERSION);
	printf("%s: abi %u, ctx %u\n", dev, a.abi_version, a.ctx_id);

	for (__u64 want = 1; want <= 3; want++) {
		if (ioctl(fa, RZIO_IOC_PING, &pings))
			FAIL("PING: %s", strerror(errno));
		if (pings != want)
			FAIL("PING returned %llu, want %llu",
			     (unsigned long long)pings, (unsigned long long)want);
	}
	printf("ctx %u: 3 pings counted\n", a.ctx_id);

	/* A second open() is a second context with its own counter. */
	fb = open(dev, O_RDWR | O_CLOEXEC);
	if (fb < 0)
		FAIL("second open: %s", strerror(errno));
	if (ioctl(fb, RZIO_IOC_INFO, &b))
		FAIL("INFO on second fd: %s", strerror(errno));
	if (b.ctx_id == a.ctx_id)
		FAIL("both fds share ctx %u", a.ctx_id);
	if (ioctl(fb, RZIO_IOC_PING, &pings))
		FAIL("PING on second fd: %s", strerror(errno));
	if (pings != 1)
		FAIL("first PING on ctx %u returned %llu",
		     b.ctx_id, (unsigned long long)pings);
	printf("ctx %u: separate state\n", b.ctx_id);

	/* Commands the module doesn't know must fail with ENOTTY. */
	if (ioctl(fa, _IO(RZIO_IOC_MAGIC, 0xff)) != -1 || errno != ENOTTY)
		FAIL("unknown command was not rejected with ENOTTY");
	printf("unknown command rejected\n");

	close(fb);
	close(fa);
	printf("ok\n");
	return 0;
}
