// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Google LLC.
 */

#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/ktime.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/percpu.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/bitops.h>
#include <linux/seq_file.h>
#include "wwan-mllb.h"
#include "radio-utils.h"
#include "feature-control.h"
#include "mtk_dev.h"

#if IS_ENABLED(CONFIG_GOOGLE_WWAN_MLLB)

#define MLLB_HISTORY_SIZE 256
#define SAMPLE_WORK_GRANULE_MS 100
#define SEC_TO_NS(sec) (1000000000ULL * (sec))
#define MBITS_TO_BITS(mbps) ((mbps) * 1000000ULL)

static bool mllb_enabled;
static u32 mllb_mode = WWAN_MLLB_MONITOR;
static u32 mllb_target_mbps;
static wwan_mllb_tx_kick_fn mllb_tx_kick_cb;
static DEFINE_MUTEX(mllb_lock);

struct wwan_mllb_pcpu_stats {
	u64 rx_bytes;
	u64 rx_packets;
	u64 steer_rx_bytes;
	u64 steer_rx_packets;
	u64 bat_normal_bytes;
	u64 bat_frag_bytes;

	u64 tx_entry_bytes;
	u64 tx_entry_packets;
	u64 tx_vq_enq_bytes;
	u64 tx_vq_enq_packets;
	u64 tx_vq_deq_bytes;
	u64 tx_vq_deq_packets;
	u64 tx_drb_fill_bytes;
	u64 tx_drb_fill_packets;
	u64 tx_retire_bytes;
	u64 tx_retire_packets;
};

struct wwan_mllb_context {
	struct wwan_mllb_pcpu_stats __percpu *pcpu_stats;
	u64 history[MLLB_HISTORY_SIZE];
	u64 history_steer[MLLB_HISTORY_SIZE];
	u64 history_bat_normal[MLLB_HISTORY_SIZE];
	u64 history_bat_frag[MLLB_HISTORY_SIZE];
	u64 history_tx_entry[MLLB_HISTORY_SIZE];
	u64 history_tx_vq_enq[MLLB_HISTORY_SIZE];
	u64 history_tx_vq_deq[MLLB_HISTORY_SIZE];
	u64 history_tx_drb_fill[MLLB_HISTORY_SIZE];
	u64 history_tx_retire[MLLB_HISTORY_SIZE];

	u32 current_idx;
	u32 rx_queue_count;

	u64 last_total_bytes;
	u64 last_steer_bytes;
	u64 last_bat_normal_bytes;
	u64 last_bat_frag_bytes;
	u64 last_tx_entry_bytes;
	u64 last_tx_vq_enq_bytes;
	u64 last_tx_vq_deq_bytes;
	u64 last_tx_drb_fill_bytes;
	u64 last_tx_retire_bytes;

	u64 accum_ns;
	bool is_idle;
	ktime_t last_sample_time;
	atomic64_t token_bucket;
	unsigned long *throttled_mask;
	struct napi_struct **napi_map;
	struct wakeup_source **ws_map;
	struct mtk_md_dev *mdev;
	struct delayed_work sample_work;
	struct workqueue_struct *wq;
	struct dentry *dbg_dir;
};

static struct wwan_mllb_context mllb_ctx;

bool wwan_mllb_is_enabled(void)
{
	return READ_ONCE(mllb_enabled);
}
EXPORT_SYMBOL_GPL(wwan_mllb_is_enabled);

int wwan_mllb_get_mode(void)
{
	return READ_ONCE(mllb_mode);
}
EXPORT_SYMBOL_GPL(wwan_mllb_get_mode);

bool wwan_mllb_throttle_allow(u32 rxq_id)
{
	u32 target = READ_ONCE(mllb_target_mbps);

	if (!target) {
		if (mllb_ctx.throttled_mask && rxq_id < mllb_ctx.rx_queue_count)
			clear_bit(rxq_id, mllb_ctx.throttled_mask);
		return true;
	}

	if (!mllb_ctx.throttled_mask || rxq_id >= mllb_ctx.rx_queue_count)
		return true;

	if (atomic64_read(&mllb_ctx.token_bucket) > 0) {
		clear_bit(rxq_id, mllb_ctx.throttled_mask);
		return true;
	}

	set_bit(rxq_id, mllb_ctx.throttled_mask);

	return false;
}
EXPORT_SYMBOL_GPL(wwan_mllb_throttle_allow);

void wwan_mllb_throttle_update(u32 len)
{
	u64 bits = (u64)len * 8;

	if (READ_ONCE(mllb_target_mbps))
		atomic64_sub(bits, &mllb_ctx.token_bucket);
}

void wwan_mllb_collect_stats(struct sk_buff *skb, int mode)
{
	struct wwan_mllb_pcpu_stats *stats;

	if (unlikely(!skb))
		return;

	stats = this_cpu_ptr(mllb_ctx.pcpu_stats);
	stats->rx_bytes += skb->len;
	stats->rx_packets++;

	wwan_mllb_throttle_update(skb->len);

	/* Only free if in SINK_NAPI mode */
	if (mode == WWAN_MLLB_SINK_NAPI)
		dev_kfree_skb_any(skb);
}
EXPORT_SYMBOL_GPL(wwan_mllb_collect_stats);

void wwan_mllb_collect_steer_stats(struct sk_buff *skb, int mode)
{
	struct wwan_mllb_pcpu_stats *stats;

	if (unlikely(!skb))
		return;

	stats = this_cpu_ptr(mllb_ctx.pcpu_stats);
	stats->steer_rx_bytes += skb->len;
	stats->steer_rx_packets++;

	/* Only free if in SINK_STEER mode */
	if (mode == WWAN_MLLB_SINK_STEER)
		dev_kfree_skb_any(skb);
}
EXPORT_SYMBOL_GPL(wwan_mllb_collect_steer_stats);

void wwan_mllb_record_bat_reload(bool is_frag, u32 bytes)
{
	struct wwan_mllb_pcpu_stats *stats = this_cpu_ptr(mllb_ctx.pcpu_stats);

	if (is_frag)
		stats->bat_frag_bytes += bytes;
	else
		stats->bat_normal_bytes += bytes;
}
EXPORT_SYMBOL_GPL(wwan_mllb_record_bat_reload);

void wwan_mllb_collect_tx_entry_stats(struct sk_buff *skb, int mode)
{
	struct wwan_mllb_pcpu_stats *stats;

	if (unlikely(!skb))
		return;

	stats = this_cpu_ptr(mllb_ctx.pcpu_stats);
	stats->tx_entry_bytes += skb->len;
	stats->tx_entry_packets++;

	wwan_mllb_throttle_update(skb->len);

	if (mode == WWAN_MLLB_SINK_TX_ENTRY)
		dev_kfree_skb_any(skb);
}
EXPORT_SYMBOL_GPL(wwan_mllb_collect_tx_entry_stats);

bool wwan_mllb_tx_throttle_check(void)
{
	u32 target = READ_ONCE(mllb_target_mbps);

	if (!target)
		return true;

	return atomic64_read(&mllb_ctx.token_bucket) > 0;
}
EXPORT_SYMBOL_GPL(wwan_mllb_tx_throttle_check);

void wwan_mllb_collect_tx_vq_enq_stats(struct sk_buff *skb)
{
	struct wwan_mllb_pcpu_stats *stats;

	if (unlikely(!skb))
		return;

	stats = this_cpu_ptr(mllb_ctx.pcpu_stats);
	stats->tx_vq_enq_bytes += skb->len;
	stats->tx_vq_enq_packets++;
}
EXPORT_SYMBOL_GPL(wwan_mllb_collect_tx_vq_enq_stats);

void wwan_mllb_collect_tx_vq_deq_stats(struct sk_buff *skb, int mode)
{
	struct wwan_mllb_pcpu_stats *stats;

	if (unlikely(!skb))
		return;

	stats = this_cpu_ptr(mllb_ctx.pcpu_stats);
	stats->tx_vq_deq_bytes += skb->len;
	stats->tx_vq_deq_packets++;

	if (mode == WWAN_MLLB_SINK_TX_VQ)
		dev_kfree_skb_any(skb);
}
EXPORT_SYMBOL_GPL(wwan_mllb_collect_tx_vq_deq_stats);

void wwan_mllb_collect_tx_drb_fill_stats(struct sk_buff *skb)
{
	struct wwan_mllb_pcpu_stats *stats;

	if (unlikely(!skb))
		return;

	stats = this_cpu_ptr(mllb_ctx.pcpu_stats);
	stats->tx_drb_fill_bytes += skb->len;
	stats->tx_drb_fill_packets++;
}
EXPORT_SYMBOL_GPL(wwan_mllb_collect_tx_drb_fill_stats);

void wwan_mllb_collect_tx_retire_stats(struct sk_buff *skb)
{
	struct wwan_mllb_pcpu_stats *stats;

	if (unlikely(!skb))
		return;

	stats = this_cpu_ptr(mllb_ctx.pcpu_stats);
	stats->tx_retire_bytes += skb->len;
	stats->tx_retire_packets++;
}
EXPORT_SYMBOL_GPL(wwan_mllb_collect_tx_retire_stats);

void wwan_mllb_register_tx_kick_cb(wwan_mllb_tx_kick_fn cb)
{
	mllb_tx_kick_cb = cb;
	LOG_INFO("MLLB: TX kick callback registered\n");
}
EXPORT_SYMBOL_GPL(wwan_mllb_register_tx_kick_cb);

void wwan_mllb_init_queues(struct mtk_md_dev *mdev, u32 rxq_cnt)
{
	struct napi_struct **napi_map;
	struct wakeup_source **ws_map;
	unsigned long *throttled_mask;

	if (!rxq_cnt)
		return;

	napi_map =
		devm_kcalloc(mdev->dev, rxq_cnt, sizeof(struct napi_struct *), GFP_KERNEL);
	ws_map =
		devm_kcalloc(mdev->dev, rxq_cnt, sizeof(struct wakeup_source *), GFP_KERNEL);
	throttled_mask =
		devm_kcalloc(mdev->dev, BITS_TO_LONGS(rxq_cnt), sizeof(unsigned long), GFP_KERNEL);

	if (!napi_map || !ws_map || !throttled_mask)
		return;

	mllb_ctx.rx_queue_count = rxq_cnt;
	mllb_ctx.napi_map = napi_map;
	mllb_ctx.ws_map = ws_map;
	mllb_ctx.throttled_mask = throttled_mask;
	mllb_ctx.mdev = mdev;

	LOG_INFO("MLLB queues init done\n");
}
EXPORT_SYMBOL_GPL(wwan_mllb_init_queues);

void wwan_mllb_exit_queues(void)
{
	mllb_ctx.mdev = NULL;
	mllb_ctx.napi_map = NULL;
	mllb_ctx.ws_map = NULL;
	mllb_ctx.throttled_mask = NULL;
	mllb_ctx.rx_queue_count = 0;
	LOG_INFO("MLLB queues exit done\n");
}
EXPORT_SYMBOL_GPL(wwan_mllb_exit_queues);

void wwan_mllb_register_napi(u32 rxq_id, struct napi_struct *napi)
{
	if (!mllb_ctx.napi_map || rxq_id >= mllb_ctx.rx_queue_count) {
		if (napi)
			LOG_ERR("Register napi failed: map=%p, rxq_id=%u\n",
				mllb_ctx.napi_map, rxq_id);
		return;
	}

	mllb_ctx.napi_map[rxq_id] = napi;
	LOG_INFO("Register napi for RXQ%u\n", rxq_id);
}
EXPORT_SYMBOL_GPL(wwan_mllb_register_napi);

void wwan_mllb_register_ws(u32 rxq_id, struct wakeup_source *ws)
{
	if (!mllb_ctx.ws_map || rxq_id >= mllb_ctx.rx_queue_count) {
		if (ws)
			LOG_ERR("Register ws failed: map=%p, rxq_id=%u\n",
				mllb_ctx.ws_map, rxq_id);
		return;
	}

	mllb_ctx.ws_map[rxq_id] = ws;
	LOG_INFO("Register ws for RXQ%u\n", rxq_id);
}
EXPORT_SYMBOL_GPL(wwan_mllb_register_ws);

static void wwan_mllb_kick_napi(u32 rxq_id)
{
	if (!mllb_ctx.napi_map[rxq_id] || !mllb_ctx.ws_map[rxq_id])
		return;

	if (!napi_schedule_prep(mllb_ctx.napi_map[rxq_id]))
		return;

	clear_bit(rxq_id, mllb_ctx.throttled_mask);

	__pm_stay_awake(mllb_ctx.ws_map[rxq_id]);
	__napi_schedule(mllb_ctx.napi_map[rxq_id]);
}

static void wwan_mllb_kick_napis(void)
{
	int i;

	if (!mllb_ctx.mdev)
		return;

	if (!mllb_ctx.napi_map || !mllb_ctx.ws_map || !mllb_ctx.throttled_mask)
		return;

	for (i = 0; i < mllb_ctx.rx_queue_count; i++) {
		if (test_bit(i, mllb_ctx.throttled_mask))
			wwan_mllb_kick_napi(i);
	}
}

static void wwan_mllb_kick_tx_queues(void)
{
	if (!mllb_ctx.mdev)
		return;

	if (mllb_tx_kick_cb)
		mllb_tx_kick_cb(mllb_ctx.mdev);
}

static void wwan_mllb_sample_work_func(struct work_struct *work)
{
	struct wwan_mllb_context *ctx =
		container_of(work, struct wwan_mllb_context, sample_work.work);
	ktime_t now = ktime_get();
	u64 delta_ns = ktime_to_ns(ktime_sub(now, ctx->last_sample_time));
	u64 total_bytes = 0, steer_total_bytes = 0;
	u64 bat_norm_tot = 0, bat_frag_tot = 0;
	u64 tx_entry_tot = 0, tx_vq_enq_tot = 0, tx_vq_deq_tot = 0;
	u64 tx_drb_fill_tot = 0, tx_retire_tot = 0;
	u64 delta_bytes, delta_steer_bytes, delta_bat_norm, delta_bat_frag;
	u64 delta_tx_entry, delta_tx_vq_enq, delta_tx_vq_deq;
	u64 delta_tx_drb_fill, delta_tx_retire;
	u64 mbps, steer_mbps, bat_norm_mbps, bat_frag_mbps, tx_entry_mbps, tx_vq_enq_mbps;
	u64 tx_vq_deq_mbps, tx_drb_fill_mbps, tx_retire_mbps;
	int cpu;
	struct wwan_mllb_pcpu_stats *s;
	u64 bits_to_add, max_bucket_bits;
	s64 current_bucket, space_left;
	u32 target = READ_ONCE(mllb_target_mbps);

	if (!READ_ONCE(mllb_enabled) || delta_ns == 0)
		return;

	ctx->accum_ns += delta_ns;
	ctx->last_sample_time = now;

	if (ctx->accum_ns >= SEC_TO_NS(1)) {
		for_each_possible_cpu(cpu) {
			s = per_cpu_ptr(ctx->pcpu_stats, cpu);
			total_bytes += s->rx_bytes;
			steer_total_bytes += s->steer_rx_bytes;
			bat_norm_tot += s->bat_normal_bytes;
			bat_frag_tot += s->bat_frag_bytes;

			tx_entry_tot += s->tx_entry_bytes;
			tx_vq_enq_tot += s->tx_vq_enq_bytes;
			tx_vq_deq_tot += s->tx_vq_deq_bytes;
			tx_drb_fill_tot += s->tx_drb_fill_bytes;
			tx_retire_tot += s->tx_retire_bytes;
		}

		delta_bytes = total_bytes - ctx->last_total_bytes;
		ctx->last_total_bytes = total_bytes;

		delta_steer_bytes = steer_total_bytes - ctx->last_steer_bytes;
		ctx->last_steer_bytes = steer_total_bytes;

		delta_bat_norm = bat_norm_tot - ctx->last_bat_normal_bytes;
		ctx->last_bat_normal_bytes = bat_norm_tot;

		delta_bat_frag = bat_frag_tot - ctx->last_bat_frag_bytes;
		ctx->last_bat_frag_bytes = bat_frag_tot;

		delta_tx_entry = tx_entry_tot - ctx->last_tx_entry_bytes;
		ctx->last_tx_entry_bytes = tx_entry_tot;

		delta_tx_vq_enq = tx_vq_enq_tot - ctx->last_tx_vq_enq_bytes;
		ctx->last_tx_vq_enq_bytes = tx_vq_enq_tot;

		delta_tx_vq_deq = tx_vq_deq_tot - ctx->last_tx_vq_deq_bytes;
		ctx->last_tx_vq_deq_bytes = tx_vq_deq_tot;

		delta_tx_drb_fill = tx_drb_fill_tot - ctx->last_tx_drb_fill_bytes;
		ctx->last_tx_drb_fill_bytes = tx_drb_fill_tot;

		delta_tx_retire = tx_retire_tot - ctx->last_tx_retire_bytes;
		ctx->last_tx_retire_bytes = tx_retire_tot;

		ctx->is_idle = (delta_bytes == 0 && delta_tx_entry == 0);

		/* Mbps = (delta_bytes * 8 * 1000) / delta_ns */
		mbps = div64_u64(delta_bytes * 8000, ctx->accum_ns);
		steer_mbps = div64_u64(delta_steer_bytes * 8000, ctx->accum_ns);
		bat_norm_mbps = div64_u64(delta_bat_norm * 8000, ctx->accum_ns);
		bat_frag_mbps = div64_u64(delta_bat_frag * 8000, ctx->accum_ns);
		tx_entry_mbps = div64_u64(delta_tx_entry * 8000, ctx->accum_ns);
		tx_vq_enq_mbps = div64_u64(delta_tx_vq_enq * 8000, ctx->accum_ns);
		tx_vq_deq_mbps = div64_u64(delta_tx_vq_deq * 8000, ctx->accum_ns);
		tx_drb_fill_mbps = div64_u64(delta_tx_drb_fill * 8000, ctx->accum_ns);
		tx_retire_mbps = div64_u64(delta_tx_retire * 8000, ctx->accum_ns);

		if (ctx->current_idx < MLLB_HISTORY_SIZE) {
			WRITE_ONCE(ctx->history[ctx->current_idx], mbps);
			WRITE_ONCE(ctx->history_steer[ctx->current_idx], steer_mbps);
			WRITE_ONCE(ctx->history_bat_normal[ctx->current_idx], bat_norm_mbps);
			WRITE_ONCE(ctx->history_bat_frag[ctx->current_idx], bat_frag_mbps);
			WRITE_ONCE(ctx->history_tx_entry[ctx->current_idx], tx_entry_mbps);
			WRITE_ONCE(ctx->history_tx_vq_enq[ctx->current_idx], tx_vq_enq_mbps);
			WRITE_ONCE(ctx->history_tx_vq_deq[ctx->current_idx], tx_vq_deq_mbps);
			WRITE_ONCE(ctx->history_tx_drb_fill[ctx->current_idx], tx_drb_fill_mbps);
			WRITE_ONCE(ctx->history_tx_retire[ctx->current_idx], tx_retire_mbps);
			/* Ensure history array entries are visible before updating current_idx */
			smp_wmb();
			WRITE_ONCE(ctx->current_idx, ctx->current_idx + 1);
		} else {
			WRITE_ONCE(mllb_enabled, false);
			LOG_INFO("WWAN MLLB: Benchmarking session finished\n");
		}

		ctx->accum_ns = 0;
	}

	if (target) {
		/* target_mbps * 1,000,000 * (delta_ns / 1,000,000,000) */
		bits_to_add = div64_u64((u64)target * delta_ns, 1000);

		max_bucket_bits = MBITS_TO_BITS((u64)target);
		current_bucket = atomic64_read(&ctx->token_bucket);

		/* To avoid burst start */
		if (ctx->is_idle) {
			max_bucket_bits = bits_to_add;

			if (atomic64_read(&ctx->token_bucket) > (s64)max_bucket_bits)
				atomic64_set(&ctx->token_bucket, max_bucket_bits);
		}

		/* To avoid expensive atomic64_add(0, &ctx->token_bucket) */
		if (current_bucket < (s64)max_bucket_bits) {
			space_left = (s64)max_bucket_bits - current_bucket;

			if (bits_to_add > space_left)
				bits_to_add = space_left;

			atomic64_add(bits_to_add, &ctx->token_bucket);
		}

		/* Kick all the throttled napi and tx queues */
		if (atomic64_read(&ctx->token_bucket) > 0) {
			wwan_mllb_kick_napis();
			wwan_mllb_kick_tx_queues();
		}
	}

	if (READ_ONCE(mllb_enabled))
		queue_delayed_work(ctx->wq, &ctx->sample_work,
				   msecs_to_jiffies(SAMPLE_WORK_GRANULE_MS));
}

static int wwan_mllb_report_show(struct seq_file *m, void *v)
{
	int i;
	u32 count;
	const char *mode_str = "MONITOR";

	if (mllb_mode == WWAN_MLLB_SINK_NAPI)
		mode_str = "SINK_NAPI";
	else if (mllb_mode == WWAN_MLLB_SINK_STEER)
		mode_str = "SINK_STEER";
	else if (mllb_mode == WWAN_MLLB_SINK_TX_ENTRY)
		mode_str = "SINK_TX_ENTRY";
	else if (mllb_mode == WWAN_MLLB_SINK_TX_VQ)
		mode_str = "SINK_TX_VQ";

	seq_puts(m, "=== WWAN MLLB Report ===\n");
	seq_printf(m, "Status: %s (Mode: %s)\n", mllb_enabled ? "RUNNING" : "STOPPED", mode_str);

	if (mllb_target_mbps) {
		seq_printf(m, "Throttle: %u Mbps | State: %s\n",
			   mllb_target_mbps,
			   mllb_ctx.is_idle ? "IDLE (Cool Down)" : "ACTIVE");
	} else {
		seq_puts(m, "Throttle: DISABLED\n");
	}

	if (mllb_ctx.current_idx == 0) {
		seq_puts(m, "\n(No data recorded)\n");
		return 0;
	}

	count = min_t(u32, READ_ONCE(mllb_ctx.current_idx), MLLB_HISTORY_SIZE);
	/* Ensure current_idx is read before accessing history array entries */
	smp_rmb();

	if (mllb_mode == WWAN_MLLB_MONITOR || mllb_mode == WWAN_MLLB_SINK_NAPI ||
	    mllb_mode == WWAN_MLLB_SINK_STEER) {
		seq_puts(m, "\n--- Downlink (RX) Throughput History (Mbps) ---\n");
		for (i = 0; i < count; i++) {
			seq_printf(m, "[%3ds]: NAPI: %llu | Steer: %llu | BAT_Norm: %llu | BAT_Frag: %llu Mbps\n",
				   i + 1,
				   READ_ONCE(mllb_ctx.history[i]),
				   READ_ONCE(mllb_ctx.history_steer[i]),
				   READ_ONCE(mllb_ctx.history_bat_normal[i]),
				   READ_ONCE(mllb_ctx.history_bat_frag[i]));
		}
	}

	if (mllb_mode == WWAN_MLLB_MONITOR || mllb_mode >= WWAN_MLLB_SINK_TX_ENTRY) {
		seq_puts(m, "\n--- Uplink (TX) Throughput History (Mbps) ---\n");
		for (i = 0; i < count; i++) {
			seq_printf(m,
				   "[%3ds]: Entry: %llu | VQ_Enq: %llu | VQ_Deq: %llu | DRB_Fill: %llu | Retire: %llu Mbps\n",
				   i + 1,
				   READ_ONCE(mllb_ctx.history_tx_entry[i]),
				   READ_ONCE(mllb_ctx.history_tx_vq_enq[i]),
				   READ_ONCE(mllb_ctx.history_tx_vq_deq[i]),
				   READ_ONCE(mllb_ctx.history_tx_drb_fill[i]),
				   READ_ONCE(mllb_ctx.history_tx_retire[i]));
		}
	}

	return 0;
}

static int wwan_mllb_report_open(struct inode *inode, struct file *file)
{
	return single_open(file, wwan_mllb_report_show, NULL);
}

static const struct file_operations wwan_mllb_report_fops = {
	.owner = THIS_MODULE,
	.open = wwan_mllb_report_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int wwan_mllb_mode_set(void *data, u64 val)
{
	if (val >= WWAN_MLLB_MODE_MAX) {
		LOG_ERR("Invalid mode %llu\n", val);
		return -EINVAL;
	}

	mutex_lock(&mllb_lock);
	mllb_mode = (u32)val;
	mutex_unlock(&mllb_lock);
	LOG_INFO("WWAN MLLB: Mode set to %u\n", mllb_mode);
	return 0;
}

static int wwan_mllb_mode_get(void *data, u64 *val)
{
	*val = READ_ONCE(mllb_mode);
	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(wwan_mllb_mode_fops, wwan_mllb_mode_get, wwan_mllb_mode_set, "%llu\n");

static int wwan_mllb_enable_set(void *data, u64 val)
{
	bool enable = !!val;
	u64 current_all_cpu_bytes = 0;
	int cpu;
	struct wwan_mllb_pcpu_stats *s;

	mutex_lock(&mllb_lock);
	if (enable && !READ_ONCE(mllb_enabled)) {
		u64 current_steer_bytes = 0;
		u64 cur_bat_norm = 0, cur_bat_frag = 0;
		u64 cur_tx_entry = 0, cur_tx_vq_enq = 0, cur_tx_vq_deq = 0;
		u64 cur_tx_drb_fill = 0, cur_tx_retire = 0;

		cancel_delayed_work_sync(&mllb_ctx.sample_work);

		for_each_possible_cpu(cpu) {
			s = per_cpu_ptr(mllb_ctx.pcpu_stats, cpu);
			current_all_cpu_bytes += s->rx_bytes;
			current_steer_bytes += s->steer_rx_bytes;
			cur_bat_norm += s->bat_normal_bytes;
			cur_bat_frag += s->bat_frag_bytes;

			cur_tx_entry += s->tx_entry_bytes;
			cur_tx_vq_enq += s->tx_vq_enq_bytes;
			cur_tx_vq_deq += s->tx_vq_deq_bytes;
			cur_tx_drb_fill += s->tx_drb_fill_bytes;
			cur_tx_retire += s->tx_retire_bytes;
		}

		memset(mllb_ctx.history, 0, sizeof(mllb_ctx.history));
		memset(mllb_ctx.history_steer, 0, sizeof(mllb_ctx.history_steer));
		memset(mllb_ctx.history_bat_normal, 0, sizeof(mllb_ctx.history_bat_normal));
		memset(mllb_ctx.history_bat_frag, 0, sizeof(mllb_ctx.history_bat_frag));
		memset(mllb_ctx.history_tx_entry, 0, sizeof(mllb_ctx.history_tx_entry));
		memset(mllb_ctx.history_tx_vq_enq, 0, sizeof(mllb_ctx.history_tx_vq_enq));
		memset(mllb_ctx.history_tx_vq_deq, 0, sizeof(mllb_ctx.history_tx_vq_deq));
		memset(mllb_ctx.history_tx_drb_fill, 0, sizeof(mllb_ctx.history_tx_drb_fill));
		memset(mllb_ctx.history_tx_retire, 0, sizeof(mllb_ctx.history_tx_retire));

		mllb_ctx.current_idx = 0;
		mllb_ctx.last_total_bytes = current_all_cpu_bytes;
		mllb_ctx.last_steer_bytes = current_steer_bytes;
		mllb_ctx.last_bat_normal_bytes = cur_bat_norm;
		mllb_ctx.last_bat_frag_bytes = cur_bat_frag;
		mllb_ctx.last_tx_entry_bytes = cur_tx_entry;
		mllb_ctx.last_tx_vq_enq_bytes = cur_tx_vq_enq;
		mllb_ctx.last_tx_vq_deq_bytes = cur_tx_vq_deq;
		mllb_ctx.last_tx_drb_fill_bytes = cur_tx_drb_fill;
		mllb_ctx.last_tx_retire_bytes = cur_tx_retire;
		mllb_ctx.last_sample_time = ktime_get();
		atomic64_set(&mllb_ctx.token_bucket, 0);
		mllb_ctx.accum_ns = 0;
		mllb_ctx.is_idle = true;

		/* Make the mllb_enable visible to all cores */
		smp_store_release(&mllb_enabled, true);

		queue_delayed_work(mllb_ctx.wq, &mllb_ctx.sample_work, HZ);
		LOG_INFO("WWAN MLLB: Enabled (Mode: %u)\n", mllb_mode);
	} else if (!enable && READ_ONCE(mllb_enabled)) {
		WRITE_ONCE(mllb_enabled, false);
		cancel_delayed_work_sync(&mllb_ctx.sample_work);
		LOG_INFO("WWAN MLLB: Disabled\n");
	}
	mutex_unlock(&mllb_lock);

	return 0;
}

static int wwan_mllb_enable_get(void *data, u64 *val)
{
	*val = READ_ONCE(mllb_enabled);
	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(wwan_mllb_enable_fops, wwan_mllb_enable_get, wwan_mllb_enable_set,
			 "%llu\n");

int wwan_mllb_init(void)
{
	struct dentry *parent_dir = get_radio_debugfs_root();

	if (IS_ERR_OR_NULL(parent_dir))
		return -ENODEV;

	mllb_ctx.pcpu_stats = alloc_percpu(struct wwan_mllb_pcpu_stats);
	if (!mllb_ctx.pcpu_stats)
		return -ENOMEM;

	mllb_ctx.wq = alloc_workqueue("wwan_mllb_wq", WQ_HIGHPRI | WQ_UNBOUND, 1);
	if (!mllb_ctx.wq)
		goto err_free_percpu;

	mllb_ctx.dbg_dir = debugfs_create_dir("mllb", parent_dir);
	if (IS_ERR_OR_NULL(mllb_ctx.dbg_dir))
		goto err_destroy_wq;

	debugfs_create_file("enable", 0644, mllb_ctx.dbg_dir, NULL, &wwan_mllb_enable_fops);
	debugfs_create_file("mode", 0644, mllb_ctx.dbg_dir, NULL, &wwan_mllb_mode_fops);
	debugfs_create_file("report", 0444, mllb_ctx.dbg_dir, NULL, &wwan_mllb_report_fops);
	debugfs_create_u32("target_mbps", 0644, mllb_ctx.dbg_dir, &mllb_target_mbps);

	INIT_DELAYED_WORK(&mllb_ctx.sample_work, wwan_mllb_sample_work_func);

	LOG_INFO("WWAN MLLB init done\n");
	return 0;

err_destroy_wq:
	destroy_workqueue(mllb_ctx.wq);
	mllb_ctx.wq = NULL;
err_free_percpu:
	free_percpu(mllb_ctx.pcpu_stats);
	mllb_ctx.pcpu_stats = NULL;
	return -ENOMEM;
}

void wwan_mllb_exit(void)
{
	debugfs_remove_recursive(mllb_ctx.dbg_dir);
	mllb_ctx.dbg_dir = NULL;
	WRITE_ONCE(mllb_enabled, false);
	cancel_delayed_work_sync(&mllb_ctx.sample_work);
	if (mllb_ctx.wq) {
		destroy_workqueue(mllb_ctx.wq);
		mllb_ctx.wq = NULL;
	}
	if (mllb_ctx.pcpu_stats) {
		free_percpu(mllb_ctx.pcpu_stats);
		mllb_ctx.pcpu_stats = NULL;
	}
}
#endif
