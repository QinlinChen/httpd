# httpd

This is a httpd server.
It has a multi-thread version on branch `multithread`.
However, it cannot be shutdown gracefully.

## Build
Just use `make`

## Usage

    ./httpd [-p PORT, --port PORT] [-h, --help] DIR

Here is an exemple
 
	./httpd -p 8080 ./site

`./site` is an example site in root directory.