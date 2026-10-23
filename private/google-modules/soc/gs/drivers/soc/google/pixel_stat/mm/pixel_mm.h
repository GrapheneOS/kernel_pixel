/* SPDX-License-Identifier: GPL-2.0-only */

#include <linux/types.h>

struct zone;
struct address_space;
struct folio;

void vh_rmqueue_mod(void *data, struct zone *preferred_zone,
		struct zone *zone, unsigned int order, gfp_t gfp_flags,
		unsigned int alloc_flags, int migratetype);
int pixel_mm_sysfs(void);
void vh_filemap_get_folio_mod(void *data,
		struct address_space *mapping, pgoff_t index,
		int fgp_flags, gfp_t gfp_mask, struct folio *folio);
void rvh_mapping_shrinkable(void *data, bool *shrinkable);
int create_mm_procfs_node(void);
