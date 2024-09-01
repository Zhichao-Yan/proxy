CC = gcc
CFLAGS = -g -Wall -I$(INCLUDE)
INCLUDE := .
CGI_DIR := cgi-bin
CGI_FILES := $(wildcard $(CGI_DIR)/*.c)
CGI_EXE := $(patsubst %.c,%,$(CGI_FILES))

all: proxy $(CGI_EXE) 

proxy: proxy.c sbuf.c queue.c rio.c
	$(CC) $(CFLAGS)  $^ -o $@ 

$(CGI_EXE): %: %.c
	$(CC) $(CFLAGS) $^ -o $@

clean:
	rm -f proxy $(CGI_EXE)

.PHONY: all clean