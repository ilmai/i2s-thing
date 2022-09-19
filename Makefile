obj-m += i2s-thing.o

all: modules

modules:
	$(MAKE) -C $(LINUX_DIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(LINUX_DIR) M=$(PWD) clean

.PHONY: all modules clean
