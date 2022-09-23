#include "buffer.h"

#include "dma.h"

static int prepare_io(struct i2st_buffer* buffer, size_t size)
{
	int ret;

	if (size != buffer->period_size)
	{
		printk(KERN_ALERT "IO size %lu doesn't match period size %lu", size, buffer->period_size);
		return -EINVAL;
	}

    // Wait until there's data available
	ret = i2st_buffer_wait_available(buffer);
	if (ret) { return ret; }

	return 0;
}

static void advance_user_offset(struct i2st_buffer* buffer)
{
	unsigned long flags;

    buffer->user_offset = (buffer->user_offset + buffer->period_size) % buffer->size;

	local_irq_save(flags);
	buffer->available -= buffer->period_size;
	local_irq_restore(flags);
}

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
	int ret;
	unsigned long error_count;

	ret = prepare_io(buffer, size);
	if (ret == EXRUN) { return 0; }
	else if (ret) { return ret; }

	error_count = raw_copy_to_user(ptr, buffer->ptr + buffer->user_offset, size);
	if (error_count != 0)
	{
		printk(KERN_ALERT "Reading from buffer failed");
		return -EFAULT;
	}

	advance_user_offset(buffer);

	return size;
}

ssize_t i2st_buffer_write(struct i2st_buffer* buffer, const char __user *ptr, size_t size)
{
	int ret;
	unsigned long error_count;

	ret = prepare_io(buffer, size);
	if (ret == EXRUN) { return 0; }
	else if (ret) { return ret; }

	error_count = raw_copy_from_user(buffer->ptr + buffer->user_offset, ptr, size);
	if (error_count != 0)
	{
		printk(KERN_ALERT "Writing to buffer failed");
		return -EFAULT;
	}

	advance_user_offset(buffer);

	return size;
}

void i2st_buffer_dma_complete(struct i2st_buffer* buffer)
{
    buffer->dma_offset = (buffer->dma_offset + buffer->period_size) % buffer->size;
	buffer->available += buffer->period_size;
	if (buffer->available > buffer->size)
	{
		++buffer->xruns;
	}

    evl_raise_flag(&buffer->flag);
}

size_t i2st_buffer_available(struct i2st_buffer* buffer)
{
	size_t available;
	unsigned long flags;

	local_irq_save(flags);
	available = buffer->available;
	local_irq_restore(flags);

	return available;
}

int i2st_buffer_wait_available(struct i2st_buffer* buffer)
{
	unsigned long flags;
	unsigned long xruns;

	local_irq_save(flags);
	xruns = buffer->xruns;
	local_irq_restore(flags);

	if (xruns)
	{
		return EXRUN;
	}

	while (i2st_buffer_available(buffer) < buffer->period_size)
	{
        evl_wait_flag(&buffer->flag);
    }

	return 0;
}

void i2st_buffer_reset_xrun(struct i2st_buffer* buffer)
{
	unsigned long flags;

	local_irq_save(flags);
	buffer->xruns = 0;
	local_irq_restore(flags);
}
