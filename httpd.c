#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <getopt.h>
#include <stdarg.h>

struct {
  int port;
  const char *site;
} G;

void show_usage(const char *name);
void app_error(const char *format, ...);

int main(int argc, char *argv[]) {
  int opt;

  app_error("hello, %s, %d", "ab", 123);
  /* initialize global */
  G.port = 0;
  G.site = NULL;

  /* process args */
  while (1) {
		static const char *optstring = "p:h";
		static const struct option longopts[] = {
			{ "port", required_argument, NULL, 'p' },
			{ "help", no_argument, NULL, 'h' },
			{ NULL, no_argument, NULL, 0 }
		};

		opt = getopt_long(argc, argv, optstring, longopts, NULL);
		if (opt == -1)
			break;
		
		switch (opt) {
			case 'p': G.port = atoi(optarg); break;
			/* 0, ?, etc. */
			default: show_usage(argv[0]);
		}	
	} 

  if (G.port == 0)
    show_usage(argv);
  
  if (optind >= argc) {
    fprintf(stderr, "Expected argument after options\n");
    exit(1);
  }

  while (argv[optind]) {
    printf("name argument = %s\n", argv[optind++]);
  }

  return 0;
}

void show_usage(const char *name) {
    printf("Usage: %s [-p, --show-pids] "
		"[-n, --numeric-sort] [-V, --version]\n", name);
    exit(1);
}

void app_error(const char *format, ...) {
  va_list ap;
  va_start(ap, format);
  vfprintf(stderr, format, ap);
  va_end(ap);
}