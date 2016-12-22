CC = gcc
CFLAGS  = -g -Wall -std=gnu99

TARGET = httproxy

all:
	$(CC) $(CFLAGS) -o $(TARGET) proxy.c cache.c hash/cuckoo_hash.c hash/lookup3.c

clean:
	$(RM) $(TARGET)