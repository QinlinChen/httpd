TARG = httpd
OBJ = httpd.o http-utils.o rio.o error.o queue.o
CC = gcc
CFLAGS = -g -O2 -Wall

$(TARG): $(OBJ)
	$(CC) $(CFLAGS) -o $(TARG) $(OBJ) -lpthread
	
run: $(TARG)
	./$(TARG) -p 8080 ./site

.PHONY: clean cleanobj

clean: cleanobj
	rm $(TARG)

cleanobj:
	rm $(OBJ)
