OBJS	= main.o map.o
SOURCE	= main.c map.c
HEADER	=
OUT	= a.out
CC	 = gcc
LFLAGS	 = -pthread
FLAGS = -g -Wall -Wextra -pedantic -Werror -target x86_64-apple-darwin20.3.0



all: $(OBJS)
	$(CC) -g $(OBJS) -o $(OUT) $(LFLAGS)

main.o: main.c
	$(CC) $(FLAGS) main.c

map.o: map.c
	$(CC) $(FLAGS) map.c


clean:
	rm -f $(OBJS) $(OUT)
