// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * KUnit test suite for Google TTF (Time-to-Full).
 */

#include <kunit/test.h>
#include <linux/types.h>
#include <linux/stdarg.h>
#include <misc/logbuffer.h>
#include "../gbms_storage.h"

/* Forward stubs for external dependencies required by google_ttf.c */
struct gbms_chg_profile;

void logbuffer_vlog(struct logbuffer *instance, const char *fmt, va_list args)
{
}

int gbms_storage_read(gbms_tag_t tag, void *data, size_t count)
{
	return 0;
}

int gbms_storage_write(gbms_tag_t tag, const void *data, size_t count)
{
	return 0;
}

int gbms_msc_temp_idx(const struct gbms_chg_profile *profile, int temp_deciC)
{
	return 0;
}

#include "../google_ttf.c"

struct ttf_test_ctx {
	u32 cccm[1];
	struct batt_ttf_stats *stats;
	struct gbms_charging_event *ce_data;
	struct gbms_chg_profile profile;
};

static int google_ttf_test_init(struct kunit *test)
{
	struct ttf_test_ctx *ctx;

	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx);

	ctx->stats = kunit_kzalloc(test, sizeof(*ctx->stats), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->stats);
	ctx->ce_data = kunit_kzalloc(test, sizeof(*ctx->ce_data), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->ce_data);

	mutex_init(&ctx->stats->ttf_lock);

	ctx->cccm[0] = 3000000; /* uA */
	ctx->profile.volt_limits[0] = 5000000; /* uV */
	ctx->profile.volt_nb_limits = 1;
	ctx->profile.temp_nb_limits = 1;
	ctx->profile.cccm_limits = ctx->cccm;
	ctx->ce_data->chg_profile = &ctx->profile;

	/* avg_cc at SOC 50 is = (40 - 20) * 3600 / 36 = 2000 mA */
	/* ref_elap = 36 */
	ctx->stats->soc_ref.elap[50] = 36; /* seconds */
	ctx->stats->soc_ref.cc[50] = 20; /* coulombs */
	ctx->stats->soc_ref.cc[51] = 40; /* coulombs */

	ctx->ce_data->charging_stats.ssoc_in = 80;

	test->priv = ctx;
	return 0;
}

/*
 * TEST
 */

/* Set high adapter capabilities so that estimate is bounded by cc_max */
static void bounded_by_cccm_limits(struct kunit *test)
{
	struct ttf_test_ctx *ctx = test->priv;
	qnum_t soc_start = qnum_fromint(50);
	qnum_t soc_end = qnum_fromint(51);
	ktime_t estimate;
	int ret;

	ctx->ce_data->adapter_details.ad_voltage = 200; /* dV */
	ctx->ce_data->adapter_details.ad_amperage = 100; /* dA */

	/* cc_max = 1000 mA */
	ctx->cccm[0] = 1000000; /* uA */

	ret = ttf_soc_estimate(&estimate, ctx->stats, ctx->ce_data, soc_start, soc_end, 0);

	/* avg_cc / cc_max = 2.00 */
	KUNIT_EXPECT_EQ(test, ret, 200);
	/* ref_elap * ratio = 72 */
	KUNIT_EXPECT_EQ(test, estimate, 72);
}

static void estimate_uses_adapter_capabilites(struct kunit *test)
{
	struct ttf_test_ctx *ctx = test->priv;
	qnum_t soc_start = qnum_fromint(50);
	qnum_t soc_end = qnum_fromint(51);
	ktime_t estimate;
	int ret;

	/* equiv_icl = ad_volt * ad_amp * eff / volt_tier = 950 mA */
	ctx->ce_data->adapter_details.ad_voltage = 50; /* dV */
	ctx->ce_data->adapter_details.ad_amperage = 10; /* dA */

	ret = ttf_soc_estimate(&estimate, ctx->stats, ctx->ce_data, soc_start, soc_end, 0);

	/* avg_cc / equiv_icl = 2.10 */
	KUNIT_EXPECT_EQ(test, ret, 210);
	/* ref_elap * ratio = 75 */
	KUNIT_EXPECT_EQ(test, estimate, 75);
}

static void error_when_zero_act_icl(struct kunit *test)
{
	struct ttf_test_ctx *ctx = test->priv;
	qnum_t soc_start = qnum_fromint(50);
	qnum_t soc_end = qnum_fromint(51);
	ktime_t estimate;
	int ret;

	ctx->ce_data->adapter_details.ad_voltage = 50; /* dV */
	ctx->ce_data->adapter_details.ad_amperage = 0; /* dA */

	ret = ttf_soc_estimate(&estimate, ctx->stats, ctx->ce_data, soc_start, soc_end, 0);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
}

static void uses_health_ibatt_when_health_active(struct kunit *test)
{
	struct ttf_test_ctx *ctx = test->priv;
	qnum_t soc_start = qnum_fromint(50);
	qnum_t soc_end = qnum_fromint(51);
	ktime_t estimate;
	int ret;

	ctx->ce_data->adapter_details.ad_voltage = 50; /* dV */
	ctx->ce_data->adapter_details.ad_amperage = 10; /* dA */

	/* Health active; health_ibatt = 500 mA */
	ctx->ce_data->ce_health.rest_state = CHG_HEALTH_ACTIVE;
	ctx->ce_data->ce_health.always_on_soc = 20;
	ctx->ce_data->ce_health.rest_cc_max = 500000; /* uA */

	ret = ttf_soc_estimate(&estimate, ctx->stats, ctx->ce_data, soc_start, soc_end, 0);

	/* avg_cc / health_ibatt = 4.00 */
	KUNIT_EXPECT_EQ(test, ret, 400);
	/* ref_elap * ratio = 144 */
	KUNIT_EXPECT_EQ(test, estimate, 144);
}

static void uses_health_ibatt_when_health_paused(struct kunit *test)
{
	struct ttf_test_ctx *ctx = test->priv;
	qnum_t soc_start = qnum_fromint(50);
	qnum_t soc_end = qnum_fromint(51);
	ktime_t estimate;
	int ret;

	ctx->ce_data->adapter_details.ad_voltage = 50; /* dV */
	ctx->ce_data->adapter_details.ad_amperage = 10; /* dA */

	/* health_ibatt - capacity_ma * rest_rate = 400 mA */
	ctx->profile.capacity_ma = 4000; /* mAh */
	ctx->ce_data->ce_health.rest_state = CHG_HEALTH_PAUSE;
	ctx->ce_data->ce_health.always_on_soc = 20;
	ctx->ce_data->ce_health.rest_rate = 10; /* centirate */

	ret = ttf_soc_estimate(&estimate, ctx->stats, ctx->ce_data, soc_start, soc_end, 0);

	/* avg_cc / health_ibatt = 5.00 */
	KUNIT_EXPECT_EQ(test, ret, 500);
	/* ref_elap / ratio = 180 */
	KUNIT_EXPECT_EQ(test, estimate, 180);
}

static void estimate_based_on_ibatt_measurement(struct kunit *test)
{
	struct ttf_test_ctx *ctx = test->priv;
	qnum_t soc_start = qnum_fromint(50);
	qnum_t soc_end = qnum_fromint(51);
	ktime_t estimate;
	int ret;

	ctx->ce_data->adapter_details.ad_voltage = 50; /* dV */
	ctx->ce_data->adapter_details.ad_amperage = 10; /* dA */

	/* ibatt = 250mA */
	ctx->ce_data->tier_stats[0].time_fast = 100; /* seconds */
	ctx->ce_data->tier_stats[0].icl_sum = 300000; /* mA * s */
	ctx->ce_data->tier_stats[0].ibatt_sum = 25000; /* mA * s */

	ret = ttf_soc_estimate(&estimate, ctx->stats, ctx->ce_data, soc_start, soc_end, 0);

	/* avg_cc / ibatt = 8.00 */
	KUNIT_EXPECT_EQ(test, ret, 800);
	/* ref_elap * ratio = 288 */
	KUNIT_EXPECT_EQ(test, estimate, 288);
}

static void fcc_and_zero_ibatt_uses_fcc(struct kunit *test)
{
	struct ttf_test_ctx *ctx = test->priv;
	qnum_t soc_start = qnum_fromint(50);
	qnum_t soc_end = qnum_fromint(51);
	ktime_t estimate;
	int ret;

	ctx->ce_data->adapter_details.ad_voltage = 50; /* dV */
	ctx->ce_data->adapter_details.ad_amperage = 10; /* dA */

	/* fcc_now = 800 mA */
	ctx->stats->fcc_now = 800000; /* uA */

	ret = ttf_soc_estimate(&estimate, ctx->stats, ctx->ce_data, soc_start, soc_end, 0);

	/* avg_cc / fcc_now = 2.50 */
	KUNIT_EXPECT_EQ(test, ret, 250);
	/* ref_elap * ratio = 90 */
	KUNIT_EXPECT_EQ(test, estimate, 90);
}

static void fcc_less_than_ibatt_uses_fcc(struct kunit *test)
{
	struct ttf_test_ctx *ctx = test->priv;
	qnum_t soc_start = qnum_fromint(50);
	qnum_t soc_end = qnum_fromint(51);
	ktime_t estimate;
	int ret;

	ctx->ce_data->adapter_details.ad_voltage = 50; /* dV */
	ctx->ce_data->adapter_details.ad_amperage = 10; /* dA */

	/* fcc_now = 1250 mA */
	ctx->stats->fcc_now = 1250000; /* uA */

	/* ibatt = 2500 mA */
	ctx->ce_data->tier_stats[0].time_fast = 100; /* seconds */
	ctx->ce_data->tier_stats[0].icl_sum = 400000; /* mA * s */
	ctx->ce_data->tier_stats[0].ibatt_sum = 250000; /* mA * s */

	ret = ttf_soc_estimate(&estimate, ctx->stats, ctx->ce_data, soc_start, soc_end, 0);

	/* avg_cc / fcc_now = 1.60 */
	KUNIT_EXPECT_EQ(test, ret, 160);
	/* ref_elap * ratio = 57.6 */
	KUNIT_EXPECT_EQ(test, estimate, 57);
}

static void fcc_greater_than_ibatt_uses_ibatt(struct kunit *test)
{
	struct ttf_test_ctx *ctx = test->priv;
	qnum_t soc_start = qnum_fromint(50);
	qnum_t soc_end = qnum_fromint(51);
	ktime_t estimate;
	int ret;

	ctx->ce_data->adapter_details.ad_voltage = 50; /* dV */
	ctx->ce_data->adapter_details.ad_amperage = 10; /* dA */

	/* fcc_now = 1250 mA */
	ctx->stats->fcc_now = 1250000; /* uA */

	/* ibatt = 1000 mA */
	ctx->ce_data->tier_stats[0].time_fast = 100; /* seconds */
	ctx->ce_data->tier_stats[0].icl_sum = 400000; /* mA * s */
	ctx->ce_data->tier_stats[0].ibatt_sum = 100000; /* mA * s */

	ret = ttf_soc_estimate(&estimate, ctx->stats, ctx->ce_data, soc_start, soc_end, 0);

	/* avg_cc / ibatt = 2.00 */
	KUNIT_EXPECT_EQ(test, ret, 200);
	/* ref_elap * ratio = 72 */
	KUNIT_EXPECT_EQ(test, estimate, 72);
}

static void fcc_ignored_when_health_active(struct kunit *test)
{
	struct ttf_test_ctx *ctx = test->priv;
	qnum_t soc_start = qnum_fromint(50);
	qnum_t soc_end = qnum_fromint(51);
	ktime_t estimate;
	int ret;

	ctx->ce_data->adapter_details.ad_voltage = 50; /* dV */
	ctx->ce_data->adapter_details.ad_amperage = 10; /* dA */

	/* fcc_now = 200 mA */
	ctx->stats->fcc_now = 200000; /* uA */

	/* Health active; health_ibatt =  500 mA */
	ctx->ce_data->ce_health.rest_state = CHG_HEALTH_ACTIVE;
	ctx->ce_data->ce_health.always_on_soc = 20;
	ctx->ce_data->ce_health.rest_cc_max = 500000; /* uA */

	ret = ttf_soc_estimate(&estimate, ctx->stats, ctx->ce_data, soc_start, soc_end, 0);

	/* avg_cc / health_ibatt = 4.00 */
	KUNIT_EXPECT_EQ(test, ret, 400);
	/* ref_elap * ratio = 144 */
	KUNIT_EXPECT_EQ(test, estimate, 144);
}

static void error_when_negative_ibatt(struct kunit *test)
{
	struct ttf_test_ctx *ctx = test->priv;
	qnum_t soc_start = qnum_fromint(50);
	qnum_t soc_end = qnum_fromint(51);
	ktime_t estimate;
	int ret;

	ctx->ce_data->adapter_details.ad_voltage = 50; /* dV */
	ctx->ce_data->adapter_details.ad_amperage = 10; /* dA */

	/* ibatt = -1000 mA */
	ctx->ce_data->tier_stats[0].time_fast = 100; /* seconds */
	ctx->ce_data->tier_stats[0].icl_sum = 300000; /* mA * s */
	ctx->ce_data->tier_stats[0].ibatt_sum = (s64)-1000 * 100; /* mA * s */

	ret = ttf_soc_estimate(&estimate, ctx->stats, ctx->ce_data, soc_start, soc_end, 0);

	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
}

static void wrong_input_soc(struct kunit *test)
{
	struct ttf_test_ctx *ctx = test->priv;
	qnum_t soc_start = qnum_fromint(80);
	qnum_t soc_end;
	ktime_t res = 0;
	int ret;

	/* Case 1: last < soc */
	soc_end = qnum_fromint(70);
	ret = ttf_soc_estimate(&res, ctx->stats, ctx->ce_data, soc_start, soc_end, 0);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);

	/* Case 2: last > 100 */
	soc_end = qnum_fromint(105);
	ret = ttf_soc_estimate(&res, ctx->stats, ctx->ce_data, soc_start, soc_end, 0);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
}

static void input_same_soc(struct kunit *test)
{
	struct ttf_test_ctx *ctx = test->priv;
	qnum_t soc_start = qnum_fromint(80);
	qnum_t soc_end = qnum_fromint(80);
	ktime_t res = 1000; /* ms */
	int ret;

	ret = ttf_soc_estimate(&res, ctx->stats, ctx->ce_data, soc_start, soc_end, 0);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, res, 0);
}

static struct kunit_case google_ttf_test_cases[] = {
	KUNIT_CASE(bounded_by_cccm_limits),
	KUNIT_CASE(estimate_uses_adapter_capabilites),
	KUNIT_CASE(error_when_zero_act_icl),
	KUNIT_CASE(uses_health_ibatt_when_health_active),
	KUNIT_CASE(uses_health_ibatt_when_health_paused),
	KUNIT_CASE(estimate_based_on_ibatt_measurement),
	KUNIT_CASE(fcc_and_zero_ibatt_uses_fcc),
	KUNIT_CASE(fcc_less_than_ibatt_uses_fcc),
	KUNIT_CASE(fcc_greater_than_ibatt_uses_ibatt),
	KUNIT_CASE(fcc_ignored_when_health_active),
	KUNIT_CASE(error_when_negative_ibatt),
	KUNIT_CASE(wrong_input_soc),
	KUNIT_CASE(input_same_soc),
	{}
};

static struct kunit_suite google_ttf_test_suite = {
	.name = "google_ttf_test",
	.init = google_ttf_test_init,
	.test_cases = google_ttf_test_cases,
};

kunit_test_suite(google_ttf_test_suite);

MODULE_DESCRIPTION("KUnit test suite for Google TTF");
MODULE_AUTHOR("Adam Solawa <solawaa@google.com>");
MODULE_LICENSE("GPL");
