// SPDX-License-Identifier: GPL-2.0
/*
 * xhci-goog-dma.c - DMA coherent driver for xHCI.
 *
 * Copyright (c) 2024 Google LLC
 *
 * Author: Howard Yen <howardyen@google.com>
 */

#include <linux/dma-map-ops.h>
#include <linux/module.h>
#include <linux/of_reserved_mem.h>

#include "xhci-goog-dma.h"

int xhci_goog_setup_dma_pool(struct device *dev)
{
	struct dma_coherent_region regions[XHCI_GOOG_DMA_RMEM_MAX];
	struct device_node *np;
	struct reserved_mem *rmem;
	int i;

	for (i = 0; i < XHCI_GOOG_DMA_RMEM_MAX; i++) {
		np = of_parse_phandle(dev->of_node, "memory-region", i);
		if (!np)
			return -EINVAL;
		rmem = of_reserved_mem_lookup(np);
		of_node_put(np);
		if (!rmem)
			return -EINVAL;

		regions[i].phys_addr = rmem->base;
		regions[i].device_addr = rmem->base;
		regions[i].size = rmem->size;
	}

	/* Prefer SRAM, then DRAM; keep the IOMMU DMA backend for streaming maps. */
	return dma_declare_coherent_memory_regions(dev, regions, ARRAY_SIZE(regions));
}
EXPORT_SYMBOL_GPL(xhci_goog_setup_dma_pool);

void xhci_goog_release_dma_pool(struct device *dev)
{
	dma_release_coherent_memory(dev);
}
EXPORT_SYMBOL_GPL(xhci_goog_release_dma_pool);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("GOOG XHCI RMEM and DMA driver");
MODULE_AUTHOR("Howard Yen <howardyen@google.com>");
