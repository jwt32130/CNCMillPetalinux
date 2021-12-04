/*  axidmaout.c - The simplest kernel module.

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
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/kthread.h>
#include <linux/sched/task.h>
#include <linux/wait.h>
#include <linux/delay.h>
#include <linux/poll.h>
#include <linux/dmaengine.h>
#include <linux/dma/xilinx_dma.h>
#include <linux/dma-mapping.h>
#include <linux/of_dma.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>

/* Standard module information, edit as appropriate */
MODULE_LICENSE("GPL");
MODULE_AUTHOR
    ("Jacob Turner");
MODULE_DESCRIPTION
    ("axidmaout - register mode dma out to");

#define DRIVER_NAME "axidmaout"

/*axi dma needs to be 16word aligned (16*4bytes min transfer) (0x40)*/
/*axi dma curdesc reg    31---------6|5|4|3|2|1|0 */
/************************************|5-0 bits are ignored */
//#define DMA_BUFF_SIZE 1024
#define DMA_BUFF_SIZE (2*1024)
#define DMA_U8_SIZE (DMA_BUFF_SIZE * sizeof(u64))

DECLARE_WAIT_QUEUE_HEAD(wait_queue_poll_data);

struct driver_data {
	dev_t device_num_base;
	struct class *class_dma;
	struct device *device_dma;
};
struct driver_data drv_data;


struct device_data {
	/*protected by mutex*/
	struct mutex buff_ctrl_mutex;
	u8 *buffers[2];
	u8 *tmpbuffers[2];
	int pos;
	/*end protected by mutex*/
	/*protected by spinlock betweent tasklet and driver write*/
	spinlock_t buff_ctrl_lock;
	int next_buff_count;
	int DMA_active_buffer;
	int Fill_buffer;
	/*end protect by spinlock*/

	struct cdev _cdev;
	dev_t dev_num;
	struct dma_chan *tx_chan;
	struct dma_chan *rx_chan;


	dma_addr_t tx_dma_addr[2];
	struct dma_async_tx_descriptor *txd;
	dma_cookie_t txcookie;
	enum dma_status txstatus;

	dma_addr_t rx_dma_addr[2];
	struct dma_async_tx_descriptor *rxd;
	dma_cookie_t rxcookie;
	enum dma_status rxstatus;
};

int dma_open (struct inode *pInode, struct file *pFile);
int dma_release (struct inode *pInode, struct file *pFile);
ssize_t dma_write (struct file *pFile, const char __user *pBuff, size_t count, loff_t *pPos);
__poll_t dma_poll (struct file *pFile, struct poll_table_struct *wait);

void setup_DMA_transfer(struct device_data *dat, int buff_number, int count);

ssize_t write_dma_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count) {
	struct device_data *dev_data = dev_get_drvdata(dev);
	int value;
	int ret;
	int bufnumber;
	int buf_count = -EINVAL; /*not valid cmd*/
	bool dma_data = false;
	ret = kstrtoint(buf, 0, &value);
	/*check for -EINVAL or -ERANGE*/
	if(ret) return ret;

	if(value == 1) {
		/*write current dma buffer*/
		mutex_lock(&dev_data->buff_ctrl_mutex);

		if(dev_data->pos) {
			/*copy succeded*/
			/*lock so we can decide if we need to queue next dma buf or the callback will*/
			spin_lock_bh(&dev_data->buff_ctrl_lock);
			bufnumber = dev_data->Fill_buffer;
			if(dev_data->Fill_buffer != dev_data->DMA_active_buffer)
			{
				buf_count = dev_data->pos;
				if(dev_data->DMA_active_buffer == -1) {
					/*no pending DMA's to queue up a transfer, claim transfer now*/
					dma_data = true;
					dev_data->DMA_active_buffer = dev_data->Fill_buffer;
				}
				dev_data->next_buff_count = buf_count;
				/*correct for next buffer*/
				dev_data->Fill_buffer++;
				if(dev_data->Fill_buffer > 1) dev_data->Fill_buffer = 0;
				dev_data->pos = 0;
			}
			else {
				buf_count = -EAGAIN; /*try again, buffers busy*/
			}
			spin_unlock_bh(&dev_data->buff_ctrl_lock);
		} else {
			/*no data to transfer*/
			buf_count = -EINVAL;
		}
		mutex_unlock(&dev_data->buff_ctrl_mutex);

		if(dma_data == true) {
			//send data function bufnumber
			setup_DMA_transfer(dev_data, bufnumber, buf_count);
		}
		
	}
	return buf_count;
}
static DEVICE_ATTR_WO(write_dma);
static struct attribute *dma_attrs[] = {
	&dev_attr_write_dma.attr,
	NULL
};
static struct attribute_group dma_attr_group = {
	.attrs = dma_attrs
};
static const struct attribute_group *dma_attr_groups[] = {
	&dma_attr_group,
	NULL
};
struct file_operations dma_fops = {
	.open = dma_open,
	.release = dma_release,
	.write = dma_write,
	.poll = dma_poll,
	.owner = THIS_MODULE
};
void tx_transfer_complete(void *dat) {
	/*tasklet - do not sleep*/
	enum dma_status txstatus;
	struct device_data *dev_data = (struct device_data *)dat;
	int bufnumber = 0;
	bool dma_data = false;
	int buf_count = 0;
	int i;

	txstatus = dma_async_is_tx_complete(dev_data->tx_chan, dev_data->txcookie, NULL, NULL);
	/* DMA_COMPLETE;
	   DMA_IN_PROGRESS;
	   DMA_PAUSED;
	   DMA_ERROR;
	   DMA_OUT_OF_ORDER;*/
	if(txstatus != DMA_COMPLETE) {
		pr_err("axidma: tx dma error\n");
		return;
	} else {
		pr_info("axidmaout: tx complete\n");

		/* dma_unmap_single(dev_data->tx_chan->device->dev, dev_data->tx_dma_addr, DMA_U8_SIZE, DMA_TO_DEVICE);*/
		
		spin_lock_bh(&dev_data->buff_ctrl_lock);
		if(dev_data->Fill_buffer == dev_data->DMA_active_buffer) {
			/*another full buffer is ready and the DMA should have been skipped at fill so claim the transfer now*/
			dev_data->DMA_active_buffer++;
			if(dev_data->DMA_active_buffer > 1) dev_data->DMA_active_buffer = 0;
			bufnumber = dev_data->DMA_active_buffer;
			dma_data = true;
			buf_count = dev_data->next_buff_count;
		} else {
			/* no buf ready so set active buffer to -1 */
			dev_data->DMA_active_buffer = -1;
		}
		spin_unlock_bh(&dev_data->buff_ctrl_lock);
		if(dma_data == true) {
			pr_info("axidmaout: setup from callback\n");
			setup_DMA_transfer(dev_data, bufnumber, buf_count);
		}
		wake_up(&wait_queue_poll_data);
	}
	pr_info("axidmaout: tx exit\n");
	return;
}
void rx_transfer_complete(void *cmp) {
	printk("axidmaout: rx callback\n");
	// complete(cmp);
	return;
}
int dma_open (struct inode *pInode, struct file *pFile) {
	struct device_data *dev_data;
	pr_info("axidmaout: Device file opened\n");
	dev_data = container_of(pInode->i_cdev, struct device_data, _cdev);
	pFile->private_data = dev_data;
	/*should really only allow 1 file open here at a time*/
	return 0;
}
int dma_release (struct inode *pInode, struct file *pFile) {
	pr_info("axidmaout: Device file released\n");
	return 0;
}
void setup_DMA_transfer(struct device_data *dat, int buff_number, int count) {
	struct dma_chan *tx_chan = dat->tx_chan;

	enum dma_ctrl_flags tx_flags = DMA_CTRL_ACK | DMA_PREP_INTERRUPT;
	u8 *tx_buf = dat->buffers[buff_number];
	dma_addr_t *tx_dma_addr = &dat->tx_dma_addr[buff_number];
	struct dma_async_tx_descriptor *txd = dat->txd;
	dma_cookie_t *txcookie = &dat->txcookie;
	// unsigned long txtimeout = msecs_to_jiffies(3000);
	// enum dma_status txstatus;

	struct dma_chan *rx_chan = dat->rx_chan;

	enum dma_ctrl_flags rx_flags = DMA_CTRL_ACK | DMA_PREP_INTERRUPT;
	u8 *rx_buf = dat->tmpbuffers[buff_number];
	dma_addr_t *rx_dma_addr = &dat->rx_dma_addr[buff_number];
	struct dma_async_tx_descriptor *rxd = dat->rxd;
	dma_cookie_t *rxcookie = &dat->rxcookie;
	// unsigned long rxtimeout = msecs_to_jiffies(3000);
	// enum dma_status rxstatus;
	int aligned_count;

	//  u8 align = 0;
	/* align to 64bit boundary */
	// printk("axidmaout: DMA setup: buffer:count %d:%d\n", buff_number, count);
	aligned_count = (count >> 6) << 6;
	pr_info("axidmaout: DMA setup: buffer:alignedcount %d:%d\n", buff_number, aligned_count);
	if((count != aligned_count) && (aligned_count < DMA_U8_SIZE)) {
		pr_err("axidmaout: unaligned buffer: check this later\n");
		aligned_count += 64;
		while(count < aligned_count) {
			tx_buf[count++] = 0x00;
		}
	}
	/*
	 *tx_dma_addr = dma_map_single(tx_chan->device->dev, tx_buf, DMA_U8_SIZE, DMA_TO_DEVICE );
	 *rx_dma_addr = dma_map_single(rx_chan->device->dev, rx_buf, DMA_U8_SIZE, DMA_FROM_DEVICE );
	*/

	txd = dmaengine_prep_slave_single(tx_chan, *tx_dma_addr, aligned_count, DMA_MEM_TO_DEV, tx_flags);
	txd->callback = tx_transfer_complete;
	txd->callback_param = dat;
	rxd = dmaengine_prep_slave_single(rx_chan, *rx_dma_addr, aligned_count, DMA_DEV_TO_MEM, rx_flags);
	// rxd->callback = rx_transfer_complete;
	// rxd->callback_param = dat;

	*txcookie = dmaengine_submit(txd);
	*rxcookie = dmaengine_submit(rxd);

	dma_async_issue_pending(rx_chan);
	dma_async_issue_pending(tx_chan);

	pr_info("axidma: dmastarted\n");
	return;

}
ssize_t dma_write (struct file *pFile, const char __user *pBuff, size_t count, loff_t *pPos) {
	struct device_data *dev_data;
	int bufnumber;
	int dma_active_buf;
	bool dma_data = false;
	pr_info("axidma: device write called: %d\n", count);
	dev_data = (struct device_data*)pFile->private_data;
	mutex_lock(&dev_data->buff_ctrl_mutex);

	/*probably not necessary to lock these to read them*/
	spin_lock_bh(&dev_data->buff_ctrl_lock);
	bufnumber = dev_data->Fill_buffer;
	dma_active_buf = dev_data->DMA_active_buffer;
	spin_unlock_bh(&dev_data->buff_ctrl_lock);

	/*if fill buff is not equal to DMA active then we are not waiting for a DMA to finish*/
	if(bufnumber != dma_active_buf ) {
		/*free to wrte*/
		if(dev_data->pos + count > DMA_U8_SIZE) {
			count = DMA_U8_SIZE - dev_data->pos;
		}
		if(count == 0) {
			count = -ENOMEM;
			goto unlock;
		}
		/*copy data from user to buffer*/
		/* should probably do this outside of lock*/
		if(copy_from_user(&dev_data->buffers[bufnumber][dev_data->pos], pBuff, count)) {
			pr_err("axidmaout: copy from user fail\n");
			count = -EFAULT;
			goto unlock;
		}
		/*copy succeded*/
		dev_data->pos += count;

		/*lock so we can decide if we need to queue next dma buf or the callback will*/
		spin_lock_bh(&dev_data->buff_ctrl_lock);
		if(dev_data->pos >= DMA_U8_SIZE) {
			/*send data*/
			if(dev_data->DMA_active_buffer == -1) {
				/*no pending DMA's to queue up a transfer, claim transfer now*/
				dma_data = true;
				dev_data->DMA_active_buffer = bufnumber;
			}
			dev_data->next_buff_count = DMA_U8_SIZE;
			/*correct for next buffer*/
			dev_data->Fill_buffer++;
			if(dev_data->Fill_buffer > 1) dev_data->Fill_buffer = 0;
			dev_data->pos = 0;
		}
		spin_unlock_bh(&dev_data->buff_ctrl_lock);
	} else {
		/*skip write*/
		count = 0;
	}
unlock:
	mutex_unlock(&dev_data->buff_ctrl_mutex);

	if(dma_data == true) {
		//send data function bufnumber
		setup_DMA_transfer(dev_data, bufnumber, DMA_U8_SIZE);
	}
	return count;
}
__poll_t dma_poll (struct file *pFile, struct poll_table_struct *wait) {
	struct device_data *dev_data;
	__poll_t mask = 0;
	bool can_write = false;
	dev_data = (struct device_data*)pFile->private_data;
	poll_wait(pFile, &wait_queue_poll_data, wait);

	spin_lock_bh(&dev_data->buff_ctrl_lock);
	can_write = (dev_data->Fill_buffer == dev_data->DMA_active_buffer) ? false : true;
	spin_unlock_bh(&dev_data->buff_ctrl_lock);

	if(can_write) {
		mask |= (POLLOUT | POLLWRNORM);
	}
	return mask;
}


// int thread_func(void *data) {

// 	/*ensure all previous writes are complete*/
// 	smp_rmb();
// 	while(!kthread_should_stop()) {
// 		printk("axidmaout: in thread loop\n");
// 		ssleep(10);
// 	}
// 	printk("axidmaout: thread exiting\n");
// 	return 0;
// }


static int axidmaout_probe(struct platform_device *pdev)
{
	int ret;
	struct device_data *dev_data;
	struct device *dev = &pdev->dev;

	pr_info("axidma: probe enter\n");

	dev_data = devm_kzalloc(dev, sizeof(*dev_data), GFP_KERNEL);
    if(!dev_data) {
        dev_info(dev, "Cannot allocate memory\n");
        return -ENOMEM;
    }
	/*pdev->dev.driver_data = dev_data;*/
    dev_set_drvdata(dev, dev_data);



	mutex_init(&dev_data->buff_ctrl_mutex);
	mutex_lock(&dev_data->buff_ctrl_mutex);

	spin_lock_init(&dev_data->buff_ctrl_lock);
	dev_data->DMA_active_buffer = -1;
	dev_data->Fill_buffer = 0;

	dev_data->pos = 0;
	dev_data->buffers[0] = dma_alloc_coherent(dev, (DMA_U8_SIZE), &dev_data->tx_dma_addr[0], GFP_KERNEL);
	dev_data->buffers[1] = dma_alloc_coherent(dev, (DMA_U8_SIZE), &dev_data->tx_dma_addr[1], GFP_KERNEL);
	dev_data->tmpbuffers[0] = dma_alloc_coherent(dev, (DMA_U8_SIZE), &dev_data->rx_dma_addr[0], GFP_KERNEL);
	dev_data->tmpbuffers[1] = dma_alloc_coherent(dev, (DMA_U8_SIZE), &dev_data->rx_dma_addr[1], GFP_KERNEL);
	// dev_data->buffers[0] = devm_kmalloc(dev, (DMA_U8_SIZE), GFP_KERNEL);
	// dev_data->buffers[1] = devm_kmalloc(dev, (DMA_U8_SIZE), GFP_KERNEL);
	// dev_data->tmpbuffers[0] = devm_kmalloc(dev, (DMA_U8_SIZE), GFP_KERNEL);
	// dev_data->tmpbuffers[1] = devm_kmalloc(dev, (DMA_U8_SIZE), GFP_KERNEL);
	mutex_unlock(&dev_data->buff_ctrl_mutex);

	if(!dev_data->buffers[0] || 
		!dev_data->buffers[1] ||
		!dev_data->tmpbuffers[0] ||
		!dev_data->tmpbuffers[1] ) 
	{
		if(dev_data->buffers[0]) dma_free_coherent(dev, (DMA_U8_SIZE), dev_data->buffers[0], dev_data->tx_dma_addr[0]);
		if(dev_data->buffers[1]) dma_free_coherent(dev, (DMA_U8_SIZE),  dev_data->buffers[1], dev_data->tx_dma_addr[1]);
		if(dev_data->tmpbuffers[0]) dma_free_coherent(dev, (DMA_U8_SIZE),  dev_data->tmpbuffers[0], dev_data->rx_dma_addr[0]);
		if(dev_data->tmpbuffers[1]) dma_free_coherent(dev, (DMA_U8_SIZE),  dev_data->tmpbuffers[1], dev_data->rx_dma_addr[1]);

		dev_info(dev, "Cannot allocate memory\n");
		return -ENOMEM;
	}
	

	pr_info("dmatest: tx virt addr: %x:%x\n", dev_data->buffers[0], dev_data->buffers[1]);
	pr_info("dmatest: rx virt addr: %x:%x\n", dev_data->tmpbuffers[0], dev_data->tmpbuffers[1]);
	pr_info("dmatest: tx phys addr: %x:%x\n", virt_to_phys((void*)dev_data->buffers[0]), virt_to_phys((void*)dev_data->buffers[1]));
	pr_info("dmatest: rx phys addr: %x:%x\n", virt_to_phys((void*)dev_data->tmpbuffers[0]), virt_to_phys((void*)dev_data->tmpbuffers[1]));

	/*do cdev init and cdev add*/
    cdev_init(&dev_data->_cdev, &dma_fops);
    dev_data->_cdev.owner=THIS_MODULE;
	dev_data->dev_num = drv_data.device_num_base;
    ret = cdev_add(&dev_data->_cdev, dev_data->dev_num,1);
    if(ret < 0){
        dev_err(dev, "Cdev add failed\n");
		goto release_mem;
    }

	// drv_data.device_dma = device_create(drv_data.class_dma, NULL, dev_data->dev_num, dev, "dma-0");
	drv_data.device_dma = device_create_with_groups(drv_data.class_dma, dev, dev_data->dev_num, dev_data, dma_attr_groups, "dma-%d", MINOR(dev_data->dev_num));
	if(IS_ERR(drv_data.device_dma)) {
		pr_err("device create failed\n");
		ret = PTR_ERR(drv_data.device_dma);
		goto dev_del;
	}

	printk("axidma: request tx channel from dma engine/device tree\n");
	dev_data->tx_chan = dma_request_chan(&pdev->dev, "axidma0");
	if (IS_ERR(dev_data->tx_chan)) {
		ret = PTR_ERR(dev_data->tx_chan);
		if (ret != -EPROBE_DEFER)
			pr_err("xilinx_dmatest: No Tx channel\n");
		goto dev_destroy;
	}

	printk("axidma: request rx channel from dma engine/device tree\n");
	dev_data->rx_chan = dma_request_chan(&pdev->dev, "axidma1");
	if (IS_ERR(dev_data->rx_chan)) {
		ret = PTR_ERR(dev_data->rx_chan);
		if (ret != -EPROBE_DEFER)
			pr_err("xilinx_dmatest: No Rx channel\n");
		goto tx_dma_release;
	}

	return 0;
	
tx_dma_release:
	dma_release_channel(dev_data->tx_chan);
dev_destroy:
	device_destroy(drv_data.class_dma, dev_data->dev_num);
dev_del:
	cdev_del(&dev_data->_cdev);
release_mem:
	dma_free_coherent(dev, (DMA_U8_SIZE), dev_data->buffers[0], dev_data->tx_dma_addr[0]);
	dma_free_coherent(dev, (DMA_U8_SIZE),  dev_data->buffers[1], dev_data->tx_dma_addr[1]);
	dma_free_coherent(dev, (DMA_U8_SIZE),  dev_data->tmpbuffers[0], dev_data->rx_dma_addr[0]);
	dma_free_coherent(dev, (DMA_U8_SIZE),  dev_data->tmpbuffers[1], dev_data->rx_dma_addr[1]);
	pr_alert("axidma: probe failed\n");
	return ret;

}

static int axidmaout_remove(struct platform_device *pdev)
{
	struct device_data *dev_data = dev_get_drvdata(&pdev->dev);
	struct device *dev = &pdev->dev;

	pr_info("axidma: removed enter\n");
	dma_release_channel(dev_data->rx_chan);
	dma_release_channel(dev_data->tx_chan);

	device_destroy(drv_data.class_dma, dev_data->dev_num);

    cdev_del(&dev_data->_cdev);

	dma_free_coherent(dev, (DMA_U8_SIZE), dev_data->buffers[0], dev_data->tx_dma_addr[0]);
	dma_free_coherent(dev, (DMA_U8_SIZE),  dev_data->buffers[1], dev_data->tx_dma_addr[1]);
	dma_free_coherent(dev, (DMA_U8_SIZE),  dev_data->tmpbuffers[0], dev_data->rx_dma_addr[0]);
	dma_free_coherent(dev, (DMA_U8_SIZE),  dev_data->tmpbuffers[1], dev_data->rx_dma_addr[1]);

	pr_info("axidma: removed exit\n");
	return 0;
}

#ifdef CONFIG_OF
static struct of_device_id axidmaout_of_match[] = {
	{ .compatible = "jacob,axidmaout", },
	{ /* end of list */ },
};
MODULE_DEVICE_TABLE(of, axidmaout_of_match);
#else
# define axidmaout_of_match
#endif


static struct platform_driver axidmaout_driver = {
	.driver = {
		.name = DRIVER_NAME,
		.owner = THIS_MODULE,
		.of_match_table	= axidmaout_of_match,
	},
	.probe		= axidmaout_probe,
	.remove		= axidmaout_remove,
};

static int __init axidmaout_init(void)
{
	int ret;
	pr_info("axidma: driver init\n");

	ret = alloc_chrdev_region(&drv_data.device_num_base, 0, 1, "axidmaout");
	if(ret < 0) {
		goto out;
	}
	pr_info("Device number %d-%d\n", MAJOR(drv_data.device_num_base), MINOR(drv_data.device_num_base));
	drv_data.class_dma = class_create(THIS_MODULE, "dma_class");
	if(IS_ERR(drv_data.class_dma)) {
		pr_err("class creation failed\n");
		ret = PTR_ERR(drv_data.class_dma);
		goto del_chrdev;
	}
	pr_info("axidma: driver exit\n");
	return platform_driver_register(&axidmaout_driver);

del_chrdev:
	unregister_chrdev_region(drv_data.device_num_base, 1);
out:
	pr_alert("axidma: init failed\n");
	return ret;
}


static void __exit axidmaout_exit(void)
{
	platform_driver_unregister(&axidmaout_driver);
	class_destroy(drv_data.class_dma);
	unregister_chrdev_region(drv_data.device_num_base, 1);
	pr_alert("AXI DMA out driver exit\n");
}

module_init(axidmaout_init);
module_exit(axidmaout_exit);
