/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _NET_NDISC_H
#define _NET_NDISC_H

#ifdef linux
#include <linux/types.h>
#include <linux/in6.h>
#include "icmpv6.h"
#endif

struct nd_msg {
	struct icmp6hdr icmph;
	struct in6_addr target;
	__u8 opt[0];
};

#define NDISC_NEIGHBOUR_SOLICITATION	135
#define NDISC_NEIGHBOUR_ADVERTISEMENT	136

enum {
	ND_OPT_SOURCE_LL_ADDR = 1,
	ND_OPT_TARGET_LL_ADDR = 2,
	ND_OPT_PREFIX_INFO = 3,
	ND_OPT_REDIRECT_HDR = 4,
	ND_OPT_MTU = 5,
};

#endif
