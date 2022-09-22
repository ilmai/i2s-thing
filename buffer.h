#ifndef I2S_THING_BUFFER_H
#define I2S_THING_BUFFER_H

#include <evl/flag.h>
#include <linux/dmaengine.h>

struct i2st_buffer
{
	char* ptr;
	dma_addr_t dma_address;

	size_t size;
    size_t period_size;
    size_t dma_offset;
    size_t user_offset;

    struct evl_flag flag;
};

int i2st_buffer_init(struct device* dev, struct i2st_buffer* buffer, unsigned int period_frames, unsigned int period_count);
void i2st_buffer_release(struct device* dev, struct i2st_buffer* buffer);
ssize_t i2st_buffer_read(struct i2st_buffer* buffer, const char __user *ptr, size_t size);
ssize_t i2st_buffer_write(struct i2st_buffer* buffer, const char __user *ptr, size_t size);
void i2st_buffer_dma_complete(struct i2st_buffer* buffer);
size_t i2st_buffer_available(struct i2st_buffer* buffer);

#endif
