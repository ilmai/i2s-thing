#include <linux/dma-mapping.h>
#include <linux/of_address.h>

#include "dma.h"

int i2st_dma_create_channel(struct device* dev, struct dma_chan** channel, const char* name, dma_addr_t address_offset)
{
	int ret;
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
		ret = PTR_ERR(*channel);
		*channel = NULL;
		return ret;
	}

	ret = dmaengine_slave_config(*channel, &config);
	if (ret)
	{
		dev_err(dev, "DMA config failed\n");
		dma_release_channel(*channel);
		*channel = NULL;
		return ret;
	}

	return 0;
}

int i2st_dma_alloc(struct device* dev, size_t size, void** ptr, dma_addr_t* dma_address)
{
	*ptr = dma_alloc_coherent(dev, PAGE_ALIGN(size), dma_address, GFP_KERNEL);
	if (!*ptr)
	{
		*ptr = NULL;
		return -ENOMEM;
	}

	return 0;
}

void i2st_dma_release(struct device* dev, size_t size, void* ptr, dma_addr_t dma_address)
{
	dma_free_coherent(dev, PAGE_ALIGN(size), ptr, dma_address);
}

void i2st_dma_close_channel(struct dma_chan* channel)
{
    dmaengine_terminate_sync(channel);
	dma_release_channel(channel);
}

int i2st_dma_start(struct dma_chan *dma_chan, dma_addr_t dma_address, unsigned int buffer_size, unsigned int period_size, enum dma_transfer_direction direction, dma_async_tx_callback callback)
{
	struct dma_async_tx_descriptor *desc;

	desc = dmaengine_prep_dma_cyclic(dma_chan, dma_address, buffer_size, period_size, direction, DMA_OOB_INTERRUPT);
	if (!desc)
	{
		return -EBUSY;
	}

	desc->callback = callback;

	dmaengine_submit(desc);
	dma_async_issue_pending(dma_chan);

	return 0;
}
