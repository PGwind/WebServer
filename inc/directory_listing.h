#ifndef DIRECTORY_LISTING_H
#define DIRECTORY_LISTING_H

struct bufferevent;
struct client_context;

int send_dir(struct bufferevent *bev, const char *dirname,
             struct client_context *ctx, int send_body);

#endif
