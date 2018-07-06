BIN = httpd

.PHONY: build clean

build: $(BIN).c
	gcc -std=gnu99 -O1 -Wall -o $(BIN) $(BIN).c

clean:
	rm $(BIN)
