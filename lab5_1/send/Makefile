PROJ=send
CC=cc
SRC=send.c
LIBS=-lgpiod
CFLAGS=-D CONSUMER=\"$(PROJ)\"

all:
	$(CC) $(SRC) $(LIBS) $(CFLAGS) -o $(PROJ)

clean:
	rm $(PROJ)
