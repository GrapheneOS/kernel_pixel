// SPDX-License-Identifier: GPL-2.0-only
/*
 * Custom AoC DMA-BUF Heap Implementation
 *
 * This implementation avoids relying on struct page for no-map carveouts.
 *
 * Copyright (c) 2026 Google LLC
 */

#include <linux/align.h>
#include <linux/device.h>
#include <linux/dma-buf.h>
#include <linux/dma-heap.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/genalloc.h>
#include <linux/io.h>
#include <linux/iosys-map.h>
#include <linux/mm.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>
#include <linux/types.h>

#include "aoc_heap.h"

struct aoc_heap {
	struct gen_pool *pool;
	struct device *dev;
	aoc_heap_callback alloc_cb;
	aoc_heap_callback free_cb;
	void *cb_ctx;
	size_t align;
};

struct aoc_heap_buffer {
	struct aoc_heap *heap;
	phys_addr_t paddr;
	size_t size;
	u64 priv_val;
};

static struct sg_table *aoc_dma_buf_map(struct dma_buf_attachment *attach,
					 enum dma_data_direction direction)
{
	struct aoc_heap_buffer *buffer = attach->dmabuf->priv;
	struct sg_table *sgt;
	dma_addr_t addr;

	sgt = kzalloc(sizeof(*sgt), GFP_KERNEL);
	if (!sgt)
		return ERR_PTR(-ENOMEM);

	if (sg_alloc_table(sgt, 1, GFP_KERNEL)) {
		kfree(sgt);
		return ERR_PTR(-ENOMEM);
	}

	/*
	 * Map physical carveout as a resource to bypass struct page checks.
	 * This is safe for no-map memory.
	 */
	attach->dma_map_attrs |= (DMA_ATTR_NO_KERNEL_MAPPING | DMA_ATTR_SKIP_CPU_SYNC);
	addr = dma_map_resource(attach->dev, buffer->paddr, buffer->size,
				direction, DMA_ATTR_SKIP_CPU_SYNC);

	if (dma_mapping_error(attach->dev, addr)) {
		sg_free_table(sgt);
		kfree(sgt);
		return ERR_PTR(-ENOMEM);
	}

	sgt->sgl->offset = 0;
	sgt->sgl->length = buffer->size;
	sg_dma_address(sgt->sgl) = addr;
	sg_dma_len(sgt->sgl) = buffer->size;

	return sgt;
}

static void aoc_dma_buf_unmap(struct dma_buf_attachment *attach,
			       struct sg_table *sgt,
			       enum dma_data_direction direction)
{
	dma_unmap_resource(attach->dev, sg_dma_address(sgt->sgl),
			    sg_dma_len(sgt->sgl), direction,
			    DMA_ATTR_SKIP_CPU_SYNC);
	sg_free_table(sgt);
	kfree(sgt);
}

static void aoc_dma_buf_release(struct dma_buf *dmabuf)
{
	struct aoc_heap_buffer *buffer = dmabuf->priv;
	struct aoc_heap *heap = buffer->heap;
	size_t aligned_size = ALIGN(buffer->size, heap->align);
	void __iomem *vaddr;

	if (heap->free_cb)
		heap->free_cb(buffer->paddr, buffer->size, buffer->priv_val, heap->cb_ctx);

	/* Zero the buffer before releasing */
	vaddr = ioremap_wc(buffer->paddr, aligned_size);
	if (vaddr) {
		memset_io(vaddr, 0, aligned_size);
		iounmap(vaddr);
	}

	gen_pool_free(heap->pool, buffer->paddr, aligned_size);

	kfree(buffer);
}

static int aoc_dma_buf_mmap(struct dma_buf *dmabuf, struct vm_area_struct *vma)
{
	struct aoc_heap_buffer *buffer = dmabuf->priv;

	vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);

	return remap_pfn_range(vma, vma->vm_start, buffer->paddr >> PAGE_SHIFT,
				vma->vm_end - vma->vm_start, vma->vm_page_prot);
}

static int aoc_dma_buf_vmap(struct dma_buf *dmabuf, struct iosys_map *map)
{
	struct aoc_heap_buffer *buffer = dmabuf->priv;
	void __iomem *vaddr;

	vaddr = ioremap_wc(buffer->paddr, buffer->size);
	if (!vaddr)
		return -ENOMEM;

	iosys_map_set_vaddr_iomem(map, vaddr);
	return 0;
}

static void aoc_dma_buf_vunmap(struct dma_buf *dmabuf, struct iosys_map *map)
{
	iounmap(map->vaddr_iomem);
	iosys_map_clear(map);
}

static const struct dma_buf_ops aoc_dma_buf_ops = {
	.map_dma_buf = aoc_dma_buf_map,
	.unmap_dma_buf = aoc_dma_buf_unmap,
	.release = aoc_dma_buf_release,
	.mmap = aoc_dma_buf_mmap,
	.vmap = aoc_dma_buf_vmap,
	.vunmap = aoc_dma_buf_vunmap,
};

bool is_aoc_heap_buffer(struct dma_buf *dmabuf)
{
	if (dmabuf == NULL)
		return false;
	return dmabuf->ops == &aoc_dma_buf_ops;
}
EXPORT_SYMBOL_GPL(is_aoc_heap_buffer);

phys_addr_t aoc_heap_get_paddr(struct dma_buf *dmabuf)
{
	struct aoc_heap_buffer *buffer;

	if (dmabuf == NULL)
		return 0;
	if (dmabuf->ops != &aoc_dma_buf_ops)
		return 0;

	buffer = dmabuf->priv;
	return buffer->paddr;
}
EXPORT_SYMBOL_GPL(aoc_heap_get_paddr);

u64 aoc_heap_get_priv_val(struct dma_buf *dmabuf)
{
	struct aoc_heap_buffer *buffer;

	if (dmabuf == NULL)
		return 0;
	if (dmabuf->ops != &aoc_dma_buf_ops)
		return 0;

	buffer = dmabuf->priv;
	return buffer->priv_val;
}
EXPORT_SYMBOL_GPL(aoc_heap_get_priv_val);

static struct dma_buf *aoc_heap_allocate(struct dma_heap *dma_heap,
					  unsigned long size,
					  u32 fd_flags,
					  u64 heap_flags)
{
	struct aoc_heap *heap = dma_heap_get_drvdata(dma_heap);
	struct aoc_heap_buffer *buffer;
	DEFINE_DMA_BUF_EXPORT_INFO(exp_info);
	struct dma_buf *dmabuf;
	phys_addr_t paddr;
	unsigned long aligned_size = ALIGN(size, heap->align);

	paddr = gen_pool_alloc(heap->pool, aligned_size);
	if (!paddr)
		return ERR_PTR(-ENOMEM);

	buffer = kzalloc(sizeof(*buffer), GFP_KERNEL);
	if (!buffer) {
		gen_pool_free(heap->pool, paddr, aligned_size);
		return ERR_PTR(-ENOMEM);
	}

	buffer->heap = heap;
	buffer->paddr = paddr;
	buffer->size = size;
	buffer->priv_val = paddr;

	exp_info.ops = &aoc_dma_buf_ops;
	exp_info.size = size;
	exp_info.flags = fd_flags;
	exp_info.priv = buffer;

	dmabuf = dma_buf_export(&exp_info);
	if (IS_ERR(dmabuf)) {
		kfree(buffer);
		gen_pool_free(heap->pool, paddr, aligned_size);
		return dmabuf;
	}

	if (heap->alloc_cb)
		heap->alloc_cb(buffer->paddr, buffer->size, buffer->priv_val, heap->cb_ctx);

	return dmabuf;
}

static const struct dma_heap_ops aoc_dma_heap_ops = {
	.allocate = aoc_heap_allocate,
};

static void aoc_heap_destroy_pool(void *data)
{
	struct gen_pool *pool = data;

	gen_pool_destroy(pool);
}

struct dma_heap *aoc_heap_create(phys_addr_t base, size_t size, size_t align,
				  const char *name, aoc_heap_callback alloc_cb,
				  aoc_heap_callback free_cb, void *ctx)
{
	struct aoc_heap *heap;
	struct dma_heap_export_info exp_info;
	struct dma_heap *dma_heap;
	struct device *dev = ctx;
	size_t aligned_size = ALIGN(size, align);
	void __iomem *vaddr;
	int ret;

	/* Zero the memory carveout */
	vaddr = ioremap_wc(base, aligned_size);
	if (vaddr) {
		memset_io(vaddr, 0, aligned_size);
		iounmap(vaddr);
	} else {
		dev_err(dev, "failed to map carveout for zeroing\n");
		return ERR_PTR(-ENOMEM);
	}

	heap = devm_kzalloc(dev, sizeof(*heap), GFP_KERNEL);
	if (!heap)
		return ERR_PTR(-ENOMEM);

	heap->pool = gen_pool_create(get_order(align) + PAGE_SHIFT, -1);
	if (!heap->pool) {
		devm_kfree(dev, heap);
		return ERR_PTR(-ENOMEM);
	}

	ret = devm_add_action_or_reset(dev, aoc_heap_destroy_pool, heap->pool);
	if (ret) {
		devm_kfree(dev, heap);
		return ERR_PTR(ret);
	}

	if (gen_pool_add(heap->pool, base, size, -1)) {
		devm_kfree(dev, heap);
		return ERR_PTR(-ENOMEM);
	}

	heap->dev = dev;
	heap->alloc_cb = alloc_cb;
	heap->free_cb = free_cb;
	heap->cb_ctx = ctx;
	heap->align = align;

	exp_info.name = name;
	exp_info.ops = &aoc_dma_heap_ops;
	exp_info.priv = heap;

	dma_heap = dma_heap_add(&exp_info);
	if (IS_ERR(dma_heap)) {
		devm_kfree(dev, heap);
		return dma_heap;
	}

	return dma_heap;
}

