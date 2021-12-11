/* Copyright (c) 2017-2020, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/err.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/of_address.h>
#include <linux/dma-contiguous.h>
#include <linux/cma.h>
#include "../ion.h"

#define ION_COMPAT_STR	"qcom,msm-ion"

static struct ion_device *idev;
static int num_heaps;
static struct ion_heap **heaps;

struct ion_heap_desc {
	unsigned int id;
	enum ion_heap_type type;
	const char *name;
};

static struct ion_heap_desc ion_heap_meta[] = {
	{
		.id	= ION_SYSTEM_HEAP_ID,
		.name	= ION_SYSTEM_HEAP_NAME,
	},
	{
		.id	= ION_SECURE_HEAP_ID,
		.name	= ION_SECURE_HEAP_NAME,
	},
	{
		.id	= ION_CP_MM_HEAP_ID,
		.name	= ION_MM_HEAP_NAME,
	},
	{
		.id	= ION_USER_CONTIG_HEAP_ID,
		.name	= ION_USER_CONTIG_HEAP_NAME,
	},
	{
		.id	= ION_QSECOM_HEAP_ID,
		.name	= ION_QSECOM_HEAP_NAME,
	},
	{
		.id	= ION_QSECOM_TA_HEAP_ID,
		.name	= ION_QSECOM_TA_HEAP_NAME,
	},
	{
		.id	= ION_SPSS_HEAP_ID,
		.name	= ION_SPSS_HEAP_NAME,
	},
	{
		.id	= ION_ADSP_HEAP_ID,
		.name	= ION_ADSP_HEAP_NAME,
	},
	{
		.id	= ION_SECURE_DISPLAY_HEAP_ID,
		.name	= ION_SECURE_DISPLAY_HEAP_NAME,
	},
	{
		.id     = ION_AUDIO_HEAP_ID,
		.name   = ION_AUDIO_HEAP_NAME,
	},
	{
		.id     = ION_VIDEO_HEAP_ID,
		.name   = ION_VIDEO_HEAP_NAME,
	},
	{
		.id	= ION_SECURE_CARVEOUT_HEAP_ID,
		.name	= ION_SECURE_CARVEOUT_HEAP_NAME,
	}
};

#define MAKE_HEAP_TYPE_MAPPING(h) { .name = #h, \
			.heap_type = ION_HEAP_TYPE_##h, }

static struct heap_types_info {
	const char *name;
	int heap_type;
} heap_types_info[] = {
	MAKE_HEAP_TYPE_MAPPING(SYSTEM),
	MAKE_HEAP_TYPE_MAPPING(SYSTEM_CONTIG),
	MAKE_HEAP_TYPE_MAPPING(CARVEOUT),
	MAKE_HEAP_TYPE_MAPPING(SECURE_CARVEOUT),
	MAKE_HEAP_TYPE_MAPPING(CHUNK),
	MAKE_HEAP_TYPE_MAPPING(DMA),
	MAKE_HEAP_TYPE_MAPPING(SECURE_DMA),
	MAKE_HEAP_TYPE_MAPPING(SYSTEM_SECURE),
	MAKE_HEAP_TYPE_MAPPING(HYP_CMA),
};

static int msm_ion_get_heap_type_from_dt_node(struct device_node *node,
					      int *heap_type)
{
	const char *name;
	int i, ret = -EINVAL;

	ret = of_property_read_string(node, "qcom,ion-heap-type", &name);
	if (ret)
		goto out;
	for (i = 0; i < ARRAY_SIZE(heap_types_info); ++i) {
		if (!strcmp(heap_types_info[i].name, name)) {
			*heap_type = heap_types_info[i].heap_type;
			ret = 0;
			goto out;
		}
	}
	WARN(1, "Unknown heap type: %s. You might need to update heap_types_info in %s",
	     name, __FILE__);
out:
	return ret;
}

static int msm_ion_populate_heap(struct device_node *node,
				 struct ion_platform_heap *heap)
{
	unsigned int i;
	int ret = -EINVAL, heap_type = -1;
	unsigned int len = ARRAY_SIZE(ion_heap_meta);

	for (i = 0; i < len; ++i) {
		if (ion_heap_meta[i].id == heap->id) {
			heap->name = ion_heap_meta[i].name;
			ret = msm_ion_get_heap_type_from_dt_node(
							node, &heap_type);
			if (ret)
				break;
			heap->type = heap_type;
			break;
		}
	}
	if (ret)
		pr_err("%s: Unable to populate heap, error: %d", __func__, ret);
	return ret;
}

static void free_pdata(const struct ion_platform_data *pdata)
{
	kfree(pdata->heaps);
	kfree(pdata);
}

static int msm_ion_get_heap_dt_data(struct device_node *node,
				    struct ion_platform_heap *heap)
{
	struct device_node *pnode;
	int ret = -EINVAL;

	pnode = of_parse_phandle(node, "memory-region", 0);
	if (pnode) {
		const __be32 *basep;
		u64 size = 0;
		u64 base = 0;

		basep = of_get_address(pnode,  0, &size, NULL);
		if (!basep) {
			struct device *dev = heap->priv;

			if (dev->cma_area) {
				base = cma_get_base(dev->cma_area);
				size = cma_get_size(dev->cma_area);
				ret = 0;
			} else if (dev->dma_mem) {
				base = dma_get_device_base(dev, dev->dma_mem);
				size = dma_get_size(dev->dma_mem);
				ret = 0;
			}
		} else {
			base = of_translate_address(pnode, basep);
			if (base != OF_BAD_ADDR)
				ret = 0;
		}

		if (!ret) {
			heap->base = base;
			heap->size = size;
		}
		of_node_put(pnode);
	} else {
		ret = 0;
	}

	WARN(ret, "Failed to parse DT node for heap %s\n", heap->name);
	return ret;
}

static struct ion_platform_data *msm_ion_parse_dt(struct platform_device *pdev)
{
	struct ion_platform_data *pdata = 0;
	struct ion_platform_heap *heaps = NULL;
	struct device_node *node;
	struct platform_device *new_dev = NULL;
	const struct device_node *dt_node = pdev->dev.of_node;
	const __be32 *val;
	int ret = -EINVAL;
	u32 num_heaps = 0;
	int idx = 0;

	for_each_available_child_of_node(dt_node, node)
		num_heaps++;

	if (!num_heaps)
		return ERR_PTR(-EINVAL);

	pdata = kzalloc(sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return ERR_PTR(-ENOMEM);

	heaps = kcalloc(num_heaps, sizeof(struct ion_platform_heap),
			GFP_KERNEL);
	if (!heaps) {
		kfree(pdata);
		return ERR_PTR(-ENOMEM);
	}

	pdata->heaps = heaps;
	pdata->nr = num_heaps;

	for_each_available_child_of_node(dt_node, node) {
		new_dev = of_platform_device_create(node, NULL, &pdev->dev);
		if (!new_dev) {
			pr_err("Failed to create device %s\n", node->name);
			goto free_heaps;
		}
		of_dma_configure(&new_dev->dev, node);

		pdata->heaps[idx].priv = &new_dev->dev;
		val = of_get_address(node, 0, NULL, NULL);
		if (!val) {
			pr_err("%s: Unable to find reg key", __func__);
			goto free_heaps;
		}
		pdata->heaps[idx].id = (u32)of_read_number(val, 1);

		ret = msm_ion_populate_heap(node, &pdata->heaps[idx]);
		if (ret)
			goto free_heaps;

		ret = msm_ion_get_heap_dt_data(node, &pdata->heaps[idx]);
		if (ret)
			goto free_heaps;

		++idx;
	}
	return pdata;

free_heaps:
	free_pdata(pdata);
	return ERR_PTR(ret);
}

struct ion_heap *get_ion_heap(int heap_id)
{
	int i;
	struct ion_heap *heap;

	for (i = 0; i < num_heaps; i++) {
		heap = heaps[i];
		if (heap->id == heap_id)
			return heap;
	}

	pr_err("%s: heap_id %d not found\n", __func__, heap_id);
	return NULL;
}

static int msm_ion_probe(struct platform_device *pdev)
{
	static struct ion_device *new_dev;
	struct ion_platform_data *pdata;
	int err = -1;
	int i;

	pdata = msm_ion_parse_dt(pdev);
	if (IS_ERR(pdata))
		return PTR_ERR(pdata);


	num_heaps = pdata->nr;

	heaps = kcalloc(pdata->nr, sizeof(struct ion_heap *), GFP_KERNEL);

	if (!heaps) {
		err = -ENOMEM;
		goto out;
	}

	new_dev = ion_device_create();
	if (IS_ERR_OR_NULL(new_dev)) {
		/*
		 * set this to the ERR to indicate to the clients
		 * that Ion failed to probe.
		 */
		idev = new_dev;
		err = PTR_ERR(new_dev);
		goto out;
	}

	/* create the heaps as specified in the board file */
	for (i = 0; i < num_heaps; i++) {
		struct ion_platform_heap *heap_data = &pdata->heaps[i];

		heaps[i] = ion_heap_create(heap_data);
		if (IS_ERR_OR_NULL(heaps[i])) {
			heaps[i] = 0;
			continue;
		} else {
			if (heap_data->size)
				pr_info("ION heap %s created at %pa with size %zx\n",
					heap_data->name,
					&heap_data->base,
					heap_data->size);
			else
				pr_info("ION heap %s created\n",
					heap_data->name);
		}

		ion_device_add_heap(new_dev, heaps[i]);
	}
	free_pdata(pdata);

	platform_set_drvdata(pdev, new_dev);
	/*
	 * intentionally set this at the very end to allow probes to be deferred
	 * completely until Ion is setup
	 */
	idev = new_dev;

	return 0;

out:
	kfree(heaps);
	free_pdata(pdata);
	return err;
}

static int msm_ion_pm_freeze(struct device *dev)
{
	struct ion_device *ion_dev = dev_get_drvdata(dev);
	struct ion_heap *heap;
	int ret;

	plist_for_each_entry(heap, &ion_dev->heaps, node) {
		if (heap->ops->pm.freeze) {
			ret = heap->ops->pm.freeze(heap);
			if (ret) {
				dev_err(dev, "%s freeze callback failed\n",
					heap->name);
				goto undo;
			}
		}
	}

	return 0;

undo:
	list_for_each_entry_continue_reverse(heap, &ion_dev->heaps.node_list,
					     node.node_list)
		if (heap->ops->pm.restore)
			heap->ops->pm.restore(heap);

	return ret;
}

static int msm_ion_pm_restore(struct device *dev)
{
	struct ion_device *ion_dev = dev_get_drvdata(dev);
	struct ion_heap *heap;
	int ret = 0;

	plist_for_each_entry(heap, &ion_dev->heaps, node) {
		int rc;

		if (heap->ops->pm.restore) {
			rc = heap->ops->pm.restore(heap);
			if (rc) {
				dev_err(dev, "%s restore callback failed.\n",
					heap->name);
				if (!ret)
					ret = rc;
			}
		}
	}

	return ret;
}

static const struct dev_pm_ops msm_ion_pm_ops = {
	.freeze_late = msm_ion_pm_freeze,
	.restore_early = msm_ion_pm_restore,
};

static const struct of_device_id msm_ion_match_table[] = {
	{.compatible = ION_COMPAT_STR},
	{},
};

static struct platform_driver msm_ion_driver = {
	.probe = msm_ion_probe,
	.driver = {
		.name = "ion-msm",
		.of_match_table = msm_ion_match_table,
		.pm = &msm_ion_pm_ops,
	},
};

static int __init msm_ion_init(void)
{
	return platform_driver_register(&msm_ion_driver);
}

subsys_initcall(msm_ion_init);
