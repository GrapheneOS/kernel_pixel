/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2026 Google LLC.
 */

#ifndef __LINK_EXCEPTION_H__
#define __LINK_EXCEPTION_H__

#include <linux/bits.h>
#include <linux/device.h>
#include <linux/pci.h>
#include <linux/types.h>

#include "../common/radio-google.h"
#include "mtk_fsm.h"

/* If multiple errors are reported, the largest number will be treated as the main reason. */
#define EXCP_REASONS_LIST(X)                                        \
	X(CLDMA_ERROR, 0, "CLDMA error")                            \
	X(CLDMA_TX_TIMEOUT, 1, "CLDMA TX timeout")                  \
	X(CLDMA_RX_HWO_ERROR, 2, "CLDMA RX HWO error")              \
	X(CLDMA_TX_HWO_ERROR, 3, "CLDMA TX HWO error")              \
	X(CCCI_PKT_OUT_OF_ORDER, 4, "CCCI packet out-of-order")     \
	X(COR_ERROR, 5, "PCIe correctable error")                   \
	X(LINK_ERROR, 6, "Driver-detected PCIe link error")         \
	X(UNCOR_ERROR, 7, "PCIe uncorrectable error")               \
	X(CPL_TIMEOUT, 8, "PCIe completion timeout")                \
	X(LINK_DOWN, 9, "PCIe surprise down")                       \
	X(LINK_UNREADY, 10, "PCIe link unready")                    \
	X(DS_LOCK_TIMEOUT, 11, "DS lock timeout")                   \
	X(L3_RESUME_FAIL, 12, "L3 resume failure")                  \
	X(L2_RESUME_FAIL, 13, "L2 resume failure")                  \
	X(L2_EXCEPT_RESUME_FAIL, 14, "L2 exception resume failure") \
	X(MD_SUSPEND_TIMEOUT, 15, "MDMCU suspend timeout")          \
	X(SAP_SUSPEND_TIMEOUT, 16, "HKMCU suspend timeout")         \
	X(SUSPEND_TIMEOUT, 17, "PCIe suspend timeout")              \
	X(MD_RESUME_TIMEOUT, 18, "MDMCU resume timeout")            \
	X(SAP_RESUME_TIMEOUT, 19, "HKMCU resume timeout")           \
	X(RESUME_TIMEOUT, 20, "PCIe resume timeout")                \
	X(COLD_RESUME, 21, "PCIe cold resume")

enum link_exception_reason {
#define ENUM_GEN(name, bit, desc) EXCP_REASON_##name = BIT(bit),
	EXCP_REASONS_LIST(ENUM_GEN)
#undef ENUM_GEN
};

struct link_exception {
	struct radio_google *goog;
	atomic_t reason_flags;
};

int link_exception_init(struct radio_google *goog);

void link_exception_fsm_state_handler(struct radio_google *goog, struct mtk_fsm_param *param);

void radio_google_link_error_handler(struct radio_google *goog);

void radio_google_uncor_error_handler(struct radio_google *goog);

void radio_google_error_detected_post(struct pci_dev *pdev, pci_channel_state_t state);

void radio_google_cor_error_detected(struct pci_dev *pdev);

void radio_google_cold_resume_handler(struct radio_google *goog);

void radio_google_link_unready_handler(struct radio_google *goog, u32 resume_state);

void radio_google_pm_except_handler(struct radio_google *goog, u32 err);

void radio_google_cldma_error_handler(struct radio_google *goog, enum link_exception_reason reason);

void radio_google_port_error_handler(struct radio_google *goog, enum link_exception_reason reason);

#endif /* __LINK_EXCEPTION_H__ */
