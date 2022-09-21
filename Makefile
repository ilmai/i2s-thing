obj-m += i2s-thing.o
i2s-thing-y += i2s-thing-module.o dma.o

all: modules

modules:
	$(MAKE) -C $(LINUX_DIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(LINUX_DIR) M=$(PWD) clean

.PHONY: all modules clean
