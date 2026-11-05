/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2026 Google LLC
 */

#ifndef AOC_HEAP_H
#define AOC_HEAP_H

#include <linux/genalloc.h>
#include <linux/types.h>

struct dma_buf;
struct aoc_heap;

/**
 * typedef aoc_heap_callback - Callback for buffer allocation and freeing
 * @paddr: Physical address of the buffer
 * @size: Size of the buffer
 * @priv_val: Opaque value stored with the buffer (e.g. hash of paddr)
 * @ctx: Context pointer provided during heap creation
 */
typedef void (*aoc_heap_callback)(phys_addr_t paddr, size_t size, u64 priv_val, void *ctx);

/**
 * aoc_heap_create - Create a DMA-BUF heap for AoC carveout memory
 * @base: Physical base address of the carveout
 * @size: Size of the carveout
 * @align: Alignment for allocations within the heap
 * @name: Name of the heap (e.g. "aoc_tpu_heap")
 * @alloc_cb: Optional callback on buffer allocation
 * @free_cb: Optional callback on buffer release
 * @ctx: Opaque context for callbacks
 *
 * Returns a pointer to the created dma_heap or an ERR_PTR.
 */
struct dma_heap *aoc_heap_create(phys_addr_t base, size_t size, size_t align,
				  const char *name, aoc_heap_callback alloc_cb,
				  aoc_heap_callback free_cb, void *ctx);

bool is_aoc_heap_buffer(struct dma_buf *dmabuf);
phys_addr_t aoc_heap_get_paddr(struct dma_buf *dmabuf);
u64 aoc_heap_get_priv_val(struct dma_buf *dmabuf);

#endif /* AOC_HEAP_H */
