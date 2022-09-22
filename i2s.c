#include "i2s.h"

#include <linux/memory.h>
#include <linux/platform_device.h>

int i2s_thing_i2s_init_regmap(struct platform_device *pdev, struct regmap** regmap)
{
	void __iomem *register_address;

	register_address = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(register_address))
	{
		return PTR_ERR(register_address);
	}

	*regmap = devm_regmap_init_mmio(&pdev->dev, register_address, &i2s_thing_regmap_config);
	if (IS_ERR(*regmap))
	{
		return PTR_ERR(*regmap);
	}

    return 0;
}

void i2s_thing_i2s_start(struct regmap* regmap)
{
    unsigned int bits;

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
}

void i2s_thing_i2s_stop(struct regmap* regmap)
{
	regmap_write(regmap, CS_A, 0);
}
