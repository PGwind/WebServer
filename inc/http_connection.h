#ifndef HTTP_CONNECTION_H
#define HTTP_CONNECTION_H

#include <event2/util.h>

struct bufferevent;
struct evconnlistener;
struct sockaddr;

void conn_eventcb(struct bufferevent *bev, short events, void *user_data);

void conn_readcd(struct bufferevent *bev, void *user_data);

void listener_cb(struct evconnlistener *listener, evutil_socket_t fd,
                 struct sockaddr *sa, int socklen, void *user_data);

void signal_cb(evutil_socket_t sig, short events, void *user_data);

#endif
