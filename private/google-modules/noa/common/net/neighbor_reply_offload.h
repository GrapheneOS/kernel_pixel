/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Neighbor reply offload logic for ARP and NA
 *
 * Copyright 2023 Google LLC.
 */
#ifndef NEIGHBOR_REPLY_OFFLOAD_H_
#define NEIGHBOR_REPLY_OFFLOAD_H_

#include "net/public/net/offload.h"
#include "net/public/net/nep_helpers.h"

int handle_neighbor_request(void **data, u32 *len, bool is_ethernet,
                            nep_pkt_info *pkt_info, nep_forward_info *nw_info);

#endif  // NEIGHBOR_REPLY_OFFLOAD_H_
