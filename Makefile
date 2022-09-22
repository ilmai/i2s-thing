obj-m += i2s-thing.o
i2s-thing-y += i2s-thing-module.o buffer.o dma.o i2s.o

all: modules

modules:
	$(MAKE) -C $(LINUX_DIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(LINUX_DIR) M=$(PWD) clean

.PHONY: all modules clean
