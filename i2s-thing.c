#include <linux/fs.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>

#define CHAR_DEVICE_NAME "i2s-thing"
#define DEVICE_CLASS_NAME "i2s-thing-class"
#define DRIVER_NAME "i2s-thing-io"

static int majorNumber;
static struct class *charDeviceClass = NULL;
static struct device *charDevice = NULL;

struct i2s_thing_device
{

};

static int i2s_thing_probe(struct platform_device *pdev);

static struct file_operations fops =
{

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

	majorNumber = register_chrdev(0, CHAR_DEVICE_NAME, &fops);
	if (majorNumber < 0)
	{
		printk(KERN_ALERT "Failed to register major number\n");
		return majorNumber;
	}

	charDeviceClass = class_create(THIS_MODULE, DEVICE_CLASS_NAME);
	if (IS_ERR(charDeviceClass))
	{
		unregister_chrdev(majorNumber, CHAR_DEVICE_NAME);
		printk(KERN_ALERT "Failed to create device class\n");
		return PTR_ERR(charDeviceClass);
	}

	charDevice = device_create(charDeviceClass, NULL, MKDEV(majorNumber, 0), NULL, CHAR_DEVICE_NAME);
	if (IS_ERR(charDevice))
	{
		class_destroy(charDeviceClass);
		unregister_chrdev(majorNumber, CHAR_DEVICE_NAME);
		printk(KERN_ALERT "Failed to create device\n");
		return PTR_ERR(charDevice);
	}

	i2s_thing_driver.driver.owner = THIS_MODULE;
	i2s_thing_driver.driver.bus = &platform_bus_type;

	ret = driver_register(&i2s_thing_driver.driver);
	if (ret)
	{
		device_destroy(charDeviceClass, MKDEV(majorNumber, 0));
		class_unregister(charDeviceClass);
		class_destroy(charDeviceClass);
		unregister_chrdev(majorNumber, CHAR_DEVICE_NAME);

		printk(KERN_ALERT "Failed to register driver\n");
		return ret;
	}

	return 0;
}

static void __exit i2s_thing_exit(void)
{
	driver_unregister(&i2s_thing_driver.driver);
	device_destroy(charDeviceClass, MKDEV(majorNumber, 0));
	unregister_chrdev(majorNumber, CHAR_DEVICE_NAME);
	class_destroy(charDeviceClass);
}

static int i2s_thing_probe(struct platform_device *pdev)
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
