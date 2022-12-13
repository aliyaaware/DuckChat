CC=g++
CFLAGS=-Wall -W -g

# Uncomment this line for the graduate student version
# CFLAGS= -g  -DGRAD=1

# LOADLIBES= -lnsl

all: client server

client: client.cpp raw.cpp
	$(CC) client.cpp raw.cpp $(LOADLIBES) $(CFLAGS) -o client

server: server.cpp 
	$(CC) server.cpp $(LOADLIBES) $(CFLAGS) -o server

clean:
	rm -f client server *.o

