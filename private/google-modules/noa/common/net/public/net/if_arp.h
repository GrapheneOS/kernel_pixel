/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * ARP protocol definitions
 *
 * Copyright 2023 Google LLC.
 */
#ifndef _NET_IF_ARP_H
#define _NET_IF_ARP_H

#ifdef linux
#include <linux/if_arp.h>
#else /* linux */
#include <cstdint>
#endif

/* ARP protocol OPCODES. */
#define ARPOP_REQUEST 1 /* ARP request */
#define ARPOP_REPLY 2   /* ARP reply */

struct arphdr {
  uint16_t ar_hrd;         /* format of hardware address */
  uint16_t ar_pro;         /* format of protocol address */
  uint8_t ar_hln;  /* length of hardware address */
  uint8_t ar_pln;  /* length of protocol address */
  uint16_t ar_op;          /* ARP opcode (command) */
};

#endif  // _NET_IF_ARP_H
