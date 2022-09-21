#ifndef I2S_THING_DMA_H
#define I2S_THING_DMA_H

#include <linux/dmaengine.h>

struct i2s_thing_dma_buffer
{
	size_t size;
	size_t length;
	s16* ptr;
	dma_addr_t dma_address;
};

int i2s_thing_dma_alloc(struct device* dev, struct i2s_thing_dma_buffer* buffer, unsigned int size);
void i2s_thing_dma_release(struct device* dev, struct i2s_thing_dma_buffer* buffer);
int i2s_thing_dma_create_channel(struct device* dev, struct dma_chan** channel, const char* name, dma_addr_t address_offset);
void i2s_thing_dma_close_channel(struct dma_chan* channel);
int i2s_thing_dma_start(struct dma_chan *dma_chan, dma_addr_t dma_address, unsigned int buffer_size, enum dma_transfer_direction direction, dma_async_tx_callback callback);

#endif
