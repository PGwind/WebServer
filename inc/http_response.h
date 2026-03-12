#ifndef HTTP_RESPONSE_H
#define HTTP_RESPONSE_H

struct bufferevent;
struct client_context;

int response_http(struct bufferevent *bev, const char *method, char *path,
                  struct client_context *ctx);

int send_html_response(struct bufferevent *bev, struct client_context *ctx,
                       int no, const char *desp, const char *body,
                       int send_body, const char *extra_headers);

int send_error(struct bufferevent *bev, struct client_context *ctx, int send_body);

int send_file_to_http(const char *filename, struct bufferevent *bev,
                      struct client_context *ctx, int send_body);

int send_header(struct bufferevent *bev, int no, const char *desp, const char *type,
                long len, const char *extra_headers);

#endif
