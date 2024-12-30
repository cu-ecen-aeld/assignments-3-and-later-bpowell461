NAME = aesdsocket
CC = gcc
CFLAGS = -Wall -Wextra -Werror
SRC = aesdsocket.c
OBJ = $(SRC:.c=.o)
all: $(NAME)
$(NAME): $(OBJ)
	$(CC) $(CFLAGS) $(OBJ) -o $(NAME)
clean:
	rm -rf $(NAME)
	rm -rf $(OBJ)