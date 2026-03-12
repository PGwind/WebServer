#ifndef _LIBEVENT_HTTP_H
#define _LIBEVENT_HTTP_H

#include <event2/util.h>

struct bufferevent;
struct evconnlistener;
struct sockaddr;
struct client_context;

void conn_eventcb(struct bufferevent *bev, short events, void *user_data);

void conn_readcd(struct bufferevent *bev, void *user_data);

void listener_cb(struct evconnlistener *listener, evutil_socket_t fd,
                 struct sockaddr *sa, int socklen, void *user_data);

int response_http(struct bufferevent *bev, const char *method, char *path,
                  struct client_context *ctx);

int send_dir(struct bufferevent *bev, const char *dirname,
             struct client_context *ctx);

int send_error(struct bufferevent *bev, struct client_context *ctx);

int send_file_to_http(const char *filename, struct bufferevent *bev,
                      struct client_context *ctx);

int send_header(struct bufferevent *bev, int no, const char* desp, const char *type, long len);

void signal_cb(evutil_socket_t sig, short events, void *user_data);

#endif
