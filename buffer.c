#include "buffer.h"

#include "dma.h"

int i2st_buffer_init(struct device* dev, struct i2st_buffer* buffer, unsigned int period_frames, unsigned int period_count)
{
    int ret;
	size_t period_size_bytes;
    size_t buffer_size_bytes;

    memset(buffer, 0, sizeof(struct i2st_buffer));

	// Two channels for stereo, with 16-bit samples
	period_size_bytes = period_frames * 2 * sizeof(s16);
    buffer_size_bytes = period_size_bytes * period_count;

	ret = i2st_dma_alloc(dev, buffer_size_bytes, (void*)&buffer->ptr, &buffer->dma_address);
	if (ret)
	{
		printk(KERN_ALERT "Failed allocating DMA buffer");
		return ret;
	}

    evl_init_flag(&buffer->flag);

    buffer->size = buffer_size_bytes;
    buffer->period_size = period_size_bytes;

    return 0;
}

void i2st_buffer_release(struct device* dev, struct i2st_buffer* buffer)
{
    evl_destroy_flag(&buffer->flag);
    i2st_dma_release(dev, buffer->size, buffer->ptr, buffer->dma_address);
    memset(buffer, 0, sizeof(struct i2st_buffer));
}

ssize_t i2st_buffer_read(struct i2st_buffer* buffer, const char __user *ptr, size_t size)
{
	unsigned long error_count;

	if (size != buffer->period_size)
	{
		printk(KERN_ALERT "Read size %lu doesn't match period size %lu", size, buffer->period_size);
		return -EINVAL;
	}

    // Wait on flag if there's no data to read
    while (i2st_buffer_available(buffer) < buffer->period_size)
    {
        evl_wait_flag(&buffer->flag);
    }

	error_count = raw_copy_to_user(ptr, buffer->ptr + buffer->user_offset, size);
	if (error_count != 0)
	{
		printk(KERN_ALERT "Reading from buffer failed");
		return -EFAULT;
	}

    buffer->user_offset = (buffer->user_offset + buffer->period_size) % buffer->size;

	return size;
}

ssize_t i2st_buffer_write(struct i2st_buffer* buffer, const char __user *ptr, size_t size)
{
	unsigned long error_count;

	if (size != buffer->period_size)
	{
		printk(KERN_ALERT "Write size %lu doesn't match period size %lu", size, buffer->period_size);
		return -EINVAL;
	}

    // Wait on flag if there's no space to write
    while (i2st_buffer_available(buffer) < buffer->period_size)
    {
        evl_wait_flag(&buffer->flag);
    }

	error_count = raw_copy_from_user(buffer->ptr + buffer->user_offset, ptr, size);
	if (error_count != 0)
	{
		printk(KERN_ALERT "Writing to buffer failed");
		return -EFAULT;
	}

    buffer->user_offset = (buffer->user_offset + buffer->period_size) % buffer->size;

	return size;
}

void i2st_buffer_dma_complete(struct i2st_buffer* buffer)
{
    buffer->dma_offset = (buffer->dma_offset + buffer->period_size) % buffer->size;
    evl_raise_flag(&buffer->flag);
}

size_t i2st_buffer_available(struct i2st_buffer* buffer)
{
	unsigned long flags;
    size_t dma_offset;
    size_t user_offset;
	size_t buffer_size;

	local_irq_save(flags);
	buffer_size = buffer->size;
	dma_offset = buffer->dma_offset;
	user_offset = buffer->user_offset;
	local_irq_restore(flags);

    if (dma_offset >= user_offset)
    {
        return dma_offset - user_offset;
    }

    return buffer_size - user_offset + dma_offset;
}
