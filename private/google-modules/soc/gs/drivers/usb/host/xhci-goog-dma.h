/* SPDX-License-Identifier: GPL-2.0 */
/*
 * xhci-goog-dma.h - DMA coherent driver for xHCI.
 *
 * Copyright (c) 2024 Google LLC
 *
 * Author: Howard Yen <howardyen@google.com>
 */
#ifndef _XHCI_GOOG_DMA_H
#define _XHCI_GOOG_DMA_H

#define XHCI_GOOG_DMA_RMEM_SRAM 0
#define XHCI_GOOG_DMA_RMEM_DRAM 1
#define XHCI_GOOG_DMA_RMEM_MAX  2

struct device;

int xhci_goog_setup_dma_pool(struct device *dev);
void xhci_goog_release_dma_pool(struct device *dev);

#endif /* _XHCI_GOOG_DMA_H */
