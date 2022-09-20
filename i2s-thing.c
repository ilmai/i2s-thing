#include <evl/file.h>
#include <evl/flag.h>
#include <linux/dmaengine.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/regmap.h>
#include <linux/platform_device.h>

#define CHAR_DEVICE_NAME "i2s-thing"
#define DEVICE_CLASS_NAME "i2s-thing-class"
#define DRIVER_NAME "i2s-thing-io"

// Registers
#define CS_A		0x00
#define FIFO_A		0x04
#define MODE_A		0x08
#define RXC_A		0x0c
#define TXC_A		0x10
#define DREQ_A		0x14
#define INTEN_A		0x18
#define INTSTC_A	0x1c
#define GRAY		0x20

// Register fields

static int major_number;
static struct class *device_class = NULL;
static struct device *char_device = NULL;
static struct evl_file efile;
static struct evl_flag eflag;
static bool char_device_opened = false;

struct i2s_thing_device
{
	struct dma_chan *dma_chan_tx;
	struct dma_chan *dma_chan_rx;
	struct regmap	*regmap;
};

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
	.oob_ioctl = i2s_thing_ioctl,
};

static const struct of_device_id i2s_thing_of_match[] = {
	{
		.compatible = "brcm,bcm2835-i2s",
	},
	{		
	},
};

static struct platform_driver i2s_thing_driver = {
	.probe = i2s_thing_probe,
	.remove = i2s_thing_remove,
	.driver = {
		.name = DRIVER_NAME,
		.of_match_table = i2s_thing_of_match,
	},
};

static const struct regmap_config i2s_thing_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = GRAY,
	.cache_type = REGCACHE_NONE,
};

struct dma_slave_config i2s_thing_dma_config =
{
	.dst_addr_width = sizeof(u32),
	.src_addr_width = sizeof(u32),
};

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
	driver_unregister(&i2s_thing_driver.driver);
	device_destroy(device_class, MKDEV(major_number, 0));
	unregister_chrdev(major_number, CHAR_DEVICE_NAME);
	class_destroy(device_class);
}

static int config_dma_channel(struct device* dev, struct dma_chan** dma_chan, const char* name)
{
	*dma_chan = dma_request_chan(dev, name);

	if (IS_ERR(*dma_chan))
	{
		dev_err(dev, "DMA TX channel request failed\n");
		*dma_chan = NULL;
		return PTR_ERR(*dma_chan);
	}

	dmaengine_slave_config(*dma_chan, &i2s_thing_dma_config);

	return 0;
}

static int i2s_thing_probe(struct platform_device *pdev)
{
	struct i2s_thing_device *i2s_thing_dev;
	void __iomem *register_address;
	const __be32 *dma_address_be32;
	dma_addr_t dma_address;

	// Custom device data
	i2s_thing_dev = devm_kzalloc(&pdev->dev, sizeof(*i2s_thing_dev), GFP_KERNEL);
	if (!i2s_thing_dev)
	{
		return -ENOMEM;
	}

	dev_set_drvdata(&pdev->dev, i2s_thing_dev);

	// Register map
	register_address = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(register_address))
	{
		return PTR_ERR(register_address);
	}

	i2s_thing_dev->regmap = devm_regmap_init_mmio(&pdev->dev, register_address, &i2s_thing_regmap_config);
	if (IS_ERR(i2s_thing_dev->regmap))
	{
		return PTR_ERR(i2s_thing_dev->regmap);
	}

	// DMA
	dma_address_be32 = of_get_address(pdev->dev.of_node, 0, NULL, NULL);
	if (!dma_address_be32)
	{
		dev_err(&pdev->dev, "Error getting DMA address\n");
		return -EINVAL;
	}
	dma_address = be32_to_cpup(dma_address_be32);	

	i2s_thing_dma_config.src_addr = dma_address + FIFO_A;
	i2s_thing_dma_config.dst_addr = dma_address + FIFO_A;

	config_dma_channel(&pdev->dev, &i2s_thing_dev->dma_chan_tx, "tx");
	config_dma_channel(&pdev->dev, &i2s_thing_dev->dma_chan_rx, "rx");

	return 0;
}

static int i2s_thing_remove(struct platform_device *pdev)
{
	struct i2s_thing_device *i2s_thing_dev;

	i2s_thing_dev = dev_get_drvdata(&pdev->dev);
	
	dma_release_channel(i2s_thing_dev->dma_chan_tx);
	dma_release_channel(i2s_thing_dev->dma_chan_rx);

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

	ret = evl_open_file(&efile, file);
	if (ret)
	{
		return ret;
	}

	evl_init_flag(&eflag);

	char_device_opened = true;
	return 0;
}

static int i2s_thing_release(struct inode *inode, struct file *file)
{
	evl_destroy_flag(&eflag);
	evl_release_file(&efile);

	char_device_opened = false;
	return 0;
}

static ssize_t i2s_thing_read(struct file *file, char __user *buf, size_t size)
{
	return 0;
}

static ssize_t i2s_thing_write(struct file *file, const char __user *buf, size_t size)
{
	return 0;
}

static long i2s_thing_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	return 0;
}

module_init(i2s_thing_init);
module_exit(i2s_thing_exit);

MODULE_DEVICE_TABLE(of, i2s_thing_of_match);

MODULE_ALIAS("platform:i2s-thing");
MODULE_DESCRIPTION("I2S driver thing");
MODULE_AUTHOR("Jussi Viiri <ilmai@iki.fi>");
MODULE_LICENSE("GPL");
