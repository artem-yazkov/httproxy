CC = gcc
CFLAGS  = -g -Wall

TARGET = httproxy

all:
	$(CC) $(CFLAGS) -o $(TARGET) proxy.c cache.c

clean:
	$(RM) $(TARGET)