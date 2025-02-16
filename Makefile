CC = g++
CFLAGS = -g -Wall -Wextra
TARGET = bread

all: bread

bread: main.o
	$(CC) main.o -o $(TARGET)

main.o:
	$(CC) -c src/main.cpp $(CFLAGS)

clean: 
	rm bread main.o