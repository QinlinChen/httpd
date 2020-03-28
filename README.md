# httpd

This is a httpd server with `multithread` and `epoll` support.

## Structure

Here are some modules used in httpd:

* `http-utils`:
There are two functions used to open listen socket for server 
and connection for client. 
* `error`:
Error handle functions. `xxx_errq` is used to print error and quit.
`xxx_err` prints error but not quits.
* `queue`:
A thread-safe queue.
* `rio`:
Robust IO that can handle signal interruption and half read/write.
* `httpd`:
Core module.

## Build

Just use `make`.

## Usage

    ./httpd [-p PORT, --port PORT] [-h, --help] DIR

Here is an exemple
 
	./httpd -p 8080 ./site

`./site` is an example site in root directory.
