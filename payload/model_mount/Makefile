ifndef PS5_PAYLOAD_SDK
$(error Set PS5_PAYLOAD_SDK to the payload SDK)
endif
include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk
ROOT := $(abspath $(CURDIR)/../..)
OUTPUT := $(ROOT)/build/model-mount
all: $(OUTPUT)/model-mount.elf
$(OUTPUT)/model-mount.elf: main.c $(ROOT)/include/model_paths.hpp
	mkdir -p $(OUTPUT)
	$(CC) -std=c11 -O2 -Wall -Wextra -Werror -I$(ROOT)/include -o $@ $< -lSceSystemService
.PHONY: all
