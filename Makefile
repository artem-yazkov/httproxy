CC = gcc
CFLAGS  = -g -Wall -std=gnu99

TARGET = httproxy

all:
	$(CC) $(CFLAGS) -o $(TARGET) proxy.c cache.c

clean:
	$(RM) $(TARGET)