SRC = writer.c
OUT = writer
all: $(OUT)

$(OUT): $(SRC)
	aarch64-none-linux-gnu-gcc $(SRC) -o $(OUT)
.PHONY: clean
clean:
	rm -f $(OUT) *.o