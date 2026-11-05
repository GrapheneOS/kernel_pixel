// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * KUnit unit test suite for google_battery.c (including real gbatt_get_ttf).
 */

#include <kunit/test.h>
#include <linux/fs.h>
#include <linux/power_supply.h>
#include <misc/logbuffer.h>
#include "../gbms_storage.h"
#include "../qmath.h"
#include <misc/gvotable.h>
#include "../google_bms.h"

struct batt_ttf_stats;
struct gbms_charging_event;
struct batt_drv;

void power_supply_unreg_notifier(struct notifier_block *nb) {}
void logbuffer_unregister(struct logbuffer *lb) {}
void gbms_storage_cleanup_device(struct gbms_storage_device *gdev) {}
void gbms_free_chg_profile(struct gbms_chg_profile *profile) {}
int gvotable_destroy_election(struct gvotable_election *el) { return 0; }

int gvotable_get_int_vote(struct gvotable_election *el, const char *reason) { return 0; }
int power_supply_get_property(struct power_supply *psy, enum power_supply_property psp,
			      union power_supply_propval *val) { return 0; }

static int mock_ttf_soc_estimate_rc;
static ktime_t mock_ttf_soc_estimate_res;
static bool mock_ttf_soc_estimate_enable;

static int mock_ttf_soc_estimate(ktime_t *res, struct batt_ttf_stats *stats,
				 const struct gbms_charging_event *ce_data,
				 qnum_t from_soc, qnum_t to_soc, int vbatt_idx)
{
	if (mock_ttf_soc_estimate_enable) {
		*res = mock_ttf_soc_estimate_res;
		return mock_ttf_soc_estimate_rc;
	}
	return -EINVAL;
}

#undef pr_fmt

/* Mock ttf_soc_estimate logic */
#define ttf_soc_estimate mock_ttf_soc_estimate
#include "../google_battery.c"

struct gbatt_ttf_test_ctx {
	struct batt_drv *batt_drv;
	struct power_supply dummy_fg_psy;
};

static int gbatt_ttf_test_init(struct kunit *test)
{
	struct gbatt_ttf_test_ctx *ctx;

	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx);

	ctx->batt_drv = kunit_kzalloc(test, sizeof(*ctx->batt_drv), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->batt_drv);

	mutex_init(&ctx->batt_drv->batt_lock);
	mutex_init(&ctx->batt_drv->chg_lock);
	mutex_init(&ctx->batt_drv->ttf_stats.ttf_lock);

	ctx->batt_drv->fg_psy = &ctx->dummy_fg_psy;
	ctx->batt_drv->ttf_stats.ttf_fake = -1;
	ctx->batt_drv->chg_health.rest_deadline = 0;

	mock_ttf_soc_estimate_enable = false;
	mock_ttf_soc_estimate_rc = 0;
	mock_ttf_soc_estimate_res = 0;

	test->priv = ctx;
	return 0;
}

// ************************
// TEST
// ************************

static void no_power_return_error(struct kunit *test)
{
	struct gbatt_ttf_test_ctx *ctx = test->priv;
	struct batt_drv *batt_drv = ctx->batt_drv;
	int ttf;

	batt_drv->ssoc_state.buck_enabled = 0;

	ttf = gbatt_get_ttf(batt_drv);

	KUNIT_EXPECT_EQ(test, ttf, -EINVAL);
}

static void algorithm_error(struct kunit *test)
{
	struct gbatt_ttf_test_ctx *ctx = test->priv;
	struct batt_drv *batt_drv = ctx->batt_drv;
	int ttf;

	batt_drv->ssoc_state.buck_enabled = 1;
	mock_ttf_soc_estimate_enable = true;
	mock_ttf_soc_estimate_rc = -123;

	ttf = gbatt_get_ttf(batt_drv);

	KUNIT_EXPECT_EQ(test, ttf, -123);
}

static void algorithm_ok_estimate_positive(struct kunit *test)
{
	struct gbatt_ttf_test_ctx *ctx = test->priv;
	struct batt_drv *batt_drv = ctx->batt_drv;
	int ttf;

	batt_drv->ssoc_state.buck_enabled = 1;

	mock_ttf_soc_estimate_enable = true;
	mock_ttf_soc_estimate_res = 1234;
	mock_ttf_soc_estimate_rc = 100;

	ttf = gbatt_get_ttf(batt_drv);

	KUNIT_EXPECT_EQ(test, ttf, 1234);
}

static void algorithm_ok_estimate_negative(struct kunit *test)
{
	struct gbatt_ttf_test_ctx *ctx = test->priv;
	struct batt_drv *batt_drv = ctx->batt_drv;
	int ttf;

	batt_drv->ssoc_state.buck_enabled = 1;

	mock_ttf_soc_estimate_enable = true;
	mock_ttf_soc_estimate_res = -1234;
	mock_ttf_soc_estimate_rc = 100;

	ttf = gbatt_get_ttf(batt_drv);

	KUNIT_EXPECT_EQ(test, ttf, 0);
}

static struct kunit_case gbatt_ttf_test_cases[] = {
	KUNIT_CASE(no_power_return_error),
	KUNIT_CASE(algorithm_error),
	KUNIT_CASE(algorithm_ok_estimate_positive),
	KUNIT_CASE(algorithm_ok_estimate_negative),
	{}
};

static struct kunit_suite gbatt_ttf_test_suite = {
	.name = "google_battery_ttf_test",
	.init = gbatt_ttf_test_init,
	.test_cases = gbatt_ttf_test_cases,
};

kunit_test_suite(gbatt_ttf_test_suite);

MODULE_DESCRIPTION("KUnit test suite for real gbatt_get_ttf in google_battery.c");
MODULE_AUTHOR("Adam Solawa <solawaa@google.com>");
MODULE_LICENSE("GPL");
