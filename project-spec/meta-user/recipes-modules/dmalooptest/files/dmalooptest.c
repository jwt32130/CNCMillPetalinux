/*  dmalooptest.c - The simplest kernel module.

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

#include <linux/delay.h>
#include <linux/dmaengine.h>
// #include <linux/init.h>
#include <linux/kthread.h>
// #include <linux/module.h>
#include <linux/of_dma.h>
#include <linux/platform_device.h>
#include <linux/random.h>
// #include <linux/slab.h>
#include <linux/wait.h>
#include <linux/sched/task.h>
#include <linux/dma/xilinx_dma.h>

#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>

/* Standard module information, edit as appropriate */
MODULE_LICENSE("GPL");
MODULE_AUTHOR
    ("Jacob Turner");
MODULE_DESCRIPTION
    ("dmalooptest - register mode dma test");

#define DRIVER_NAME "dmalooptest"

struct dma_chan *tx_chan, *rx_chan;
struct completion tx_completion;
struct completion rx_completion;

void tx_transfer_complete(void *cmp) {
	printk("JT: tx callback\n");
	complete(cmp);
	return;
}
void rx_transfer_complete(void *cmp) {
	printk("JT: rx callback\n");
	complete(cmp);
	return;
}

struct dmalooptest_local {
	int irq;
	unsigned long mem_start;
	unsigned long mem_end;
	void __iomem *base_addr;
};

static irqreturn_t dmalooptest_irq(int irq, void *lp)
{
	printk("dmalooptest interrupt\n");
	return IRQ_HANDLED;
}

static int dmalooptest_probe(struct platform_device *pdev)
{
	// struct dma_chan *chan, *rx_chan;
	int err;
	enum dma_ctrl_flags tx_flags = DMA_CTRL_ACK | DMA_PREP_INTERRUPT;
	u8 *tx_buf;
	dma_addr_t tx_dma_addr;
	struct dma_async_tx_descriptor *txd = NULL;
	dma_cookie_t txcookie;
	unsigned long txtimeout = msecs_to_jiffies(3000);
	enum dma_status txstatus;

	enum dma_ctrl_flags rx_flags = DMA_CTRL_ACK | DMA_PREP_INTERRUPT;
	u8 *rx_buf;
	dma_addr_t rx_dma_addr;
	struct dma_async_tx_descriptor *rxd = NULL;
	dma_cookie_t rxcookie;
	unsigned long rxtimeout = msecs_to_jiffies(3000);
	enum dma_status rxstatus;

	u8 align = 0;

	printk("dmatest: request tx channel from dma engine/device tree\n");
	tx_chan = dma_request_chan(&pdev->dev, "axidma0");
	if (IS_ERR(tx_chan)) {
		err = PTR_ERR(tx_chan);
		if (err != -EPROBE_DEFER)
			pr_err("xilinx_dmatest: No Tx channel\n");
		return err;
	}

	printk("dmatest: request rx channel from dma engine/device tree\n");
	rx_chan = dma_request_chan(&pdev->dev, "axidma1");
	if (IS_ERR(rx_chan)) {
		err = PTR_ERR(rx_chan);
		if (err != -EPROBE_DEFER)
			pr_err("xilinx_dmatest: No Rx channel\n");
		goto free_tx;
	}

	printk("dmatest: create tx contiguous memory for DMA\n");
	tx_buf = kmalloc(1024, GFP_KERNEL);
	tx_buf[0] = 0x11;
	tx_buf[1] = 0x22;
	tx_buf[2] = 0x33;
	tx_buf[3] = 0x44;
	printk("dmatest: create rx contiguous memory for DMA\n");
	rx_buf = kzalloc(1024, GFP_KERNEL);
	// should really check for alignment - for now just assuming it is aligned
	printk("dmatest: tx virt addr: %x\n", tx_buf);
	printk("dmatest: rx virt addr: %x\n", rx_buf);
	printk("dmatest: tx phys addr: %x\n", virt_to_phys((void*)tx_buf));
	printk("dmatest: rx phys addr: %x\n", virt_to_phys((void*)rx_buf));
	printk("dmatest: map tx buf for DMA use - unclaim from processor\n");
	tx_dma_addr = dma_map_single(tx_chan->device->dev, tx_buf, 1024, DMA_TO_DEVICE );
	printk("dmatest: map rx buf for DMA use - unclaim from processor\n");
	rx_dma_addr = dma_map_single(rx_chan->device->dev, rx_buf, 1024, DMA_FROM_DEVICE );

	printk("dmatest: tx prep channel for single transfer\n");
	txd = dmaengine_prep_slave_single(tx_chan, tx_dma_addr, 1024, DMA_MEM_TO_DEV, tx_flags);
	txd->callback = tx_transfer_complete;
	txd->callback_param = &tx_completion;
	printk("dmatest: rx prep channel for single transfer\n");
	rxd = dmaengine_prep_slave_single(rx_chan, rx_dma_addr, 1024, DMA_DEV_TO_MEM, rx_flags);
	rxd->callback = rx_transfer_complete;
	rxd->callback_param = &rx_completion;

	printk("dmatest: tx submit transfer\n");
	txcookie = dmaengine_submit(txd);
	printk("dmatest: rx submit transfer\n");
	rxcookie = dmaengine_submit(rxd);
	printk("dmatest: tx init completion struct\n");
	init_completion(&tx_completion);
	printk("dmatest: rx init completion struct\n");
	init_completion(&rx_completion);
	printk("dmatest: rx start\n");
	printk("dmatest: tx start\n");
	dma_async_issue_pending(rx_chan);
	dma_async_issue_pending(tx_chan);




	txtimeout = wait_for_completion_timeout(&tx_completion, txtimeout);
	txstatus = dma_async_is_tx_complete(tx_chan, txcookie, NULL, NULL);
	if(txtimeout == 0) {
		printk("jt: tx timeout\n");
	} else if(txstatus != DMA_COMPLETE) {
		printk("jt: tx dma error\n");
	} else {
		printk("dmatest: tx complete\n");
	}
	rxtimeout = wait_for_completion_timeout(&rx_completion, rxtimeout);
	rxstatus = dma_async_is_tx_complete(rx_chan, rxcookie, NULL, NULL);
	if(rxtimeout == 0) {
		printk("jt: rx timeout\n");
	} else if(rxstatus != DMA_COMPLETE) {
		printk("jt: rx dma error\n");
	} else {
		printk("dmatest: rx complete\n");
	}
	printk("dmatest: tx unmap memory and reclaim it for processor use\n");
	dma_unmap_single(tx_chan->device->dev, tx_dma_addr, 1024, DMA_TO_DEVICE);
	printk("dmatest: rx unmap memory and reclaim it for processor use\n");
	dma_unmap_single(rx_chan->device->dev, rx_dma_addr, 1024, DMA_FROM_DEVICE);
	printk("dmatest: %x%x%x%x\n", rx_buf[0], rx_buf[1],rx_buf[2],rx_buf[3]);
	kfree(tx_buf);
	kfree(rx_buf);

	return 0;

free_rx:
	dma_release_channel(rx_chan);
free_tx:
	dma_release_channel(tx_chan);

	return err;
}

static int dmalooptest_remove(struct platform_device *pdev)
{
	struct dmatest_chan *dtc, *_dtc;
	struct dma_chan *chan;

	dma_release_channel(rx_chan);
	dma_release_channel(tx_chan);
	return 0;
}

#ifdef CONFIG_OF
static struct of_device_id dmalooptest_of_match[] = {
	{ .compatible = "jacob,dmalooptest", },
	{ /* end of list */ },
};
MODULE_DEVICE_TABLE(of, dmalooptest_of_match);
#else
# define dmalooptest_of_match
#endif


static struct platform_driver dmalooptest_driver = {
	.driver = {
		.name = DRIVER_NAME,
		.owner = THIS_MODULE,
		.of_match_table	= dmalooptest_of_match,
	},
	.probe		= dmalooptest_probe,
	.remove		= dmalooptest_remove,
};

static int __init dmalooptest_init(void)
{
	printk("dmalooptest: register driver\n");

	return platform_driver_register(&dmalooptest_driver);
}


static void __exit dmalooptest_exit(void)
{
	platform_driver_unregister(&dmalooptest_driver);
	printk(KERN_ALERT "dmalooptest: driver unregistered\n");
}

module_init(dmalooptest_init);
module_exit(dmalooptest_exit);
