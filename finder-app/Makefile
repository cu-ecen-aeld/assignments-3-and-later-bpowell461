CROSS_COMPILE ?= cc
SRC = writer.c
OUT = writer
all: $(OUT)
ifeq ($(CROSS_COMPILE),aarch64-none-linux-gnu-)
$(OUT): $(SRC)
	aarch64-none-linux-gnu-gcc $(SRC) -o $(OUT)
else
$(OUT): $(SRC)
	$(CROSS_COMPILE) $(SRC) -o $(OUT)
endif
.PHONY: clean
clean:
	rm -f $(OUT) *.o