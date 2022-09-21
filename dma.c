#include <linux/dma-mapping.h>
#include <linux/of_address.h>

#include "dma.h"

int i2s_thing_dma_create_channel(struct device* dev, struct dma_chan** channel, const char* name, dma_addr_t address_offset)
{
	const __be32 *address_be32;
	dma_addr_t address;

    struct dma_slave_config config =
    {
        .src_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES,
        .dst_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES,
        .src_maxburst = 2,
        .dst_maxburst = 2,
    };

	// Set address
	address_be32 = of_get_address(dev->of_node, 0, NULL, NULL);
	if (!address_be32)
	{
		dev_err(dev, "Error getting DMA address\n");
		return -EINVAL;
	}
	address = be32_to_cpup(address_be32) + address_offset;

    config.src_addr = address,
    config.dst_addr = address,

    // Channel
	*channel = dma_request_chan(dev, name);

	if (IS_ERR(*channel))
	{
		dev_err(dev, "DMA TX channel request failed\n");
		*channel = NULL;
		return PTR_ERR(*channel);
	}

	dmaengine_slave_config(*channel, &config);

	return 0;
}

int i2s_thing_dma_alloc(struct device* dev, struct i2s_thing_dma_buffer* buffer, unsigned int size)
{
	void* buffer_ptr;
	
	buffer_ptr = dma_alloc_coherent(dev, PAGE_ALIGN(size), &buffer->dma_address, GFP_KERNEL);
	if (!buffer_ptr)
	{
		return -ENOMEM;
	}

	buffer->size = size;
	buffer->length = size / sizeof(s16);
	buffer->ptr = (s16*)buffer_ptr;

	return 0;
}

void i2s_thing_dma_release(struct device* dev, struct i2s_thing_dma_buffer* buffer)
{
	dma_free_coherent(dev, PAGE_ALIGN(buffer->size), buffer->ptr, buffer->dma_address);
    memset(buffer, 0, sizeof(struct i2s_thing_dma_buffer));
}

void i2s_thing_dma_close_channel(struct dma_chan* channel)
{
    dmaengine_terminate_sync(channel);
	dma_release_channel(channel);
}

int i2s_thing_dma_start(struct dma_chan *dma_chan, dma_addr_t dma_address, unsigned int buffer_size, enum dma_transfer_direction direction, dma_async_tx_callback callback)
{
	struct dma_async_tx_descriptor *desc;

	desc = dmaengine_prep_dma_cyclic(dma_chan, dma_address, buffer_size, buffer_size / 2, direction, DMA_OOB_INTERRUPT);
	if (!desc)
	{
		return -EBUSY;
	}

	desc->callback = callback;

	dmaengine_submit(desc);
	dma_async_issue_pending(dma_chan);

	return 0;
}
