/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Neighbor reply offload logic for ARP and NA
 *
 * Copyright 2023 Google LLC.
 */

#include "neighbor_reply_offload.h"

#ifdef linux
#include <linux/if_arp.h>
#include <linux/if_ether.h>
#include <linux/in.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/udp.h>
#include <linux/icmpv6.h>
#include <net/checksum.h>
#include <net/ndisc.h>

#include "checksum.h"
#include "nep_tables.h"
#include "netengine_utils.h"

#else
#include <net/checksum.h>
#include "net/nep_tables.h"
#include "net/netengine_utils.h"
#include "net/if_arp.h"
#include "net/if_ether.h"
#include "net/in.h"
#include "net/ip.h"
#include "net/ipv6.h"
#include "net/udp.h"
#include "net/icmpv6.h"
#include "net/ndisc.h"
#endif

// From RFC 4861, section 4.2
struct nd_opt_tll {
    uint8_t type;
    uint8_t len;
    uint8_t addr[ETH_ALEN];
} __attribute__((__packed__));

static void TransEtherTo8023Hdr(struct ethhdr *eth, struct dot11_llc_snap_header *llc_hdr, u32 len)
{
    llc_hdr->oui[0] = 0;
    llc_hdr->oui[1] = 0;
    llc_hdr->oui[2] = 0;
    llc_hdr->ctl = 0x3;
    llc_hdr->dsap = 0xAA;
    llc_hdr->ssap = 0xAA;
    llc_hdr->type = eth->h_proto;
    eth->h_proto = htons(len - ETH_HLEN);
}

static bool nep_flowid_lookup(u8 *dest_mac, nep_forward_info *nw_info)
{
    TetherFlowIdKey flow_id_k;
    TetherFlowIdValue flow_id_v;
    memset(&flow_id_k, 0, sizeof(flow_id_k));
    memcpy(flow_id_k.dstMac, dest_mac, ETH_ALEN);
    if (nep_tables_flowid_map_lookup(&flow_id_k, &flow_id_v) != NEP_MAP_TABLE_ERR_NONE)
        return false;

    nw_info->priority = flow_id_k.priority;
    nw_info->flowid = flow_id_v;
    return true;
}

#ifndef linux
static inline uint16_t csum_ipv6_magic(const struct in6_addr *saddr,
                      const struct in6_addr *daddr,
                    uint32_t len, uint8_t proto,
                    uint32_t csum)
{
    uint64_t s = csum;
    int i;
    const uint32_t *sptr = (const uint32_t *)saddr;
    const uint32_t *dptr = (const uint32_t *)daddr;

    for (i = 0; i < 4; i++) {
        uint32_t val;
        memcpy(&val, &sptr[i], sizeof(val));
        s += val;
    }
    for (i = 0; i < 4; i++) {
        uint32_t val;
        memcpy(&val, &dptr[i], sizeof(val));
        s += val;
    }
    s += (uint64_t)htonl(len);
    s += (uint64_t)htonl(proto);

    s = (s & 0xffffffff) + (s >> 32);
    s = (s & 0xffffffff) + (s >> 32);

    // Fold the 32-bit sum into 16-bits
    while (s >> 16) {
        s = (s & 0xffff) + (s >> 16);
    }

    return (uint16_t)~s;
}
#endif

static void build_na_packet(struct offload_info *offload, struct ipv6hdr *ip6h) {
    struct ipv6hdr na_ip6h = *ip6h;
    struct nd_msg *nd_msg = (struct nd_msg *)(ip6h + 1);;
    struct nd_opt_tll *tll_opt = (struct nd_opt_tll *)(nd_msg + 1);

    // IPv6 header
    memcpy(&na_ip6h.saddr, &offload->downstream_if_v6_addr, sizeof(struct in6_addr));
    memcpy(&na_ip6h.daddr, &ip6h->saddr, sizeof(struct in6_addr));
    na_ip6h.hop_limit = 255;
    *ip6h = na_ip6h;

    // ICMPv6 NA message
    nd_msg->icmph.icmp6_type = NDISC_NEIGHBOUR_ADVERTISEMENT;
    nd_msg->icmph.icmp6_code = 0;
    nd_msg->icmph.icmp6_cksum = 0;
    nd_msg->icmph.icmp6_solicited = 1;
    nd_msg->icmph.icmp6_override = 1;

    // Target link-layer address option
    tll_opt->type = ND_OPT_TARGET_LL_ADDR;
    tll_opt->len = 1;  // 1 for 8 bytes
    memcpy(tll_opt->addr, offload->downstream_if_Mac, ETH_ALEN);

    // ICMPv6 checksum
    struct in6_addr saddr, daddr;
    memcpy(&saddr, &ip6h->saddr, sizeof(struct in6_addr));
    memcpy(&daddr, &ip6h->daddr, sizeof(struct in6_addr));

    uint32_t payload_len = sizeof(struct nd_msg) + sizeof(struct nd_opt_tll);
    nd_msg->icmph.icmp6_cksum = csum_ipv6_magic(&saddr, &daddr,
                                                payload_len,
                                                IPPROTO_ICMPV6,
                                                nep_csum16_calculate(nd_msg,
                                                             payload_len));
}

int handle_neighbor_request(void **data, u32 *len, bool is_ethernet __attribute__((__unused__)),
                            nep_pkt_info *pkt_info, nep_forward_info *nw_info)
{
    struct offload_info *offload = get_tethering_offload_info();
    if (!offload)
        return NET_ENGINE_ACT_PUNT;

    struct ethhdr *eth = (struct ethhdr *)*data;
    struct dot11_llc_snap_header *llc_hdr = (struct dot11_llc_snap_header *)(eth +1);

    if (eth->h_proto == htons(ETH_P_ARP)) {
        if (*len < sizeof(struct ethhdr) + sizeof(struct arphdr) + 2 * ETH_ALEN + 2 * sizeof(struct in_addr)) {
            pr_err("ARP packet too short: %u", *len);
            return NET_ENGINE_ACT_PUNT;
        }
        struct arphdr *arp = (struct arphdr *)(eth + 1);
        if ((uint16_t)arp->ar_op == (uint16_t)htons(ARPOP_REQUEST)) {
            // ARP Request
            struct in_addr target_ip;
            memcpy(&target_ip, (uint8_t *)(arp + 1) + 2 * ETH_ALEN + sizeof(struct in_addr), sizeof(struct in_addr));

            if (target_ip.s_addr == offload->downstream_if_v4_addr.s_addr) {
                hexdump("ARP IN(target_ip): ", (uint8_t*) &target_ip, sizeof(struct in_addr));
                if (nep_flowid_lookup(eth->h_source, nw_info)) {
                    // Target IP matches downstream interface IP, build ARP reply
                    struct ethhdr reply_eth = *eth;
                    struct arphdr *reply_arp = (struct arphdr *)(eth + 1);
                    uint8_t *arp_ptr = (uint8_t *)(reply_arp + 1);

                    // Ethernet header
                    memcpy(reply_eth.h_dest, reply_eth.h_source, ETH_ALEN);
                    memcpy(reply_eth.h_source, offload->downstream_if_Mac, ETH_ALEN);
                    eth = (struct ethhdr *)((uint8_t *)eth - DOT11_LLC_SNAP_HDR_LEN);
                    *eth = reply_eth;

                    // LLC/SNAP header
                    llc_hdr = (struct dot11_llc_snap_header *)(eth + 1);
                    TransEtherTo8023Hdr(eth, llc_hdr, *len + DOT11_LLC_SNAP_HDR_LEN);

                    // ARP header
                    reply_arp->ar_op = (uint16_t)htons(ARPOP_REPLY);

                    // ARP data
                    memcpy(arp_ptr + ETH_ALEN + sizeof(struct in_addr), arp_ptr, ETH_ALEN + sizeof(struct in_addr));
                    memcpy(arp_ptr, offload->downstream_if_Mac, ETH_ALEN);
                    memcpy(arp_ptr + ETH_ALEN, &offload->downstream_if_v4_addr, sizeof(struct in_addr));

                    // Set forward info
                    *len += DOT11_LLC_SNAP_HDR_LEN;
                    *data = eth;
                    pkt_info->action = PKT_ACTION_FORWARD_TO_WLAN;
                    nw_info->is_wlan_forward = true;
                    return NET_ENGINE_ACT_FORWARD;
                } else {
                    pr_err("Fail to lookup flow id");
                }
            }
        }
    } else if (eth->h_proto == htons(ETH_P_IPV6)) {
        if (*len < sizeof(struct ethhdr) + sizeof(struct ipv6hdr)) {
            pr_err("IPv6 packet too short: %u", *len);
            return NET_ENGINE_ACT_PUNT;
        }
        struct ipv6hdr *ip6h = (struct ipv6hdr *)(eth + 1);
        if (ip6h->nexthdr == IPPROTO_ICMPV6) {
            if (*len < sizeof(struct ethhdr) + sizeof(struct ipv6hdr) + sizeof(struct icmp6hdr)) {
                pr_err("ICMPv6 packet too short: %u", *len);
                return NET_ENGINE_ACT_PUNT;
            }
            struct nd_msg *nd_msg = (struct nd_msg *)(ip6h + 1);
            if (nd_msg->icmph.icmp6_type == NDISC_NEIGHBOUR_SOLICITATION) {
                // Neighbor Solicitation
                if (*len < sizeof(struct ethhdr) + sizeof(struct ipv6hdr) + sizeof(struct nd_msg)) {
                    pr_err("NS packet too short: %u", *len);
                    return NET_ENGINE_ACT_PUNT;
                }
                if (*len > sizeof(struct ethhdr) + sizeof(struct ipv6hdr) + sizeof(struct nd_msg) + sizeof(struct nd_opt_tll)) {
                    pr_err("NS packet too big: %u", *len);
                    return NET_ENGINE_ACT_PUNT;
                }

                if (memcmp(&nd_msg->target, &offload->downstream_if_v6_addr, sizeof(struct in6_addr)) == 0) {
                    hexdump("NS IN(target_ip): ", (uint8_t*) &nd_msg->target, sizeof(struct in6_addr));
                    if (nep_flowid_lookup(eth->h_source, nw_info)) {
                        struct ethhdr reply_eth = *eth;
                        // Ethernet header
                        memcpy(reply_eth.h_dest, reply_eth.h_source, ETH_ALEN);
                        memcpy(reply_eth.h_source, offload->downstream_if_Mac, ETH_ALEN);
                        eth = (struct ethhdr *)((uint8_t *)eth - DOT11_LLC_SNAP_HDR_LEN);
                        *eth = reply_eth;

                        // LLC/SNAP header
                        llc_hdr = (struct dot11_llc_snap_header *)(eth + 1);
                        TransEtherTo8023Hdr(eth, llc_hdr, *len + DOT11_LLC_SNAP_HDR_LEN);

                        // Target IP matches downstream interface IP, build NA packet
                        build_na_packet(offload, ip6h);

                        // Set forward info
                        *len += DOT11_LLC_SNAP_HDR_LEN;
                        *data = eth;
                        pkt_info->action = PKT_ACTION_FORWARD_TO_WLAN;
                        nw_info->is_wlan_forward = true;

                        return NET_ENGINE_ACT_FORWARD;
                    } else {
                        pr_err("Fail to lookup flow id");
                    }
                }
            }
        }
    }

    return NET_ENGINE_ACT_PUNT;
}
