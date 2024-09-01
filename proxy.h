#ifndef PROXY_H
#define PROXY_H

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/mman.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/select.h>
#include <sys/stat.h>
#include "sbuf.h"
#include "queue.h"
#include "rio.h"


void signal_sigchld(int sig);
void ignore_sigpipe(int sig);

int open_listenfd(char *port);

void* manager(void *arg);
void* worker(void *arg);

void serve_static(int fd,char *filename,char *args,int head_only);
void serve_dynamic(int fd,char *filename,char *cgi_args,int head_only);
void method_get(int fd,char *filename,char *args,int head_only);
void method_head(int fd,char *filename,char *args);
void method_post(int fd,char *filename,char *body);
void transaction(int clientfd);
void local_service(int clientfd,char *method,char *path,char *body);


void send_response(int fd,const char *line,const char *header,const char *body);
void send_error_response(int fd,char *version,const char *code,const char *status,const char *msg,const char *cause,int head_only);

void parse_url(char *url,char *scheme,char *host,char *path);
void parse_headers(const char *headers,const char *name,char *value);
void parse_host(char *host,char *port);
void parse_request(int clientfd,const char *buf);
void parse_path(char *path,char *filename,char *args);



#endif 