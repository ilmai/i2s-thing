#include <evl/file.h>
#include <evl/flag.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/memory.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#include "dma.h"
#include "i2s.h"

#define CHAR_DEVICE_NAME "i2s-thing"
#define DEVICE_CLASS_NAME "i2s-thing-class"
#define DRIVER_NAME "i2s-thing-io"

struct i2s_thing_settings
{
	unsigned int buffer_size;
};

// IOCTL
#define I2S_THING_IOCTL	0x10
#define I2S_THING_START _IOW(I2S_THING_IOCTL, 0x01, struct i2s_thing_settings)

static int major_number;
static struct class *device_class = NULL;
static struct device *char_device = NULL;
static struct device *i2s_device = NULL;
static struct evl_file efile;
static struct evl_flag eflag;
static bool char_device_opened = false;

struct dma_chan *dma_chan_tx;
struct dma_chan *dma_chan_rx;
struct regmap	*regmap;

struct i2s_thing_dma_buffer tx_buffer;
struct i2s_thing_dma_buffer rx_buffer;

static int i2s_thing_probe(struct platform_device *pdev);
static int i2s_thing_remove(struct platform_device *pdev);

static int i2s_thing_open(struct inode *inode, struct file *file);
static int i2s_thing_release(struct inode *inode, struct file *file);
static ssize_t i2s_thing_read(struct file *file, char __user *buf, size_t size);
static ssize_t i2s_thing_write(struct file *file, const char __user *buf, size_t size);
static long i2s_thing_ioctl(struct file *file, unsigned int cmd, unsigned long arg);

static struct file_operations i2s_thing_fops =
{
	.owner = THIS_MODULE,
	.open = i2s_thing_open,
	.release = i2s_thing_release,
	.oob_read = i2s_thing_read,
	.oob_write = i2s_thing_write,
	.unlocked_ioctl = i2s_thing_ioctl,
};

static const struct of_device_id i2s_thing_of_match[] = {
	{ .compatible = "brcm,bcm2835-i2s", },
	{ },
};

static struct platform_driver i2s_thing_driver = {
	.probe = i2s_thing_probe,
	.remove = i2s_thing_remove,
	.driver = {
		.name = DRIVER_NAME,
		.of_match_table = i2s_thing_of_match,
	},
};

static void dma_tx_complete(void *param)
{
}

static void dma_rx_complete(void *param)
{
}

static int __init i2s_thing_init(void)
{
	int ret;

	// Create character device
	major_number = register_chrdev(0, CHAR_DEVICE_NAME, &i2s_thing_fops);
	if (major_number < 0)
	{
		printk(KERN_ALERT "Failed to register major number\n");
		return major_number;
	}

	device_class = class_create(THIS_MODULE, DEVICE_CLASS_NAME);
	if (IS_ERR(device_class))
	{
		unregister_chrdev(major_number, CHAR_DEVICE_NAME);
		printk(KERN_ALERT "Failed to create device class\n");
		return PTR_ERR(device_class);
	}

	char_device = device_create(device_class, NULL, MKDEV(major_number, 0), NULL, CHAR_DEVICE_NAME);
	if (IS_ERR(char_device))
	{
		class_destroy(device_class);
		unregister_chrdev(major_number, CHAR_DEVICE_NAME);
		printk(KERN_ALERT "Failed to create device\n");
		return PTR_ERR(char_device);
	}

	// Create I2S driver
	i2s_thing_driver.driver.owner = THIS_MODULE;
	i2s_thing_driver.driver.bus = &platform_bus_type;

	ret = driver_register(&i2s_thing_driver.driver);
	if (ret)
	{
		device_destroy(device_class, MKDEV(major_number, 0));
		class_unregister(device_class);
		class_destroy(device_class);
		unregister_chrdev(major_number, CHAR_DEVICE_NAME);

		printk(KERN_ALERT "Failed to register driver\n");
		return ret;
	}

	return 0;
}

static void __exit i2s_thing_exit(void)
{
	i2s_thing_dma_release(i2s_device, &tx_buffer);
	i2s_thing_dma_release(i2s_device, &rx_buffer);

	driver_unregister(&i2s_thing_driver.driver);
	device_destroy(device_class, MKDEV(major_number, 0));
	unregister_chrdev(major_number, CHAR_DEVICE_NAME);
	class_destroy(device_class);
}

static int i2s_thing_probe(struct platform_device *pdev)
{
	int ret;

	ret = i2s_thing_i2s_init_regmap(pdev, &regmap);
	if (ret) { return ret; }

	i2s_thing_dma_create_channel(&pdev->dev, &dma_chan_tx, "tx", FIFO_A);
	i2s_thing_dma_create_channel(&pdev->dev, &dma_chan_rx, "rx", FIFO_A);

	i2s_device = &pdev->dev;

	return 0;
}

static int i2s_thing_remove(struct platform_device *pdev)
{
	i2s_thing_dma_release(i2s_device, &tx_buffer);
	i2s_thing_dma_release(i2s_device, &rx_buffer);
	i2s_thing_dma_close_channel(dma_chan_tx);
	i2s_thing_dma_close_channel(dma_chan_rx);

	return 0;
}

static int i2s_thing_open(struct inode *inode, struct file *file)
{
	int ret;
	
	// Only allow one access at a time
	if (char_device_opened)
	{
		return -EBUSY;
	}

	memset(&tx_buffer, 0, sizeof(tx_buffer));
	memset(&rx_buffer, 0, sizeof(rx_buffer));

	ret = evl_open_file(&efile, file);
	if (ret)
	{
		printk(KERN_ALERT "evl_open_file failed");
		return ret;
	}

	ret = stream_open(inode, file);
	if (ret)
	{
		evl_release_file(&efile);

		printk(KERN_ALERT "stream_open failed");
		return ret;
	}

	evl_init_flag(&eflag);

	char_device_opened = true;
	return 0;
}

static int i2s_thing_release(struct inode *inode, struct file *file)
{
	i2s_thing_i2s_stop(regmap);

	evl_destroy_flag(&eflag);
	evl_release_file(&efile);

	char_device_opened = false;
	return 0;
}

static ssize_t i2s_thing_read(struct file *file, char __user *buf, size_t size)
{
	unsigned long error_count;

	if (size != rx_buffer.size)
	{
		printk(KERN_ALERT "Read size %lu doesn't match buffer size %lu", size, rx_buffer.size);
		return -EINVAL;
	}

	error_count = raw_copy_to_user(buf, rx_buffer.ptr, size);
	if (error_count != 0)
	{
		printk(KERN_ALERT "Reading from buffer failed");
		return -EFAULT;
	}

	return size;
}

static ssize_t i2s_thing_write(struct file *file, const char __user *buf, size_t size)
{
	unsigned long error_count;

	if (size != tx_buffer.size)
	{
		printk(KERN_ALERT "Write size %lu doesn't match buffer size %lu", size, tx_buffer.size);
		return -EINVAL;
	}

	error_count = raw_copy_from_user(tx_buffer.ptr, buf, size);
	if (error_count != 0)
	{
		printk(KERN_ALERT "Writing to buffer failed");
		return -EFAULT;
	}

	return size;
}

static int i2s_thing_start(unsigned int buffer_size)
{
	int ret;
	size_t buffer_size_bytes;

	buffer_size_bytes = buffer_size * sizeof(s16);

	// Allocate DMA buffers
	ret = i2s_thing_dma_alloc(i2s_device, &tx_buffer, buffer_size_bytes);
	if (ret)
	{
		printk(KERN_ALERT "Failed allocating DMA buffer");
		return ret;
	}
	
	ret = i2s_thing_dma_alloc(i2s_device, &rx_buffer, buffer_size_bytes);
	if (ret)
	{
		printk(KERN_ALERT "Failed allocating DMA buffer");
		return ret;
	}

	// Enable DMA
	i2s_thing_dma_start(dma_chan_tx, tx_buffer.dma_address, buffer_size, DMA_MEM_TO_DEV, dma_tx_complete);
	i2s_thing_dma_start(dma_chan_rx, rx_buffer.dma_address, buffer_size, DMA_DEV_TO_MEM, dma_rx_complete);

	i2s_thing_i2s_start(regmap);

	return 0;
}

static long i2s_thing_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	switch (cmd)
	{
	case I2S_THING_START:
		struct i2s_thing_settings __user *settings = (struct i2s_thing_settings __user *)arg;
		return i2s_thing_start(settings->buffer_size);

	default:
		return -EINVAL;
	}

	return 0;
}

module_init(i2s_thing_init);
module_exit(i2s_thing_exit);

MODULE_DEVICE_TABLE(of, i2s_thing_of_match);

MODULE_ALIAS("platform:i2s-thing");
MODULE_DESCRIPTION("I2S driver thing");
MODULE_AUTHOR("Jussi Viiri <ilmai@iki.fi>");
MODULE_LICENSE("GPL");
