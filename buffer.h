#ifndef I2S_THING_BUFFER_H
#define I2S_THING_BUFFER_H

#include <linux/dmaengine.h>

struct i2s_thing_buffer
{
	s16* ptr;
	dma_addr_t dma_address;

	size_t size;
    size_t period_size;
    size_t dma_offset;
    size_t io_offset;
};

int i2s_thing_buffer_init(struct device* dev, struct i2s_thing_buffer* buffer, unsigned int period_frames, unsigned int period_count);
void i2s_thing_buffer_release(struct device* dev, struct i2s_thing_buffer* buffer);
ssize_t i2s_thing_buffer_read(struct i2s_thing_buffer* i2s_buffer, const char __user *buf, size_t size);
ssize_t i2s_thing_buffer_write(struct i2s_thing_buffer* i2s_buffer, const char __user *buf, size_t size);

#endif
