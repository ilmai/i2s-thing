#include "buffer.h"

#include "dma.h"

int i2s_thing_buffer_init(struct device* dev, struct i2s_thing_buffer* buffer, unsigned int period_frames, unsigned int period_count)
{
    int ret;
	size_t period_size_bytes;
    size_t buffer_size_bytes;

    memset(buffer, 0, sizeof(struct i2s_thing_buffer));

	// Two channels for stereo, with 16-bit samples
	period_size_bytes = period_frames * 2 * sizeof(s16);
    buffer_size_bytes = period_size_bytes * period_count;

	ret = i2s_thing_dma_alloc(dev, buffer_size_bytes, (void*)&buffer->ptr, &buffer->dma_address);
	if (ret)
	{
		printk(KERN_ALERT "Failed allocating DMA buffer");
		return ret;
	}

    buffer->size = buffer_size_bytes;
    buffer->period_size = period_size_bytes;

    return 0;
}

void i2s_thing_buffer_release(struct device* dev, struct i2s_thing_buffer* buffer)
{
    i2s_thing_dma_release(dev, buffer->size, buffer->ptr, buffer->dma_address);
    memset(buffer, 0, sizeof(struct i2s_thing_buffer));
}

ssize_t i2s_thing_buffer_read(struct i2s_thing_buffer* i2s_buffer, const char __user *buf, size_t size)
{
	unsigned long error_count;

	if (size != i2s_buffer->period_size)
	{
		printk(KERN_ALERT "Read size %lu doesn't match period size %lu", size, i2s_buffer->period_size);
		return -EINVAL;
	}

	error_count = raw_copy_to_user(buf, i2s_buffer->ptr, size);
	if (error_count != 0)
	{
		printk(KERN_ALERT "Reading from buffer failed");
		return -EFAULT;
	}

	return size;
}

ssize_t i2s_thing_buffer_write(struct i2s_thing_buffer* i2s_buffer, const char __user *buf, size_t size)
{
	unsigned long error_count;

	if (size != i2s_buffer->period_size)
	{
		printk(KERN_ALERT "Write size %lu doesn't match period size %lu", size, i2s_buffer->period_size);
		return -EINVAL;
	}

	error_count = raw_copy_from_user(i2s_buffer->ptr, buf, size);
	if (error_count != 0)
	{
		printk(KERN_ALERT "Writing to buffer failed");
		return -EFAULT;
	}

	return size;
}
