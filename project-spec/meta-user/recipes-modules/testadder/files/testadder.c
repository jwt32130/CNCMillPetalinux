/*  testadder.c - The simplest kernel module.

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
    ("testadder - simple module that adds 2 numbers with interrupt");

#define DRIVER_NAME "testadder"


struct driver_data {
	struct class *testadder_class;
	/*1 device in this driver*/
	struct device *dev;
};
struct driver_data drv_data;
struct testadder_local {
	int irq;
	unsigned long mem_start;
	unsigned long mem_end;
	void __iomem *base_addr;
	unsigned int *reg1;
	unsigned int *reg2;
	unsigned int *reg3;
	unsigned int *reg4;
};
ssize_t var1_show(struct device *dev, struct device_attribute *attr, char *buf) {
	struct testadder_local *lp = dev_get_drvdata(dev->parent);
	return sprintf(buf, "%d\n", *lp->reg1);
}
ssize_t var1_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count) {
	unsigned int regvalue;
	int ret;
	struct testadder_local *lp = dev_get_drvdata(dev->parent);
	ret = kstrtouint(buf, 0, &regvalue);
	if(ret)
		return ret;

	*lp->reg1 = regvalue;
	
	return count;
}
ssize_t var2_show(struct device *dev, struct device_attribute *attr, char *buf) {
	struct testadder_local *lp = dev_get_drvdata(dev->parent);
	return sprintf(buf, "%d\n", *lp->reg2);
}
ssize_t var2_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count) {
	unsigned int regvalue;
	int ret;
	struct testadder_local *lp = dev_get_drvdata(dev->parent);
	ret = kstrtouint(buf, 0, &regvalue);
	if(ret)
		return ret;

	*lp->reg2 = regvalue;
	
	return count;
}
ssize_t ctrl_show(struct device *dev, struct device_attribute *attr, char *buf) {
	struct testadder_local *lp = dev_get_drvdata(dev->parent);
	return sprintf(buf, "%d\n", *lp->reg3);
}
ssize_t ctrl_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count) {
	struct testadder_local *lp = dev_get_drvdata(dev->parent);
	if(sysfs_streq(buf,"1")) {
		//perform addition
		printk("start add\n");
		*lp->reg3 = 0x00000001;
	} else if(sysfs_streq(buf,"2")) {
		//clear int
		printk("clear int\n");
		*lp->reg3 = 0x00000002;
	} else {
		printk("ctrl failed\n");
		return -EINVAL;
	}
	return count;
}
ssize_t val_show(struct device *dev, struct device_attribute *attr, char *buf) {
	struct testadder_local *lp = dev_get_drvdata(dev->parent);
	return sprintf(buf, "%d\n", *lp->reg4);
}

static DEVICE_ATTR_RW(var1);
static DEVICE_ATTR_RW(var2);
static DEVICE_ATTR_RW(ctrl);
static DEVICE_ATTR_RO(val);
static struct attribute *testadder_attrs[] = {
	&dev_attr_var1.attr,
	&dev_attr_var2.attr,
	&dev_attr_ctrl.attr,
	&dev_attr_val.attr,
	NULL
};
static struct attribute_group testadder_attr_group = {
	.attrs = testadder_attrs
};
static const struct attribute_group *testadder_attr_groups[] = {
	&testadder_attr_group,
	NULL
};
static irqreturn_t testadder_irq(int irq, void *lp)
{
	unsigned int status;
	struct testadder_local *lp_tmp = (struct testadder_local *)lp;
	printk("testadder: interrupt called\n");
	status = *(lp_tmp->reg3);
	if(status & 0x00000002) {
		*(lp_tmp->reg3) = 0x00000002;
		printk("testadder: interrupt cleared\n");
	}
	return IRQ_HANDLED;
}

static int testadder_probe(struct platform_device *pdev)
{
	struct resource *r_irq; /* Interrupt resources */
	struct resource *r_mem; /* IO mem resources */
	struct device *dev = &pdev->dev;
	struct testadder_local *lp = NULL;

	int rc = 0;
	dev_info(dev, "Device Tree Probing get mem location\n");
	/* Get iospace for the device */
	r_mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!r_mem) {
		dev_err(dev, "invalid address\n");
		return -ENODEV;
	}
	dev_info(dev, "alloc local driver mem\n");
	lp = (struct testadder_local *) kmalloc(sizeof(struct testadder_local), GFP_KERNEL);
	if (!lp) {
		dev_err(dev, "Cound not allocate testadder device\n");
		return -ENOMEM;
	}
	dev_info(dev, "store local data in parent\n");
	dev_set_drvdata(dev, lp);
	lp->mem_start = r_mem->start;
	lp->mem_end = r_mem->end;

	dev_info(dev, "request driver memory boundary\n");
	if (!request_mem_region(lp->mem_start,
				lp->mem_end - lp->mem_start + 1,
				DRIVER_NAME)) {
		dev_err(dev, "Couldn't lock memory region at %p\n",
			(void *)lp->mem_start);
		rc = -EBUSY;
		goto error1;
	}

	dev_info(dev, "MMU memory remap\n");
	lp->base_addr = ioremap(lp->mem_start, lp->mem_end - lp->mem_start + 1);
	if (!lp->base_addr) {
		dev_err(dev, "testadder: Could not allocate iomem\n");
		rc = -EIO;
		goto error2;
	}
	lp->reg1 = (unsigned int*)(lp->base_addr + 0x00);
	lp->reg2 = (unsigned int*)(lp->base_addr + 0x04);
	lp->reg3 = (unsigned int*)(lp->base_addr + 0x08);
	lp->reg4 = (unsigned int*)(lp->base_addr + 0x0C);

	/* Get IRQ for the device */
	dev_info(dev, "Device Tree Probing get IRQ location\n");
	r_irq = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (!r_irq) {
		dev_info(dev, "no IRQ found\n");
		dev_info(dev, "testadder at 0x%08x mapped to 0x%08x\n",
			(unsigned int __force)lp->mem_start,
			(unsigned int __force)lp->base_addr);
		return 0;
	}
	lp->irq = r_irq->start;
	dev_info(dev, "Request IRQ from linux\n");
	rc = request_irq(lp->irq, &testadder_irq, IRQF_TRIGGER_RISING, DRIVER_NAME, lp);
	if (rc) {
		dev_err(dev, "testmodule: Could not allocate interrupt %d.\n",
			lp->irq);
		goto error3;
	}

	/*populate sysfs with device info*/
	dev_info(dev, "Make class device TestAdder1\n");
	drv_data.dev = device_create_with_groups(drv_data.testadder_class, dev, 0, lp, testadder_attr_groups, "TestAdder1");
	if(IS_ERR(drv_data.dev)) {
		dev_err(dev, "error in device_create\n");
		rc = PTR_ERR(drv_data.dev);
	}

	dev_info(dev,"testadder at 0x%08x mapped to 0x%08x, irq=%d\n",
		(unsigned int __force)lp->mem_start,
		(unsigned int __force)lp->base_addr,
		lp->irq);
	return 0;

error3:
	free_irq(lp->irq, lp);
error2:
	release_mem_region(lp->mem_start, lp->mem_end - lp->mem_start + 1);
error1:
	kfree(lp);
	dev_set_drvdata(dev, NULL);
	return rc;
}

static int testadder_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct testadder_local *lp = dev_get_drvdata(dev);
	// should destroy if devt is used
	device_unregister(drv_data.dev);
	free_irq(lp->irq, lp);
	iounmap(lp->base_addr);
	release_mem_region(lp->mem_start, lp->mem_end - lp->mem_start + 1);
	kfree(lp);
	dev_set_drvdata(dev, NULL);
	return 0;
}

#ifdef CONFIG_OF
static struct of_device_id testadder_of_match[] = {
	{ .compatible = "jacob,testadder", },
	{ /* end of list */ },
};
MODULE_DEVICE_TABLE(of, testadder_of_match);
#else
# define testadder_of_match
#endif


static struct platform_driver testadder_driver = {
	.driver = {
		.name = DRIVER_NAME,
		.owner = THIS_MODULE,
		.of_match_table	= testadder_of_match,
	},
	.probe		= testadder_probe,
	.remove		= testadder_remove,
};

static int __init testadder_init(void)
{
	int ret;
	printk("testadder: driver init.\n");

	printk("testadder: init create sysfs class\n");
	drv_data.testadder_class = class_create(THIS_MODULE, "testadder_class");
	if(IS_ERR(drv_data.testadder_class)) {
		pr_err("class creation failed\n");
		ret = PTR_ERR(drv_data.testadder_class);
		return ret;
	}

	ret = platform_driver_register(&testadder_driver);

	printk("testadder: platform driver registered\n");

	return ret;
}


static void __exit testadder_exit(void)
{
	platform_driver_unregister(&testadder_driver);
	class_destroy(drv_data.testadder_class);
	printk(KERN_ALERT "testadder: unregister\n");
	printk(KERN_ALERT "testadder: destroy class files\n");
}

module_init(testadder_init);
module_exit(testadder_exit);
