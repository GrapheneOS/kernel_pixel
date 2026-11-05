// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Google LLC.
 */

#include <linux/device.h>

#include "fsm-listener.h"
#include "link-exception.h"
#include "modem_metrics.h"
#include "mtk_fsm.h"
#include "radio-utils.h"
#include "ramdump-stat.h"

static void fsm_pre_handler(struct mtk_fsm_param *param, void *data)
{
	struct radio_google *goog = data;

	modem_metrics_fsm_state_handler(goog, param);
}

static void fsm_post_handler(struct mtk_fsm_param *param, void *data)
{
	struct radio_google *goog = data;

	link_exception_fsm_state_handler(goog, param);
	modem_cdd_fsm_state_handler(goog, param);
}

int fsm_listener_init(struct radio_google *goog)
{
	int ret;

	ret = goog->tmi_ops->fsm.notifier_register(goog->mdev, MTK_USER_GOOGLE, fsm_pre_handler,
						   goog, FSM_PRIO_0, true);
	if (ret) {
		LOG_ERR("Failed to register fsm pre notifier, ret=%d\n", ret);
		return ret;
	}

	/*
	 * Bump to FSM_PRIO_1 to ensure link_exception_fsm_state_handler can be executed before
	 * vendor driver triggers a modem force crash.
	 */
	ret = goog->tmi_ops->fsm.notifier_register(goog->mdev, MTK_USER_GOOGLE, fsm_post_handler,
						   goog, FSM_PRIO_1, false);
	if (ret) {
		LOG_ERR("Failed to register fsm post notifier, ret=%d\n", ret);
		goog->tmi_ops->fsm.notifier_unregister(goog->mdev, MTK_USER_GOOGLE);
		return ret;
	}

	return 0;
}

void fsm_listener_exit(struct radio_google *goog)
{
	goog->tmi_ops->fsm.notifier_unregister(goog->mdev, MTK_USER_GOOGLE);
}
