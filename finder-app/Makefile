# Variables
CC = aarch64-none-linux-gnu-gcc
CFLAGS = -Wall -Wextra -O2
SRC = writer.c
OUT = writer
INSTALL_DIR = /usr/local/bin

# Default target
all: $(OUT)

# Build target
$(OUT): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(OUT)

# Clean target
.PHONY: clean
clean:
	rm -f $(OUT) *.o

# Install target
.PHONY: install
install: $(OUT)
	install -m 0755 $(OUT) $(INSTALL_DIR)

# Uninstall target
.PHONY: uninstall
uninstall:
	rm -f $(INSTALL_DIR)/$(OUT)

# Help target
.PHONY: help
help:
	@echo "Makefile targets:"
	@echo "  all       - Build the binary"
	@echo "  clean     - Remove generated files"
	@echo "  install   - Install the binary to $(INSTALL_DIR)"
	@echo "  uninstall - Remove the installed binary"
	@echo "  help      - Display this help message"