CC = g++
CFLAGS = -O3 -Wall -Wextra
TARGET = bread

all: bread

bread: main.o log.o
		$(CC) main.o log.o -o $(TARGET) -lgpgme

main.o: src/main.cpp
		$(CC) -c src/main.cpp $(CFLAGS)

log.o: src/log.cpp
		$(CC) -c src/log.cpp $(CFLAGS)

clean:
		rm main.o log.o bread
