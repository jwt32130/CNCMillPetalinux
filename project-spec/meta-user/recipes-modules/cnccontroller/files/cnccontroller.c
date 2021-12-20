/*  cnccontroller.c - The simplest kernel module.

* Copyright (C) 2013 - 2016 Xilinx, Inc
*
*   This program is free software; you can redistribute it and/or modify
*   it under the terms of the GNU General Public License as published by
*   the Free Software Foundation; either version 2 of the License, or
*   (at your option) any later version.

*   This program is distributed in the hope that it will be useful,
*   but WITHOUT ANY WARRANTY; without even the implied warranty of
*   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*   GNU General Public License for more details.
*
*   You should have received a copy of the GNU General Public License along
*   with this program. If not, see <http://www.gnu.org/licenses/>.

*/
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/interrupt.h>

#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>

/* Standard module information, edit as appropriate */
MODULE_LICENSE("GPL");
MODULE_AUTHOR
    ("Jacob Turner");
MODULE_DESCRIPTION
    ("cnccontroller - splits an axi stream of data to separate controllers and handles feedback");

#define DRIVER_NAME "cnccontroller"


struct driver_data {
	struct class *cncC_class;
	struct device *dev;
};

struct driver_data drv_data;

struct cnccontroller_local {
	int irq;
	unsigned long mem_start;
	unsigned long mem_end;
	void __iomem *base_addr;
	unsigned int *ctrl_reg;
	unsigned int *status_reg;
	unsigned int *opcount_reg;
};
ssize_t reset_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count) {
	unsigned int regvalue;
	int ret;
	struct cnccontroller_local *lp = dev_get_drvdata(dev->parent);
	if(sysfs_streq(buf,"1")) {
		printk("resetting cncController\n");
		*(lp->ctrl_reg) = 0x00000005;
	} else {
		printk("clearing reset cncController\n");
		*(lp->ctrl_reg) = 0x00000000;
	}
	
	return count;
}
static DEVICE_ATTR_WO(reset);
// static DEVICE_ATTR_WO(stop_and_clear);
// static DEVICE_ATTR_WO(reset_stop);
// static DEVICE_ATTR_WO(enable_reverse);
static struct attribute *cncC_attrs[] = {
	&dev_attr_reset.attr,
	NULL
};
static struct attribute_group cncC_attr_group = {
	.attrs = cncC_attrs
};
static const struct attribute_group *cncC_attr_groups[] = {
	&cncC_attr_group,
	NULL
};
static irqreturn_t cnccontroller_irq(int irq, void *lp)
{
	printk("cnccontroller interrupt\n");
	return IRQ_HANDLED;
}

static int cnccontroller_probe(struct platform_device *pdev)
{
	struct resource *r_irq; /* Interrupt resources */
	struct resource *r_mem; /* IO mem resources */
	struct device *dev = &pdev->dev;
	struct cnccontroller_local *lp = NULL;

	int rc = 0;
	dev_info(dev, "Device Tree Probing\n");
	/* Get iospace for the device */
	r_mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!r_mem) {
		dev_err(dev, "invalid address\n");
		return -ENODEV;
	}
	lp = (struct cnccontroller_local *) kmalloc(sizeof(struct cnccontroller_local), GFP_KERNEL);
	if (!lp) {
		dev_err(dev, "Cound not allocate cnccontroller device\n");
		return -ENOMEM;
	}
	dev_set_drvdata(dev, lp);
	lp->mem_start = r_mem->start;
	lp->mem_end = r_mem->end;

	if (!request_mem_region(lp->mem_start,
				lp->mem_end - lp->mem_start + 1,
				DRIVER_NAME)) {
		dev_err(dev, "Couldn't lock memory region at %p\n",
			(void *)lp->mem_start);
		rc = -EBUSY;
		goto error1;
	}

	lp->base_addr = ioremap(lp->mem_start, lp->mem_end - lp->mem_start + 1);
	if (!lp->base_addr) {
		dev_err(dev, "cnccontroller: Could not allocate iomem\n");
		rc = -EIO;
		goto error2;
	}
	lp->ctrl_reg = (unsigned int*)(lp->base_addr + 0x00);
	lp->status_reg = (unsigned int*)(lp->base_addr + 0x04);
	lp->opcount_reg = (unsigned int*)(lp->base_addr + 0x08);

	/* Get IRQ for the device */
	// r_irq = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	// if (!r_irq) {
		// dev_info(dev, "no IRQ found\n");
		// dev_info(dev, "cnccontroller at 0x%08x mapped to 0x%08x\n",
			// (unsigned int __force)lp->mem_start,
			// (unsigned int __force)lp->base_addr);
		// return 0;
	// }
	// lp->irq = r_irq->start;
	// rc = request_irq(lp->irq, &cnccontroller_irq, 0, DRIVER_NAME, lp);
	// if (rc) {
		// dev_err(dev, "testmodule: Could not allocate interrupt %d.\n",
			// lp->irq);
		// goto error3;
	// }
	dev_info(dev, "Make class device cncController\n");
	drv_data.dev = device_create_with_groups(drv_data.cncC_class, dev, 0, lp, cncC_attr_groups, "cncC0");
	if(IS_ERR(drv_data.dev)) {
		dev_err(dev, "error in device create\n");
		rc = PTR_ERR(drv_data.cncC_class);
		goto error4;
	}

	dev_info(dev,"cnccontroller at 0x%08x mapped to 0x%08x, irq=%d\n",
		(unsigned int __force)lp->mem_start,
		(unsigned int __force)lp->base_addr,
		lp->irq);
	return 0;
error4:
error3:
	// free_irq(lp->irq, lp);
error2:
	release_mem_region(lp->mem_start, lp->mem_end - lp->mem_start + 1);
error1:
	kfree(lp);
	dev_set_drvdata(dev, NULL);
	return rc;
}

static int cnccontroller_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct cnccontroller_local *lp = dev_get_drvdata(dev);
	device_unregister(drv_data.dev);
	// free_irq(lp->irq, lp);
	iounmap(lp->base_addr);
	release_mem_region(lp->mem_start, lp->mem_end - lp->mem_start + 1);
	kfree(lp);
	dev_set_drvdata(dev, NULL);
	return 0;
}

#ifdef CONFIG_OF
static struct of_device_id cnccontroller_of_match[] = {
	{ .compatible = "jacob,cnccontroller", },
	{ /* end of list */ },
};
MODULE_DEVICE_TABLE(of, cnccontroller_of_match);
#else
# define cnccontroller_of_match
#endif


static struct platform_driver cnccontroller_driver = {
	.driver = {
		.name = DRIVER_NAME,
		.owner = THIS_MODULE,
		.of_match_table	= cnccontroller_of_match,
	},
	.probe		= cnccontroller_probe,
	.remove		= cnccontroller_remove,
};

static int __init cnccontroller_init(void)
{
	int ret;
	printk("cncController: init\n");

	drv_data.cncC_class = class_create(THIS_MODULE, "cncController");
	if(IS_ERR(drv_data.cncC_class)) {
		pr_err("class creation failed\n");
		ret = PTR_ERR(drv_data.cncC_class);
		return ret;
	}


	ret = platform_driver_register(&cnccontroller_driver);
	printk("cncController: platform_driver_register\n");
	return ret;
}


static void __exit cnccontroller_exit(void)
{
	platform_driver_unregister(&cnccontroller_driver);
	class_destroy(drv_data.cncC_class);
	printk(KERN_ALERT "cncController: unreg and destroy class\n");
}

module_init(cnccontroller_init);
module_exit(cnccontroller_exit);
