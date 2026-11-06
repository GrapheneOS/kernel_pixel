# SPDX-License-Identifier: GPL-2.0-only
gcip-objects		:= gcip-alloc-helper.o \
			   gcip-devfreq.o \
			   gcip-dma-fence.o \
			   gcip-domain-pool.o \
			   gcip-fault-injection.o \
			   gcip-fence-array.o \
			   gcip-fence.o \
			   gcip-firmware.o \
			   gcip-image-config.o \
			   gcip-iommu-reserve.o \
			   gcip-iommu.o \
			   gcip-kci.o \
			   gcip-mailbox.o \
			   gcip-mem-pool.o \
			   gcip-pm.o \
			   gcip-resource-accessor.o \
			   gcip-slc.o \
			   gcip-telemetry.o \
			   gcip-thermal.o \
			   gcip-usage-stats.o

gcip-objects		+= iif/iif-dma-fence.o \
			   iif/iif-fence-table.o \
			   iif/iif-fence.o \
			   iif/iif-manager.o \
			   iif/iif-signaler-submission-waiter.o \
			   iif/iif-sync-file.o
