NAME = aesdsocket
CC = gcc
CFLAGS = -Wall -Wextra -g -lc
SRC = aesdsocket.c
OBJ = $(SRC:.c=.o)
all: $(NAME)

$(NAME): $(OBJ)
	$(CC) $(CFLAGS) $(OBJ) -o $(NAME)
clean:
	rm -rf $(NAME)
	rm -rf $(OBJ)