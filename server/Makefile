NAME = aesdsocket
CC ?= $(CROSS_COMPILE)gcc
CFLAGS = -Wall -Wextra
LDFLAGS ?= -pthread -lrt
SRC = aesdsocket.c
OBJ = $(SRC:.c=.o)
all: $(NAME)

$(NAME): $(OBJ)
	$(CC) $(CFLAGS) $(OBJ) -o $(NAME) $(LDFLAGS)
clean:
	rm -rf $(NAME)
	rm -rf $(OBJ)