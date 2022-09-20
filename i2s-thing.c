#include <evl/file.h>
#include <evl/flag.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
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
#define CS_EN		BIT(0)
#define CS_RXON		BIT(1)
#define CS_TXON		BIT(2)
#define CS_TXCLR	BIT(3)
#define CS_RXCLR	BIT(4)
#define CS_DMAEN	BIT(9)
#define CS_STBY		BIT(25)

#define MODE_FSLEN(val)	(val)
#define MODE_FLEN(val)	((val - 1) << 10)
#define MODE_FSI		BIT(20)
#define MODE_FSM		BIT(21)
#define MODE_CLKI		BIT(22)
#define MODE_CLKM		BIT(23)
#define MODE_FTXP		BIT(24)
#define MODE_FRXP		BIT(25)

#define XC_CH2WID(val)	(val)
#define XC_CH2POS(val)	(val << 4)
#define XC_CH2EN		BIT(14)
#define XC_CH2WEX		BIT(15)
#define XC_CH1WID(val)	(val << 16)
#define XC_CH1POS(val)	(val << 20)
#define XC_CH1EN		BIT(30)
#define XC_CH1WEX		BIT(31)

#define DREQ_RX_REQ(val)	(val)
#define DREQ_TX_REQ(val)	(val << 8)
#define DREQ_RX_PANIC(val)	(val << 16)
#define DREQ_TX_PANIC(val)	(val << 24)

struct i2s_thing_settings
{
	unsigned int buffer_size;
};

struct i2s_thing_dma_buffer
{
	size_t size;
	size_t length;
	s16* ptr;
	dma_addr_t dma_address;
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

static struct dma_slave_config i2s_thing_dma_config =
{
	.src_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES,
	.dst_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES,
	.src_maxburst = 2,
	.dst_maxburst = 2,
};

static void dma_tx_complete(void *param)
{
}

static void dma_rx_complete(void *param)
{
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

static int alloc_dma_buffer(struct i2s_thing_dma_buffer* buffer, unsigned int size)
{
	void* buffer_ptr;
	
	buffer_ptr = dma_alloc_coherent(i2s_device, PAGE_ALIGN(size), &buffer->dma_address, GFP_KERNEL);
	if (!buffer_ptr)
	{
		return -ENOMEM;
	}

	buffer->size = size;
	buffer->length = size / sizeof(s16);
	buffer->ptr = (s16*)buffer_ptr;

	return 0;
}

static int start_dma(struct dma_chan *dma_chan, dma_addr_t dma_address, unsigned int buffer_size, enum dma_transfer_direction direction, dma_async_tx_callback callback)
{
	struct dma_async_tx_descriptor *desc;

	desc = dmaengine_prep_dma_cyclic(dma_chan, dma_address, buffer_size, buffer_size / 2, direction, DMA_OOB_INTERRUPT);
	if (!desc)
	{
		return -EBUSY;
	}

	desc->callback = callback;

	dmaengine_submit(desc);
	dma_async_issue_pending(dma_chan);

	return 0;
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
	driver_unregister(&i2s_thing_driver.driver);
	device_destroy(device_class, MKDEV(major_number, 0));
	unregister_chrdev(major_number, CHAR_DEVICE_NAME);
	class_destroy(device_class);
}

static int i2s_thing_probe(struct platform_device *pdev)
{
	void __iomem *register_address;
	const __be32 *dma_address_be32;
	dma_addr_t dma_address;

	// Register map
	register_address = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(register_address))
	{
		return PTR_ERR(register_address);
	}

	regmap = devm_regmap_init_mmio(&pdev->dev, register_address, &i2s_thing_regmap_config);
	if (IS_ERR(regmap))
	{
		return PTR_ERR(regmap);
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

	config_dma_channel(&pdev->dev, &dma_chan_tx, "tx");
	config_dma_channel(&pdev->dev, &dma_chan_rx, "rx");

	i2s_device = &pdev->dev;

	return 0;
}

static int i2s_thing_remove(struct platform_device *pdev)
{
	dma_free_coherent(i2s_device, tx_buffer.size, tx_buffer.ptr, tx_buffer.dma_address);
	dma_free_coherent(i2s_device, rx_buffer.size, rx_buffer.ptr, rx_buffer.dma_address);

    dmaengine_terminate_sync(dma_chan_tx);
	dma_release_channel(dma_chan_tx);

    dmaengine_terminate_sync(dma_chan_rx);
	dma_release_channel(dma_chan_rx);

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
		printk(KERN_ALERT "evl_open_file failed");
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
	unsigned long error_count;

	if (size > rx_buffer.size)
	{
		printk(KERN_ALERT "Read size %lu larger than buffer size %lu", size, rx_buffer.size);
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

	if (size > tx_buffer.size)
	{
		printk(KERN_ALERT "Write size %lu larger than buffer size %lu", size, tx_buffer.size);
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
	unsigned int bits;
	size_t i;
	size_t buffer_size_bytes;

	buffer_size_bytes = buffer_size * sizeof(s16);

	// Allocate DMA buffers
	ret = alloc_dma_buffer(&tx_buffer, buffer_size_bytes);
	if (ret)
	{
		printk(KERN_ALERT "Failed allocating DMA buffer");
		return ret;
	}
	
	ret = alloc_dma_buffer(&rx_buffer, buffer_size_bytes);
	if (ret)
	{
		printk(KERN_ALERT "Failed allocating DMA buffer");
		return ret;
	}

	// Enable DMA
	start_dma(dma_chan_tx, tx_buffer.dma_address, buffer_size, DMA_MEM_TO_DEV, dma_tx_complete);
	start_dma(dma_chan_rx, rx_buffer.dma_address, buffer_size, DMA_DEV_TO_MEM, dma_rx_complete);

	// Enable PCM device and clear standby
	regmap_write(regmap, CS_A, CS_EN | CS_STBY);

	// Change settings
	regmap_write(regmap, MODE_A, MODE_FSM | MODE_CLKM | MODE_FSI | MODE_CLKI | MODE_FTXP | MODE_FRXP | MODE_FLEN(64));

	bits = XC_CH1EN | XC_CH1POS(1) | XC_CH1WID(16) | XC_CH2EN | XC_CH2POS(33) | XC_CH2WID(16);
	regmap_write(regmap, TXC_A, bits);
	regmap_write(regmap, RXC_A, bits);

	// Clear FIFO
	bits = CS_RXCLR | CS_TXCLR;
	regmap_update_bits(regmap, CS_A, bits, bits);

	// Set up DREQ
	regmap_write(regmap, DREQ_A, DREQ_TX_REQ(0x30) | DREQ_TX_PANIC(0x10) | DREQ_RX_REQ(0x20) | DREQ_RX_PANIC(0x30));

	// Enable transmission
	bits = CS_DMAEN | CS_TXON | CS_RXON;
	regmap_update_bits(regmap, CS_A, bits, bits);

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
