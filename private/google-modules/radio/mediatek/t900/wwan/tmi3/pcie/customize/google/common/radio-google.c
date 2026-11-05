// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Google LLC.
 */

#include <linux/pci.h>

#include "ap-awake.h"
#include "feature-control.h"
#include "fsm-listener.h"
#include "link-exception.h"
#include "md2ap-wakemon.h"
#include "modem_metrics.h"
#include "mtk-pcie-pm-user-internal.h"
#include "mtk-pcie.h"
#include "mtk_dev.h"
#include "radio-google.h"
#include "radio-utils.h"
#include "remote-wakeup.h"
#include "soc-qos-ext.h"
#include "dpmaif-google.h"
#include "wwan-mllb.h"

/**
 * radio_google_early_init() - Early initialization of Google radio features.
 * @mdev: pointer to mtk_md_dev
 * @ops: pointer to tmi_ops
 *
 * This function is called BEFORE mtk_pci_dev_init(). It allocates the
 * radio_google control block and initializes early/global features.
 *
 * Return: 0 on success, negative error code on failure.
 */
int radio_google_early_init(struct mtk_md_dev *mdev, struct tmi_ops *ops)
{
	struct radio_google *radio_google;
	int ret;

	if (!mdev || !mdev->dev || !ops)
		return -EINVAL;

	radio_google = devm_kzalloc(mdev->dev, sizeof(*radio_google), GFP_KERNEL);
	if (!radio_google) {
		ret = -ENOMEM;
		goto out;
	}

	radio_google->mdev = mdev;
	radio_google->tmi_ops = ops;
	mdev->google = radio_google;

	google_feature_control_init();

	ret = dpmaif_google_affinity_init();
	if (ret) {
		LOG_ERR("dpmaif_google_affinity_init() failed, ret=%d\n", ret);
		goto free_google_feature_control;
	}

	return 0;

free_google_feature_control:
	google_feature_control_exit();
	devm_kfree(mdev->dev, radio_google);
	mdev->google = NULL;
out:
	return ret;
}
EXPORT_SYMBOL_GPL(radio_google_early_init);

/**
 * radio_google_late_exit() - Late exit/cleanup of Google radio features.
 * @mdev: pointer to mtk_md_dev
 *
 * This function is called AFTER mtk_pci_dev_exit() (and mtk_pci_free_irq()).
 * It cleans up asymmetric modules (like md2ap_wakemon) that need to stay alive
 * during core driver teardown, performs global cleanup, and finally frees
 * the radio_google control block.
 */
void radio_google_late_exit(struct mtk_md_dev *mdev)
{
	struct radio_google *radio_google;

	if (!mdev || !mdev->google)
		return;

	radio_google = mdev->google;

	md2ap_wakemon_exit(radio_google);
	dpmaif_google_affinity_exit();
	google_feature_control_exit();
	devm_kfree(mdev->dev, radio_google);
	mdev->google = NULL;
}
EXPORT_SYMBOL_GPL(radio_google_late_exit);

/**
 * radio_google_init() - Main initialization of Google radio features.
 * @mdev: pointer to mtk_md_dev
 *
 * This function is called AFTER mtk_pci_dev_init(). It initializes
 * mandatory and optional device-specific features.
 *
 * Return: 0 on success, negative error code on failure.
 */
int radio_google_init(struct mtk_md_dev *mdev)
{
	struct radio_google *radio_google;
	int ret;

	if (!mdev || !mdev->google)
		return -EINVAL;

	radio_google = mdev->google;

	/* Mandatory features */

	ret = mtk_google_pcie_init(radio_google);
	if (ret) {
		LOG_ERR("mtk_google_pcie_init() failed, ret=%d\n", ret);
		goto out;
	}

	ret = remote_wakeup_init(radio_google);
	if (ret) {
		LOG_ERR("remote_wakeup_init() failed, ret=%d\n", ret);
		goto free_mtk_google_pcie;
	}

	ret = mtk_pci_user_init(radio_google);
	if (ret) {
		LOG_ERR("mtk_pci_user_init() failed, ret=%d\n", ret);
		goto free_remote_wakeup;
	}

	ret = fsm_listener_init(radio_google);
	if (ret) {
		LOG_ERR("fsm_listener_init() failed, ret=%d\n", ret);
		goto free_mtk_pci_user;
	}

	ret = dpmaif_google_lro_size_limit_init();
	if (ret) {
		LOG_ERR("dpmaif_google_lro_size_limit_init() failed, ret=%d\n", ret);
		goto free_fsm_listener;
	}

	/* Optional features */

	ret = link_exception_init(radio_google);
	if (ret)
		LOG_WARN("link_exception_init() failed, ret=%d\n", ret);

	ret = soc_qos_init(radio_google);
	if (ret)
		LOG_WARN("soc_qos_init() failed, ret=%d\n", ret);

	ret = modem_metrics_init(radio_google);
	if (ret)
		LOG_WARN("modem_metrics_init() failed, ret=%d\n", ret);

	ret = md2ap_wakemon_init(radio_google);
	if (ret)
		LOG_WARN("md2ap_wakemon_init() failed, ret=%d\n", ret);

	ret = ap_awake_gpio_init(radio_google);
	if (ret)
		LOG_WARN("ap_awake_gpio_init() failed, ret=%d\n", ret);

	ret = wwan_mllb_init();
	if (ret)
		LOG_WARN("wwan_mllb_init() failed, ret=%d\n", ret);

	return 0;

free_fsm_listener:
	fsm_listener_exit(radio_google);
free_mtk_pci_user:
	mtk_pci_user_exit(radio_google);
free_remote_wakeup:
	remote_wakeup_exit(radio_google);
free_mtk_google_pcie:
	mtk_google_pcie_exit(radio_google);
out:
	LOG_ERR("Failed to init radio_google, ret=%d\n", ret);

	return ret;
}
EXPORT_SYMBOL_GPL(radio_google_init);

/**
 * radio_google_exit() - Main exit/cleanup of Google radio features.
 * @mdev: pointer to mtk_md_dev
 *
 * This function is called BEFORE mtk_pci_dev_exit(). It de-initializes
 * the majority of Google-specific features. Asymmetric modules (like
 * md2ap_wakemon) are skipped here and cleaned up in late_exit instead.
 */
void radio_google_exit(struct mtk_md_dev *mdev)
{
	struct radio_google *radio_google;

	if (!mdev || !mdev->google)
		return;

	radio_google = mdev->google;

	wwan_mllb_exit();
	modem_metrics_exit(radio_google);
	soc_qos_exit(radio_google);
	dpmaif_google_lro_size_limit_exit();
	fsm_listener_exit(radio_google);
	mtk_pci_user_exit(radio_google);
	remote_wakeup_exit(radio_google);
	mtk_google_pcie_exit(radio_google);
}
EXPORT_SYMBOL_GPL(radio_google_exit);

MODULE_AUTHOR("Mahesh Kallelil <kallelil@google.com>");
MODULE_DESCRIPTION("Module for Google radio features");
MODULE_LICENSE("GPL");
