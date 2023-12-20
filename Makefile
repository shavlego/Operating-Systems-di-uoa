CC=g++
CFLAGS=-Wall -pthread
LIBS=-lrt

all: processA processB

processA: processA.cpp
	$(CC) $(CFLAGS) $< -o $@ $(LIBS)

processB: processB.cpp
	$(CC) $(CFLAGS) $< -o $@ $(LIBS)

clean:
	rm -f processA processB

