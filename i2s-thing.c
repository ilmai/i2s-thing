#include <evl/file.h>
#include <evl/flag.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>

#define CHAR_DEVICE_NAME "i2s-thing"
#define DEVICE_CLASS_NAME "i2s-thing-class"
#define DRIVER_NAME "i2s-thing-io"

static int major_number;
static struct class *device_class = NULL;
static struct device *char_device = NULL;
static struct evl_file efile;
static struct evl_flag eflag;
static bool char_device_opened = false;

struct i2s_thing_device
{

};

static int i2s_thing_probe(struct platform_device *pdev);

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
	.driver = {
		.name = DRIVER_NAME,
		.of_match_table = i2s_thing_of_match,
	},
};

static int __init i2s_thing_init(void)
{
	int ret;

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
