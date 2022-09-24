#include <evl/file.h>
#include <evl/flag.h>
#include <linux/module.h>

#include "buffer.h"
#include "dma.h"
#include "i2s.h"

#define CHAR_DEVICE_NAME "i2s-thing"
#define DEVICE_CLASS_NAME "i2s-thing-class"
#define DRIVER_NAME "i2s-thing-io"

struct i2st_settings
{
	unsigned int period_frames;
	unsigned int period_count;
};

// IOCTL
#define I2S_THING_IOCTL	0x10
#define I2S_THING_START 	_IOW(I2S_THING_IOCTL, 0x01, struct i2st_settings)
#define I2S_THING_STOP  	 _IO(I2S_THING_IOCTL, 0x02)
#define I2S_THING_RESTART	 _IO(I2S_THING_IOCTL, 0x03)

static int major_number = 0;
static struct class *device_class = NULL;
static struct device *char_device = NULL;
static struct device *i2s_device = NULL;
static struct evl_file efile;
static struct evl_flag eflag;

static struct dma_chan *dma_chan_tx = NULL;
static struct dma_chan *dma_chan_rx = NULL;
static struct regmap *regmap = NULL;

static struct i2st_buffer tx_buffer;
static struct i2st_buffer rx_buffer;

static bool running = false;

static int i2st_probe(struct platform_device *pdev);
static int i2st_remove(struct platform_device *pdev);

static int i2st_open(struct inode *inode, struct file *file);
static int i2st_release(struct inode *inode, struct file *file);
static ssize_t i2st_read(struct file *file, char __user *buf, size_t size);
static ssize_t i2st_write(struct file *file, const char __user *buf, size_t size);
static long i2st_ioctl(struct file *file, unsigned int cmd, unsigned long arg);

static struct file_operations i2st_fops =
{
	.owner = THIS_MODULE,
	.open = i2st_open,
	.release = i2st_release,
	.oob_read = i2st_read,
	.oob_write = i2st_write,
	.oob_ioctl = i2st_ioctl,
};

static const struct of_device_id i2st_of_match[] = {
	{ .compatible = "brcm,bcm2835-i2s", },
	{ },
};

static struct platform_driver i2st_driver = {
	.probe = i2st_probe,
	.remove = i2st_remove,
	.driver = {
		.name = DRIVER_NAME,
		.of_match_table = i2st_of_match,
	},
};

static int i2st_stop(void);

static void dma_tx_complete(void *param)
{
	i2st_buffer_dma_complete(&tx_buffer);
}

static void dma_rx_complete(void *param)
{
	i2st_buffer_dma_complete(&rx_buffer);
}

static int __init i2st_init(void)
{
	int ret;

	memset(&efile, 0, sizeof(efile));
	memset(&eflag, 0, sizeof(eflag));
	memset(&tx_buffer, 0, sizeof(tx_buffer));
	memset(&rx_buffer, 0, sizeof(rx_buffer));

	// Create character device
	major_number = register_chrdev(0, CHAR_DEVICE_NAME, &i2st_fops);
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
	i2st_driver.driver.owner = THIS_MODULE;
	i2st_driver.driver.bus = &platform_bus_type;

	ret = driver_register(&i2st_driver.driver);
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

static void __exit i2st_exit(void)
{
	driver_unregister(&i2st_driver.driver);
	device_destroy(device_class, MKDEV(major_number, 0));
	unregister_chrdev(major_number, CHAR_DEVICE_NAME);
	class_destroy(device_class);
}

static int i2st_probe(struct platform_device *pdev)
{
	int ret;

	ret = i2st_i2s_init_regmap(pdev, &regmap);
	if (ret) { return ret; }

	ret = i2st_dma_create_channel(&pdev->dev, &dma_chan_tx, "tx", FIFO_A);
	if (ret) { return ret; }
	ret = i2st_dma_create_channel(&pdev->dev, &dma_chan_rx, "rx", FIFO_A);
	if (ret) { return ret; }

	i2s_device = &pdev->dev;

	return 0;
}

static int i2st_remove(struct platform_device *pdev)
{
	i2st_dma_close_channel(dma_chan_tx);
	i2st_dma_close_channel(dma_chan_rx);

	memset(&tx_buffer, 0, sizeof(tx_buffer));
	memset(&rx_buffer, 0, sizeof(rx_buffer));

	regmap = NULL;
	dma_chan_tx = NULL;
	dma_chan_rx = NULL;

	return 0;
}

static int i2st_open(struct inode *inode, struct file *file)
{
	int ret;

	// Only allow one access at a time
	if (efile.filp)
	{
		return -EBUSY;
	}

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

	return 0;
}

static int i2st_release(struct inode *inode, struct file *file)
{
	int ret;

	if (running)
	{
		ret = i2st_stop();
		if (ret) return ret;
	}

	if (efile.filp)
	{
		evl_destroy_flag(&eflag);
		evl_release_file(&efile);
	}

	memset(&efile, 0, sizeof(efile));
	memset(&eflag, 0, sizeof(eflag));

	return 0;
}

static ssize_t i2st_read(struct file *file, char __user *buf, size_t size)
{
	return i2st_buffer_read(&rx_buffer, buf, size);
}

static ssize_t i2st_write(struct file *file, const char __user *buf, size_t size)
{
	return i2st_buffer_write(&tx_buffer, buf, size);
}

static int i2st_start(unsigned int period_frames, unsigned int period_count)
{
	int ret;

	// Don't allow calling multiple times
	if (running)
	{
		return -EBUSY;
	}

	// Init buffers
	ret = i2st_buffer_init(i2s_device, &tx_buffer, period_frames, period_count);
	if (ret) { return ret; }
	ret = i2st_buffer_init(i2s_device, &rx_buffer, period_frames, period_count);
	if (ret)
	{
		i2st_buffer_release(i2s_device, &tx_buffer);
		return ret;
	}

	// Enable DMA
	ret = i2st_dma_start(dma_chan_tx, tx_buffer.dma_address, tx_buffer.size, tx_buffer.period_size, DMA_MEM_TO_DEV, dma_tx_complete);
	if (ret)
	{
		i2st_buffer_release(i2s_device, &tx_buffer);
		i2st_buffer_release(i2s_device, &rx_buffer);
		return ret;
	}

	ret = i2st_dma_start(dma_chan_rx, rx_buffer.dma_address, rx_buffer.size, rx_buffer.period_size, DMA_DEV_TO_MEM, dma_rx_complete);
	if (ret)
	{
		i2st_dma_stop(dma_chan_tx);
		i2st_buffer_release(i2s_device, &tx_buffer);
		i2st_buffer_release(i2s_device, &rx_buffer);
		return ret;
	}

	// Start I2S
	i2st_i2s_start(regmap);

	running = true;
	return 0;
}

static int i2st_stop(void)
{
	if (!running)
	{
		return -EINVAL;
	}

	i2st_i2s_stop(regmap);

	if (dma_chan_tx) i2st_dma_stop(dma_chan_tx);
	if (dma_chan_rx) i2st_dma_stop(dma_chan_rx);

	i2st_buffer_release(i2s_device, &tx_buffer);
	i2st_buffer_release(i2s_device, &rx_buffer);

	running = false;
	return 0;
}

static int i2st_restart(void)
{
	if (!running)
	{
		return -EINVAL;
	}

	i2st_i2s_stop(regmap);
	i2st_buffer_reset(&tx_buffer);
	i2st_buffer_reset(&rx_buffer);
	i2st_i2s_start(regmap);

	return 0;
}

static long i2st_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	switch (cmd)
	{
	case I2S_THING_START:
		struct i2st_settings __user *settings = (struct i2st_settings __user *)arg;
		return i2st_start(settings->period_frames, settings->period_count);

	case I2S_THING_STOP:
		return i2st_stop();

	case I2S_THING_RESTART:
		return i2st_restart();

	default:
		return -EINVAL;
	}

	return 0;
}

module_init(i2st_init);
module_exit(i2st_exit);

MODULE_DEVICE_TABLE(of, i2st_of_match);

MODULE_ALIAS("platform:i2s-thing");
MODULE_DESCRIPTION("I2S driver thing");
MODULE_AUTHOR("Jussi Viiri <ilmai@iki.fi>");
MODULE_LICENSE("GPL");
