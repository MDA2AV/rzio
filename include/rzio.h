/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * rzio user-space ABI, shared by the module and the programs that use it.
 *
 * Unstable while rzio is being built: every change bumps RZIO_ABI_VERSION,
 * and programs check it with RZIO_IOC_INFO before anything else.
 */
#ifndef _RZIO_H
#define _RZIO_H

#include <linux/ioctl.h>
#include <linux/types.h>

#define RZIO_ABI_VERSION	0

/* ioctl type byte; rzio commands are only valid on /dev/rzio fds. */
#define RZIO_IOC_MAGIC		0xE7

struct rzio_info {
	__u32 abi_version;
	__u32 ctx_id;		/* unique per open() since the module loaded */
};

/* Describe this context. */
#define RZIO_IOC_INFO	_IOR(RZIO_IOC_MAGIC, 0x00, struct rzio_info)
/* Bump this context's ping counter and return the new value. */
#define RZIO_IOC_PING	_IOR(RZIO_IOC_MAGIC, 0x01, __u64)

#endif /* _RZIO_H */
