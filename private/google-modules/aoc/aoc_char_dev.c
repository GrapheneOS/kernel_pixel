// SPDX-License-Identifier: GPL-2.0-only
/* Copyright 2019 Google LLC. All Rights Reserved.
 *
 * Character device interface for AoC services
 * ACD - AoC Character Device
 */

#define pr_fmt(fmt) "aoc_char: " fmt

#include <linux/dma-buf.h>
#include <linux/fs.h>
#include <linux/kref.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <iif/iif-fence.h>
#include <iif/iif.h>

#include "aoc.h"
#include "aoc_ipc_core.h"
#include "aoc-interface.h"

#include "aoc_heap.h"

#define ACD_CHARDEV_NAME "aoc_char"

static int acd_major = -1;
static int acd_major_dev;
static unsigned int acd_next_minor; /* protected by acd_devices_lock */
static struct class *acd_class;

struct acd_device_entry {
	struct device *acd_device;
	struct aoc_service_dev *service;
	bool opened;
	struct list_head list;
	struct kref refcount;

#if AOC_USE_TPU_OFFLOAD
	struct mutex lock;
	/* following are protected by lock */
	int iif_pair_count;
	struct iif_fence *aoc_ready_fence[TPU_OFFLOAD_BATCH_COUNT_MAX];
	struct iif_fence *tpu_ready_fence[TPU_OFFLOAD_BATCH_COUNT_MAX];
	struct dma_buf *dmabuf;
	int32_t tpu_offload_id;
#endif
};

static LIST_HEAD(acd_devices_list); /* protected by acd_devices_lock */
static DEFINE_MUTEX(acd_devices_lock);

/* Driver methods */
static int acd_probe(struct aoc_service_dev *dev);
static int acd_remove(struct aoc_service_dev *dev);

static struct aoc_driver aoc_char_driver = {
	.drv = {
			.name = "aoc_char",
		},
	.probe = acd_probe,
	.remove = acd_remove,
};

/* File methods */
static int acd_open(struct inode *inode, struct file *file);
static int acd_release(struct inode *inode, struct file *file);
static long acd_unlocked_ioctl(struct file *file, unsigned int cmd,
			       unsigned long arg);
static ssize_t acd_read(struct file *file, char __user *buf, size_t count,
			loff_t *off);
static ssize_t acd_write(struct file *file, const char __user *buf,
			 size_t count, loff_t *off);
static unsigned int acd_poll(struct file *file, poll_table *wait);

static const struct file_operations fops = {
	.open = acd_open,
	.release = acd_release,
	.unlocked_ioctl = acd_unlocked_ioctl,
	.read = acd_read,
	.write = acd_write,
	.poll = acd_poll,

	.owner = THIS_MODULE,
};

static void acd_device_entry_release(struct kref *ref)
{
	kfree(container_of(ref, struct acd_device_entry, refcount));
}

/*
 * Caller must hold acd_devices_lock.
 */
static struct acd_device_entry *acd_device_entry_for_inode(struct inode *inode)
{
	struct acd_device_entry *entry;

	list_for_each_entry(entry, &acd_devices_list, list) {
		/* entries with service->dead == true can't be in acd_devices_list */
		if (entry->acd_device->devt == inode->i_rdev)
			return entry;
	}

	return NULL;
}

static char *acd_devnode(const struct device *dev, umode_t *mode)
{
	if (!mode || !dev)
		return NULL;

	if (MAJOR(dev->devt) == acd_major)
		*mode = 0666;

	return kasprintf(GFP_KERNEL, "%s", dev_name(dev));
}

static int create_character_device(struct aoc_service_dev *dev)
{
	int rc = 0;
	struct acd_device_entry *new_entry;

	new_entry = kzalloc(sizeof(*new_entry), GFP_KERNEL);
	if (!new_entry) {
		rc = -ENOMEM;
		goto err_kmalloc;
	}
#if AOC_USE_TPU_OFFLOAD
	new_entry->tpu_offload_id = -1;
	mutex_init(&new_entry->lock);
#endif

	mutex_lock(&acd_devices_lock);
	new_entry->acd_device = device_create(acd_class, &dev->dev,
					      MKDEV(acd_major, acd_next_minor), NULL,
					      "acd-%s", dev_name(&dev->dev));
	if (IS_ERR(new_entry->acd_device)) {
		pr_err("device_create failed: %ld\n", PTR_ERR(new_entry->acd_device));
		rc = PTR_ERR(new_entry->acd_device);
		goto err_device_create;
	}
	get_device(&dev->dev);
	new_entry->service = dev;
	acd_next_minor++;
	new_entry->opened = false;
	kref_init(&new_entry->refcount);
	list_add(&new_entry->list, &acd_devices_list);
	mutex_unlock(&acd_devices_lock);
	return 0;

err_device_create:
	mutex_unlock(&acd_devices_lock);
	kfree(new_entry);
err_kmalloc:
	return rc;
}

static int acd_open(struct inode *inode, struct file *file)
{
	int rc = 0;
	struct acd_device_entry *entry;

	pr_debug("attempt to open major:%d minor:%d\n", MAJOR(inode->i_rdev),
		 MINOR(inode->i_rdev));

	mutex_lock(&acd_devices_lock);
	entry = acd_device_entry_for_inode(inode);
	if (!entry) {
		rc = -ENODEV;
		goto err_acd_device_entry;
	}

	if (entry->opened) {
		rc = -EBUSY;
		goto err_open;
	}

	kref_get(&entry->refcount);
	get_device(&entry->service->dev);
	entry->opened = true;
	file->private_data = entry;
	mutex_unlock(&acd_devices_lock);
	return 0;

err_open:
err_acd_device_entry:
	mutex_unlock(&acd_devices_lock);
	return rc;
}

#if AOC_USE_TPU_OFFLOAD
static int acd_tpu_offload_cleanup(struct file *file, struct acd_device_entry *entry)
{
	int ret = 0;

	mutex_lock(&entry->lock);
	if (entry->tpu_offload_id >= 0) {
		bool should_block = ((file->f_flags & O_NONBLOCK) == 0);
		struct CMD_AUDIO_INPUT_TPU_OFFLOAD_STOP cmd = {0};

		AocCmdHdrSet(&(cmd.parent), CMD_AUDIO_INPUT_TPU_OFFLOAD_STOP_ID, sizeof(cmd));
		cmd.tpu_offload_id = entry->tpu_offload_id;

		ret = aoc_service_write(entry->service, (uint8_t *)&cmd, sizeof(cmd), should_block);
		if (ret == sizeof(cmd))
			ret = aoc_service_read(entry->service, (uint8_t *)&cmd,
				sizeof(cmd), should_block);

		if (ret >= 0) {
			if (ret != sizeof(cmd))
				ret = -EIO;
			else
				ret = cmd.parent.reply;
		}
		entry->tpu_offload_id = -1;
	}

	if (entry->dmabuf) {
		dma_buf_put(entry->dmabuf);
		entry->dmabuf = NULL;
	}

	for (int i = 0; i < entry->iif_pair_count; i++) {
		if (entry->aoc_ready_fence[i]) {
			iif_fence_signaler_completed(entry->aoc_ready_fence[i]);
			iif_fence_put(entry->aoc_ready_fence[i]);
			entry->aoc_ready_fence[i] = NULL;
		}
		if (entry->tpu_ready_fence[i]) {
			iif_fence_waiter_completed(entry->tpu_ready_fence[i], IIF_IP_AOC);
			iif_fence_put(entry->tpu_ready_fence[i]);
			entry->tpu_ready_fence[i] = NULL;
		}
	}
	entry->iif_pair_count = 0;
	mutex_unlock(&entry->lock);

	return ret;
}
#endif

static int acd_release(struct inode *inode, struct file *file)
{
	struct acd_device_entry *entry = file->private_data;

	if (!entry)
		return -ENODEV;

#if AOC_USE_TPU_OFFLOAD
	if (entry->tpu_offload_id >= 0)
		acd_tpu_offload_cleanup(file, entry);
#endif

	entry->opened = false;
	put_device(&entry->service->dev);
	kref_put(&entry->refcount, acd_device_entry_release);
	file->private_data = NULL;

	return 0;
}

#if AOC_USE_TPU_OFFLOAD
static int acd_handle_tpu_offload_config(struct acd_device_entry *entry, struct file *file,
					 struct aoc_tpu_offload_config *config)
{
	int ret = 0;
	struct iif_fence *aoc_ready_fence = NULL;
	struct iif_fence *tpu_ready_fence = NULL;
	bool should_block = ((file->f_flags & O_NONBLOCK) == 0);
	struct CMD_AUDIO_INPUT_TPU_OFFLOAD_CONFIG cmd = {0};
	struct dma_buf *dmabuf = NULL;
	void *prvdata;
	phys_addr_t phys_addr;

	if (config->tpu_offload_id < 0)
		return -EINVAL;
	if (config->batch_count < 1 || config->batch_count > TPU_OFFLOAD_BATCH_COUNT_MAX)
		return -EINVAL;

	/* Ensure control channel is clean before sending config */
	aoc_service_flush_read_data(entry->service);

	mutex_lock(&entry->lock);
	if (entry->service->dead)
		ret = -ESHUTDOWN;
	else if (entry->iif_pair_count != 0 || entry->dmabuf != NULL)
		ret = -EINVAL;

	if (ret != 0)
		goto fail;

	dmabuf = dma_buf_get(config->dmabuf_fd);
	if (IS_ERR(dmabuf)) {
		ret = PTR_ERR(dmabuf);
		pr_err("dma_buf_get failed ret=%d\n", ret);
		goto fail;
	}

	if (!is_aoc_heap_buffer(dmabuf)) {
		dma_buf_put(dmabuf);
		ret = -EINVAL;
		pr_err("dmabuf is not from aoc-heap\n");
		goto fail;
	}
	phys_addr = aoc_heap_get_paddr(dmabuf);

	prvdata = dev_get_drvdata(entry->service->dev.parent);
	phys_addr = aoc_dram_translate_to_aoc(prvdata, phys_addr);

	entry->dmabuf = dmabuf;
	entry->tpu_offload_id = config->tpu_offload_id;

	AocCmdHdrSet(&(cmd.parent), CMD_AUDIO_INPUT_TPU_OFFLOAD_CONFIG_ID, sizeof(cmd));
	cmd.tpu_offload_id = config->tpu_offload_id;
	cmd.address = (uint32_t)phys_addr;
	cmd.batch_count = config->batch_count;
	cmd.period_ms = config->period_ms;
	cmd.batch_size = config->batch_size;

	// Input Chunk Config
	cmd.input_chunk_config.offset = config->input_chunk_config.offset;
	cmd.input_chunk_config.channels = config->input_chunk_config.channels;
	cmd.input_chunk_config.bits = config->input_chunk_config.bits;
	cmd.input_chunk_config.sample_rate = config->input_chunk_config.sample_rate;
	cmd.input_chunk_config.format = config->input_chunk_config.format;
	cmd.input_chunk_config.is_interleave = config->input_chunk_config.is_interleave;

	// Output Chunk Config
	cmd.output_chunk_config.offset = config->output_chunk_config.offset;
	cmd.output_chunk_config.channels = config->output_chunk_config.channels;
	cmd.output_chunk_config.bits = config->output_chunk_config.bits;
	cmd.output_chunk_config.sample_rate = config->output_chunk_config.sample_rate;
	cmd.output_chunk_config.format = config->output_chunk_config.format;
	cmd.output_chunk_config.is_interleave = config->output_chunk_config.is_interleave;

	for (int i = 0; i < config->batch_count; i++) {
		aoc_ready_fence = iif_fence_fdget(config->batches[i].aoc_ready_fence);
		if (IS_ERR(aoc_ready_fence)) {
			ret = PTR_ERR(aoc_ready_fence);
			pr_err("invalid IIF fd aoc_ready_fence\n");
			goto fail;
		}

		if (aoc_ready_fence->params.fence_type != IIF_FENCE_TYPE_REUSABLE) {
			ret = -EINVAL;
			pr_err("aoc_ready_fence is not reusable\n");
			goto fail;
		}

		tpu_ready_fence = iif_fence_fdget(config->batches[i].tpu_ready_fence);
		if (IS_ERR(tpu_ready_fence)) {
			ret = PTR_ERR(tpu_ready_fence);
			pr_err("invalid IIF fd tpu_ready_fence\n");
			goto fail;
		}

		if (tpu_ready_fence->params.fence_type != IIF_FENCE_TYPE_REUSABLE) {
			ret = -EINVAL;
			pr_err("tpu_ready_fence is not reusable\n");
			goto fail;
		}

		ret = iif_fence_submit_signaler(aoc_ready_fence);
		if (ret) {
			pr_err("iif_fence_submit_signaler failed ret=%d\n", ret);
			goto fail;
		}

		ret = iif_fence_submit_waiter(tpu_ready_fence, IIF_IP_AOC);
		if (ret) {
			pr_err("iif_fence_submit_waiter failed ret=%d\n", ret);
			goto fail;
		}

		cmd.batches[i].aoc_ready_fence = aoc_ready_fence->id;
		cmd.batches[i].tpu_ready_fence = tpu_ready_fence->id;
		cmd.batches[i].offset = config->batches[i].offset;

		entry->aoc_ready_fence[i] = aoc_ready_fence;
		entry->tpu_ready_fence[i] = tpu_ready_fence;
		entry->iif_pair_count = i + 1;

		aoc_ready_fence = NULL;
		tpu_ready_fence = NULL;
	}

	ret = aoc_service_write(entry->service, (uint8_t *)&cmd, sizeof(cmd), should_block);
	if (ret < 0)
		goto fail;
	if (ret != sizeof(cmd)) {
		ret = -EIO;
		goto fail;
	}
	ret = aoc_service_read(entry->service, (uint8_t *)&cmd, sizeof(cmd), should_block);
	if (ret < 0)
		goto fail;
	if (ret != sizeof(cmd)) {
		ret = -EIO;
		goto fail;
	}
	mutex_unlock(&entry->lock);
	return cmd.parent.reply;

fail:
	pr_err("%s failed ret=%d\n", __func__, ret);
	for (int i = 0; i < entry->iif_pair_count; i++)
		iif_fence_signal_with_status(entry->aoc_ready_fence[i], ret);
	if (tpu_ready_fence && !IS_ERR(tpu_ready_fence))
		iif_fence_put(tpu_ready_fence);
	if (aoc_ready_fence && !IS_ERR(aoc_ready_fence))
		iif_fence_put(aoc_ready_fence);
	mutex_unlock(&entry->lock);

	acd_tpu_offload_cleanup(file, entry);
	return ret;
}

static int acd_handle_tpu_offload_start(struct acd_device_entry *entry, struct file *file,
					struct aoc_tpu_offload_start *start)
{
	int ret = 0;
	bool should_block = ((file->f_flags & O_NONBLOCK) == 0);
	struct CMD_AUDIO_INPUT_TPU_OFFLOAD_START cmd = {0};

	if (entry->tpu_offload_id != start->tpu_offload_id)
		return -EINVAL;

	AocCmdHdrSet(&(cmd.parent), CMD_AUDIO_INPUT_TPU_OFFLOAD_START_ID, sizeof(cmd));
	cmd.tpu_offload_id = start->tpu_offload_id;
	cmd.feature = start->feature;
	ret = aoc_service_write(entry->service, (uint8_t *)&cmd, sizeof(cmd), should_block);
	if (ret == sizeof(cmd))
		ret = aoc_service_read(entry->service, (uint8_t *)&cmd, sizeof(cmd), should_block);
	if (ret < 0)
		return ret;
	if (ret != sizeof(cmd))
		return -EIO;
	return cmd.parent.reply;
}

static int acd_handle_tpu_offload_stop(struct acd_device_entry *entry, struct file *file,
				       struct aoc_tpu_offload_stop *stop)
{
	if (entry->tpu_offload_id != stop->tpu_offload_id)
		return -EINVAL;

	return acd_tpu_offload_cleanup(file, entry);
}
#endif

static long acd_unlocked_ioctl(struct file *file, unsigned int cmd,
			       unsigned long arg)
{
	struct acd_device_entry *entry = file->private_data;
	long rv = 0;

	if (!entry)
		return -ENODEV;

	switch (cmd) {
#if AOC_USE_TPU_OFFLOAD
	case AOC_IOCTL_TPU_OFFLOAD_CONFIG: {
		struct aoc_tpu_offload_config config;

		if (copy_from_user(&config, (void __user *)arg, sizeof(config)) == 0)
			rv = acd_handle_tpu_offload_config(entry, file, &config);
		else
			rv = -EFAULT;
	}
	break;
	case AOC_IOCTL_TPU_OFFLOAD_START: {
		struct aoc_tpu_offload_start start;

		if (copy_from_user(&start, (void __user *)arg, sizeof(start)) == 0)
			rv = acd_handle_tpu_offload_start(entry, file, &start);
		else
			rv = -EFAULT;
	}
	break;
	case AOC_IOCTL_TPU_OFFLOAD_STOP: {
		struct aoc_tpu_offload_stop stop;

		if (copy_from_user(&stop, (void __user *)arg, sizeof(stop)) == 0)
			rv = acd_handle_tpu_offload_stop(entry, file, &stop);
		else
			rv = -EFAULT;
	}
	break;
#endif
	default:
		rv = -ENOTTY;
		break;
	}

	return rv;
}

static ssize_t acd_read(struct file *file, char __user *buf, size_t count,
			loff_t *off)
{
	struct acd_device_entry *entry = file->private_data;
	char *buffer;
	size_t leftover;
	ssize_t retval = 0;
	bool should_block = ((file->f_flags & O_NONBLOCK) == 0);

	if (!entry)
		return -ENODEV;

	buffer = kmalloc(count, GFP_KERNEL);
	if (!buffer)
		return -ENOMEM;

	retval =
		aoc_service_read(entry->service, buffer, count, should_block);
	if (retval >= 0) {
		leftover = copy_to_user(buf, buffer, retval);
		retval = retval - leftover;
	}

	kfree(buffer);
	return retval;
}

static ssize_t acd_write(struct file *file, const char __user *buf,
			 size_t count, loff_t *off)
{
	struct acd_device_entry *entry = file->private_data;
	bool should_block = ((file->f_flags & O_NONBLOCK) == 0);
	char *buffer;
	size_t leftover;
	ssize_t retval = 0;

	if (!entry)
		return -ENODEV;

	buffer = kmalloc(count, GFP_KERNEL);
	if (!buffer)
		return -ENOMEM;

	leftover = copy_from_user(buffer, buf, count);
	if (leftover == 0) {
		retval = aoc_service_write(entry->service, buffer, count,
					   should_block);
	} else {
		retval = -EFAULT;
	}

	kfree(buffer);
	return retval;
}

static unsigned int acd_poll(struct file *file, poll_table *wait)
{
	unsigned int mask = 0;
	struct acd_device_entry *entry = file->private_data;
	struct aoc_service_dev *service;
	bool acd_device_dead;

	acd_device_dead = entry->service->dead;

	if (acd_device_dead)
		return mask | POLLIN | POLLRDNORM | POLLOUT | POLLWRNORM;

	service = entry->service;
	poll_wait(file, aoc_service_get_read_queue(service), wait);
	poll_wait(file, aoc_service_get_write_queue(service), wait);
	aoc_service_set_read_blocked(service);
	aoc_service_set_write_blocked(service);
	if (aoc_service_can_read(service))
		mask |= POLLIN | POLLRDNORM;
	if (aoc_service_can_write(service))
		mask |= POLLOUT | POLLWRNORM;

	return mask;
}

static int acd_probe(struct aoc_service_dev *dev)
{
	int ret;

	pr_debug("probe service with name %s\n", dev_name(&dev->dev));
	ret = create_character_device(dev);

	return ret;
}

static int acd_remove(struct aoc_service_dev *dev)
{
	struct acd_device_entry *entry;
	struct acd_device_entry *tmp;

	mutex_lock(&acd_devices_lock);
	list_for_each_entry_safe(entry, tmp, &acd_devices_list, list) {
		if (entry->acd_device->parent == &dev->dev) {
			pr_debug("remove service with name %s\n",
				 dev_name(&dev->dev));

			list_del_init(&entry->list);
			put_device(&entry->service->dev);
			device_destroy(acd_class, entry->acd_device->devt);
			kref_put(&entry->refcount, acd_device_entry_release);
			break;
		}
	}
	acd_next_minor = 0;
	mutex_unlock(&acd_devices_lock);

	return 0;
}

static void cleanup_resources(void)
{
	aoc_driver_unregister(&aoc_char_driver);

	if (acd_class) {
		class_destroy(acd_class);
		acd_class = NULL;
	}

	if (acd_major >= 0) {
		unregister_chrdev(acd_major, ACD_CHARDEV_NAME);
		acd_major = -1;
	}
}

static int __init acd_init(void)
{
	pr_debug("driver init\n");

	acd_major = register_chrdev(0, ACD_CHARDEV_NAME, &fops);
	if (acd_major < 0) {
		pr_err("Failed to register character major number\n");
		goto fail;
	}

	acd_major_dev = MKDEV(acd_major, 0);

	acd_class = class_create(ACD_CHARDEV_NAME);
	if (!acd_class) {
		pr_err("Failed to create class\n");
		goto fail;
	}

	acd_class->devnode = acd_devnode;

	aoc_driver_register(&aoc_char_driver);

	return 0;

fail:
	cleanup_resources();
	return -1;
}

static void __exit acd_exit(void)
{
	pr_debug("driver exit\n");

	cleanup_resources();
}

module_init(acd_init);
module_exit(acd_exit);

MODULE_DESCRIPTION("Google AOC character device driver");
MODULE_LICENSE("GPL v2");
MODULE_IMPORT_NS("DMA_BUF");
