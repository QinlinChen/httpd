TARG = httpd
OBJ = httpd.o http-utils.o rio.o error.o
COMPILER = gcc
CFLAGS = -g -O2 -Wall

TARG: $(OBJ)
	$(COMPILER) $(CFLAGS) -o $(TARG) $(OBJ) -lpthread
	
run: TARG
	./$(TARG) -p 8080 ./site

.PHONY: clean cleanobj

clean: cleanobj
	rm $(TARG)

cleanobj:
	rm $(OBJ)