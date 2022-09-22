#ifndef I2S_THING_I2S_H
#define I2S_THING_I2S_H

#include <linux/platform_device.h>
#include <linux/regmap.h>

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

static const struct regmap_config i2st_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = GRAY,
	.cache_type = REGCACHE_NONE,
};

int i2st_i2s_init_regmap(struct platform_device *pdev, struct regmap** regmap);
void i2st_i2s_start(struct regmap* regmap);
void i2st_i2s_stop(struct regmap* regmap);

#endif
