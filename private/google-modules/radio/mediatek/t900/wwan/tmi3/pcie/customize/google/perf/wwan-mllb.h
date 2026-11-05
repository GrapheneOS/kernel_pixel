/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2026 Google LLC.
 */

#ifndef _WWAN_MLLB_H_
#define _WWAN_MLLB_H_

#include <linux/types.h>
#include <linux/skbuff.h>
#include "../common/radio-google.h"
#include "mtk_dev.h"

/**
 * enum wwan_mllb_mode - Operating modes for MLLB
 * @WWAN_MLLB_MONITOR: Count packets only; pass them to the network stack.
 * @WWAN_MLLB_SINK_NAPI: Count packets and free them immediately at NAPI level.
 * @WWAN_MLLB_SINK_STEER: Count packets and free them immediately at Steer thread level.
 * @WWAN_MLLB_SINK_TX_ENTRY: Count packets and free them immediately at TX entry.
 * @WWAN_MLLB_SINK_TX_VQ: Count packets and free them immediately at TX VQ dequeue.
 */
enum wwan_mllb_mode {
	WWAN_MLLB_MONITOR = 0,
	WWAN_MLLB_SINK_NAPI = 1,
	WWAN_MLLB_SINK_STEER = 2,
	WWAN_MLLB_SINK_TX_ENTRY = 3,
	WWAN_MLLB_SINK_TX_VQ = 4,
	WWAN_MLLB_MODE_MAX,
};

typedef void (*wwan_mllb_tx_kick_fn)(struct mtk_md_dev *mdev);

#if IS_ENABLED(CONFIG_GOOGLE_WWAN_MLLB)
/**
 * wwan_mllb_is_enabled - Check if the benchmarking session is active.
 *
 * Return: true if MLLB is currently sampling data.
 */
bool wwan_mllb_is_enabled(void);

/**
 * wwan_mllb_get_mode - Get the current MLLB operational mode.
 *
 * Return: Current mode enum value.
 */
int wwan_mllb_get_mode(void);

/**
 * wwan_mllb_collect_stats - Record packet statistics.
 * @skb: Pointer to the socket buffer being processed.
 * @mode: Operational mode snapshot.
 *
 * Updates per-CPU counters and handles packet freeing if in SINK_NAPI mode.
 */
void wwan_mllb_collect_stats(struct sk_buff *skb, int mode);

/**
 * wwan_mllb_throttle_allow - NAPI entry gatekeeper check.
 * @rxq_id: The rxq id corresponds to the NAPI context
 *
 * Checks if the token bucket has enough credit to allow a NAPI poll cycle.
 * Return: true if the poll cycle is allowed; false to trigger backpressure.
 */
bool wwan_mllb_throttle_allow(u32 rxq_id);

/**
 * wwan_mllb_throttle_update - Precision quota deduction.
 * @len: Packet length in bytes.
 *
 * Deducts bits from the token bucket based on processed payload size.
 */
void wwan_mllb_throttle_update(u32 len);

/**
 * wwan_mllb_bat_reload_hook - Record BAT reload statistics.
 * @is_frag: true if FRAG_BAT, false if NORMAL_BAT.
 * @bytes: Total bytes reloaded in this batch (cnt * buf_size).
 */
void wwan_mllb_record_bat_reload(bool is_frag, u32 bytes);
static inline void wwan_mllb_bat_reload_hook(bool is_frag, u32 bytes)
{
	if (unlikely(wwan_mllb_is_enabled() && bytes))
		wwan_mllb_record_bat_reload(is_frag, bytes);
}

/**
 * wwan_mllb_register_tx_kick_cb - Register WWAN TX kick callback.
 */
void wwan_mllb_register_tx_kick_cb(wwan_mllb_tx_kick_fn cb);

/**
 * wwan_mllb_register_ws - Register wakeup source from the WWAN driver.
 */
void wwan_mllb_register_ws(u32 rxq_id, struct wakeup_source *ws);

/**
 * wwan_mllb_register_napi - Register NAPI instance from the WWAN driver.
 */
void wwan_mllb_register_napi(u32 rxq_id, struct napi_struct *napi);

/**
 * wwan_mllb_init_queues - Register resources for all the RXQs.
 */
void wwan_mllb_init_queues(struct mtk_md_dev *mdev, u32 rxq_cnt);

/**
 * wwan_mllb_exit_queues - Deregister resources for all the RXQs.
 */
void wwan_mllb_exit_queues(void);

/**
 * wwan_mllb_napi_hook - Dynamic hook for WWAN data path (NAPI boundary)
 * @skb: Current packet
 *
 * Return: true if packet was consumed (Sink NAPI Mode), false otherwise.
 */
static inline bool wwan_mllb_napi_hook(struct sk_buff *skb)
{
	int mode;

	if (unlikely(wwan_mllb_is_enabled() && skb)) {
		mode = wwan_mllb_get_mode();
		wwan_mllb_collect_stats(skb, mode);
		return mode == WWAN_MLLB_SINK_NAPI;
	}
	return false;
}

void wwan_mllb_collect_steer_stats(struct sk_buff *skb, int mode);

/**
 * wwan_mllb_steer_hook - Dynamic hook for Steer Thread boundary
 * @skb: Current packet being committed to network stack
 *
 * Return: true if packet was consumed (Sink Steer Mode), false otherwise.
 */
static inline bool wwan_mllb_steer_hook(struct sk_buff *skb)
{
	int mode;

	if (unlikely(wwan_mllb_is_enabled() && skb)) {
		mode = wwan_mllb_get_mode();
		wwan_mllb_collect_steer_stats(skb, mode);
		return mode == WWAN_MLLB_SINK_STEER;
	}
	return false;
}

/* TX MLLB Hooks & Stats Functions */
bool wwan_mllb_tx_throttle_check(void);

static inline bool wwan_mllb_tx_throttle_allow(void)
{
	if (unlikely(wwan_mllb_is_enabled()))
		return wwan_mllb_tx_throttle_check();
	return true;
}

void wwan_mllb_collect_tx_entry_stats(struct sk_buff *skb, int mode);
void wwan_mllb_collect_tx_vq_enq_stats(struct sk_buff *skb);
void wwan_mllb_collect_tx_vq_deq_stats(struct sk_buff *skb, int mode);
void wwan_mllb_collect_tx_drb_fill_stats(struct sk_buff *skb);
void wwan_mllb_collect_tx_retire_stats(struct sk_buff *skb);

static inline bool wwan_mllb_tx_entry_hook(struct sk_buff *skb)
{
	int mode;

	if (unlikely(wwan_mllb_is_enabled() && skb)) {
		mode = wwan_mllb_get_mode();
		wwan_mllb_collect_tx_entry_stats(skb, mode);
		return mode == WWAN_MLLB_SINK_TX_ENTRY;
	}
	return false;
}

static inline void wwan_mllb_tx_vq_enq_hook(struct sk_buff *skb)
{
	if (unlikely(wwan_mllb_is_enabled() && skb))
		wwan_mllb_collect_tx_vq_enq_stats(skb);
}

static inline bool wwan_mllb_tx_vq_deq_hook(struct sk_buff *skb)
{
	int mode;

	if (unlikely(wwan_mllb_is_enabled() && skb)) {
		mode = wwan_mllb_get_mode();
		wwan_mllb_collect_tx_vq_deq_stats(skb, mode);
		return mode == WWAN_MLLB_SINK_TX_VQ;
	}
	return false;
}

static inline void wwan_mllb_tx_drb_fill_hook(struct sk_buff *skb)
{
	if (unlikely(wwan_mllb_is_enabled() && skb))
		wwan_mllb_collect_tx_drb_fill_stats(skb);
}

static inline void wwan_mllb_tx_retire_hook(struct sk_buff *skb)
{
	if (unlikely(wwan_mllb_is_enabled() && skb))
		wwan_mllb_collect_tx_retire_stats(skb);
}

int wwan_mllb_init(void);
void wwan_mllb_exit(void);

#else /* CONFIG_GOOGLE_WWAN_MLLB */

static inline bool wwan_mllb_is_enabled(void)
{
	return false;
}

static inline void wwan_mllb_collect_stats(struct sk_buff *skb, int mode)
{
}

static inline bool wwan_mllb_napi_hook(struct sk_buff *skb)
{
	return false;
}

static inline void wwan_mllb_collect_steer_stats(struct sk_buff *skb, int mode)
{
}

static inline bool wwan_mllb_steer_hook(struct sk_buff *skb)
{
	return false;
}

static inline bool wwan_mllb_tx_entry_hook(struct sk_buff *skb)
{
	return false;
}

static inline void wwan_mllb_tx_vq_enq_hook(struct sk_buff *skb)
{
}

static inline bool wwan_mllb_tx_vq_deq_hook(struct sk_buff *skb)
{
	return false;
}

static inline void wwan_mllb_tx_drb_fill_hook(struct sk_buff *skb)
{
}

static inline void wwan_mllb_tx_retire_hook(struct sk_buff *skb)
{
}

static inline int wwan_mllb_init(void)
{
	return 0;
}

static inline void wwan_mllb_exit(void)
{
}

static inline int wwan_mllb_get_mode(void)
{
	return WWAN_MLLB_MONITOR;
}

static inline void wwan_mllb_throttle_update(u32 len)
{
}

static inline void wwan_mllb_bat_reload_hook(bool is_frag, u32 bytes)
{
}

static inline bool wwan_mllb_throttle_allow(u32 q_id)
{
	return true;
}

static inline bool wwan_mllb_tx_throttle_allow(void)
{
	return true;
}

static inline void wwan_mllb_register_tx_kick_cb(wwan_mllb_tx_kick_fn cb)
{
}

static inline void wwan_mllb_register_ws(u32 rxq_id, struct wakeup_source *ws)
{
}

static inline void wwan_mllb_register_napi(u32 rxq_id, struct napi_struct *napi)
{
}

static inline void wwan_mllb_init_queues(struct mtk_md_dev *mdev, u32 rxq_cnt)
{
}

static inline void wwan_mllb_exit_queues(void)
{
}

#endif /* CONFIG_GOOGLE_WWAN_MLLB */

#endif /* _WWAN_MLLB_H_ */
