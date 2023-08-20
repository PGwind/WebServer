#ifndef _LIBEVENT_HTTP_H
#define _LIBEVENT_HTTP_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <time.h>
#include <signal.h>
#include <ctype.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <event2/listener.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <event2/event.h>

void conn_eventcb(struct bufferevent *bev, short events, void *user_data);

void conn_readcb(struct bufferevent *bev, void *user_data);

void listener_cb(struct evconnlistener *listener, evutil_socket_t fd,
                 struct sockaddr *sa, int socklen, void *user_data);

int response_http(struct bufferevent *bev, const char *method, char *path);

int send_dir(struct bufferevent *bev,const char *dirname);

int send_error(struct bufferevent *bev);

int send_file_to_http(const char *filename, struct bufferevent *bev);

int send_header(struct bufferevent *bev, int no, const char* desp, const char *type, long len);

void signal_cb(evutil_socket_t sig, short events, void *user_data);

#endif
