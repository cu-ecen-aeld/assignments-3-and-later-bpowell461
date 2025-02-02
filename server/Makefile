NAME = aesdsocket
CC ?= $(CROSS_COMPILE)gcc
CFLAGS = -Wall -Wextra
LDFLAGS ?= -pthread -lrt
DEFINES ?= -DUSE_AESD_CHAR_DEVICE
SRC = aesdsocket.c
OBJ = $(SRC:.c=.o)
all: $(NAME)

$(NAME): $(OBJ)
	$(CC) $(DEFINES) $(CFLAGS) $(OBJ) -o $(NAME) $(LDFLAGS)
clean:
	rm -rf $(NAME)
	rm -rf $(OBJ)