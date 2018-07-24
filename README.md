# httpd

This is a httpd server with `multithread` and `epoll` support.

## Build
Just use `make`

## Usage

    ./httpd [-p PORT, --port PORT] [-h, --help] DIR

Here is an exemple
 
	./httpd -p 8080 ./site

`./site` is an example site in root directory.

## TODO
* Session control