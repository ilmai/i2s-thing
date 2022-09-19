#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>

static int i2s_thing_probe(struct platform_device *pdev)
{
    return 0;
}

static const struct of_device_id i2s_thing_of_match[] = {
	{ .compatible = "brcm,bcm2835-i2s", },
	{},
};

MODULE_DEVICE_TABLE(of, i2s_thing_of_match);

static struct platform_driver i2s_thing_driver = {
	.probe		= i2s_thing_probe,
	.driver		= {
		.name	= "i2s-thing",
		.of_match_table = i2s_thing_of_match,
	},
};

module_platform_driver(i2s_thing_driver);

MODULE_ALIAS("platform:i2s-thing");
MODULE_DESCRIPTION("I2S driver thing");
MODULE_AUTHOR("Jussi Viiri <jussi.viiri@iki.fi>");
MODULE_LICENSE("GPL");
